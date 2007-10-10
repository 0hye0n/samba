/* 
 *  Unix SMB/CIFS implementation.
 *  RPC Pipe client / server routines for Dfs
 *  Copyright (C) Shirish Kalele	2000.
 *  Copyright (C) Jeremy Allison	2001.
 *  Copyright (C) Jelmer Vernooij	2005-2006.
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* This is the implementation of the dfs pipe. */

#include "includes.h"

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_MSDFS

/* This function does not return a WERROR or NTSTATUS code but rather 1 if
   dfs exists, or 0 otherwise. */

void _dfs_GetManagerVersion(pipes_struct *p, struct dfs_GetManagerVersion *r)
{
	if(lp_host_msdfs()) 
		*r->out.exist_flag = 1;
	else
		*r->out.exist_flag = 0;
}

WERROR _dfs_Add(pipes_struct *p, struct dfs_Add *r)
{
	struct junction_map jn;
	struct referral* old_referral_list = NULL;
	BOOL self_ref = False;
	int consumedcnt = 0;
	BOOL exists = False;

	pstring altpath;

	if (p->pipe_user.ut.uid != 0) {
		DEBUG(10,("_dfs_add: uid != 0. Access denied.\n"));
		return WERR_ACCESS_DENIED;
	}

	DEBUG(5,("init_reply_dfs_add: Request to add %s -> %s\\%s.\n",
		r->in.path, r->in.server, r->in.share));

	pstrcpy(altpath, r->in.server);
	pstrcat(altpath, "\\");
	pstrcat(altpath, r->in.share);

	/* The following call can change the cwd. */
	if(get_referred_path(p->mem_ctx, r->in.path, &jn, &consumedcnt, &self_ref)) {
		exists = True;
		jn.referral_count += 1;
		old_referral_list = jn.referral_list;
	} else {
		jn.referral_count = 1;
	}

	vfs_ChDir(p->conn,p->conn->connectpath);

	jn.referral_list = TALLOC_ARRAY(p->mem_ctx, struct referral, jn.referral_count);
	if(jn.referral_list == NULL) {
		DEBUG(0,("init_reply_dfs_add: talloc failed for referral list!\n"));
		return WERR_DFS_INTERNAL_ERROR;
	}

	if(old_referral_list) {
		memcpy(jn.referral_list, old_referral_list, sizeof(struct referral)*jn.referral_count-1);
	}
  
	jn.referral_list[jn.referral_count-1].proximity = 0;
	jn.referral_list[jn.referral_count-1].ttl = REFERRAL_TTL;

	pstrcpy(jn.referral_list[jn.referral_count-1].alternate_path, altpath);
  
	if(!create_msdfs_link(&jn, exists)) {
		vfs_ChDir(p->conn,p->conn->connectpath);
		return WERR_DFS_CANT_CREATE_JUNCT;
	}
	vfs_ChDir(p->conn,p->conn->connectpath);

	return WERR_OK;
}

WERROR _dfs_Remove(pipes_struct *p, struct dfs_Remove *r)
{
	struct junction_map jn;
	BOOL self_ref = False;
	int consumedcnt = 0;
	BOOL found = False;

	pstring altpath;

	if (p->pipe_user.ut.uid != 0) {
		DEBUG(10,("_dfs_remove: uid != 0. Access denied.\n"));
		return WERR_ACCESS_DENIED;
	}

	if (r->in.server && r->in.share) {
		pstrcpy(altpath, r->in.server);
		pstrcat(altpath, "\\");
		pstrcat(altpath, r->in.share);
		strlower_m(altpath);
	}

	DEBUG(5,("init_reply_dfs_remove: Request to remove %s -> %s\\%s.\n",
		r->in.path, r->in.server, r->in.share));

	if(!get_referred_path(p->mem_ctx, r->in.path, &jn, &consumedcnt, &self_ref)) {
		return WERR_DFS_NO_SUCH_VOL;
	}

	/* if no server-share pair given, remove the msdfs link completely */
	if(!r->in.server && !r->in.share) {
		if(!remove_msdfs_link(&jn)) {
			vfs_ChDir(p->conn,p->conn->connectpath);
			return WERR_DFS_NO_SUCH_VOL;
		}
		vfs_ChDir(p->conn,p->conn->connectpath);
	} else {
		int i=0;
		/* compare each referral in the list with the one to remove */
		DEBUG(10,("altpath: .%s. refcnt: %d\n", altpath, jn.referral_count));
		for(i=0;i<jn.referral_count;i++) {
			pstring refpath;
			pstrcpy(refpath,jn.referral_list[i].alternate_path);
			trim_char(refpath, '\\', '\\');
			DEBUG(10,("_dfs_remove:  refpath: .%s.\n", refpath));
			if(strequal(refpath, altpath)) {
				*(jn.referral_list[i].alternate_path)='\0';
				DEBUG(10,("_dfs_remove: Removal request matches referral %s\n",
					refpath));
				found = True;
			}
		}

		if(!found) {
			return WERR_DFS_NO_SUCH_SHARE;
		}

		/* Only one referral, remove it */
		if(jn.referral_count == 1) {
			if(!remove_msdfs_link(&jn)) {
				vfs_ChDir(p->conn,p->conn->connectpath);
				return WERR_DFS_NO_SUCH_VOL;
			}
		} else {
			if(!create_msdfs_link(&jn, True)) { 
				vfs_ChDir(p->conn,p->conn->connectpath);
				return WERR_DFS_CANT_CREATE_JUNCT;
			}
		}
		vfs_ChDir(p->conn,p->conn->connectpath);
	}

	return WERR_OK;
}

static BOOL init_reply_dfs_info_1(TALLOC_CTX *mem_ctx, struct junction_map* j, struct dfs_Info1* dfs1)
{
	dfs1->path = talloc_asprintf(mem_ctx, 
				"\\\\%s\\%s\\%s", global_myname(), 
				j->service_name, j->volume_name);
	if (dfs1->path == NULL)
		return False;

	DEBUG(5,("init_reply_dfs_info_1: initing entrypath: %s\n",dfs1->path));
	return True;
}

static BOOL init_reply_dfs_info_2(TALLOC_CTX *mem_ctx, struct junction_map* j, struct dfs_Info2* dfs2)
{
	dfs2->path = talloc_asprintf(mem_ctx, 
			"\\\\%s\\%s\\%s", global_myname(), j->service_name, j->volume_name);
	if (dfs2->path == NULL)
		return False;
	dfs2->comment = talloc_strdup(mem_ctx, j->comment);
	dfs2->state = 1; /* set up state of dfs junction as OK */
	dfs2->num_stores = j->referral_count;
	return True;
}

static BOOL init_reply_dfs_info_3(TALLOC_CTX *mem_ctx, struct junction_map* j, struct dfs_Info3* dfs3)
{
	int ii;
	if (j->volume_name[0] == '\0')
		dfs3->path = talloc_asprintf(mem_ctx, "\\\\%s\\%s",
			global_myname(), j->service_name);
	else
		dfs3->path = talloc_asprintf(mem_ctx, "\\\\%s\\%s\\%s", global_myname(),
			j->service_name, j->volume_name);

	if (dfs3->path == NULL)
		return False;

	dfs3->comment = talloc_strdup(mem_ctx, j->comment);
	dfs3->state = 1;
	dfs3->num_stores = j->referral_count;
    
	/* also enumerate the stores */
	dfs3->stores = TALLOC_ARRAY(mem_ctx, struct dfs_StorageInfo, j->referral_count);
	if (!dfs3->stores)
		return False;

	memset(dfs3->stores, '\0', j->referral_count * sizeof(struct dfs_StorageInfo));

	for(ii=0;ii<j->referral_count;ii++) {
		char* p; 
		pstring path;
		struct dfs_StorageInfo* stor = &(dfs3->stores[ii]);
		struct referral* ref = &(j->referral_list[ii]);
  
		pstrcpy(path, ref->alternate_path);
		trim_char(path,'\\','\0');
		p = strrchr_m(path,'\\');
		if(p==NULL) {
			DEBUG(4,("init_reply_dfs_info_3: invalid path: no \\ found in %s\n",path));
			continue;
		}
		*p = '\0';
		DEBUG(5,("storage %d: %s.%s\n",ii,path,p+1));
		stor->state = 2; /* set all stores as ONLINE */
		stor->server = talloc_strdup(mem_ctx, path);
		stor->share = talloc_strdup(mem_ctx, p+1);
	}
	return True;
}

static BOOL init_reply_dfs_info_100(TALLOC_CTX *mem_ctx, struct junction_map* j, struct dfs_Info100* dfs100)
{
	dfs100->comment = talloc_strdup(mem_ctx, j->comment);
	return True;
}


WERROR _dfs_Enum(pipes_struct *p, struct dfs_Enum *r)
{
	struct junction_map jn[MAX_MSDFS_JUNCTIONS];
	int num_jn = 0;
	int i;

	num_jn = enum_msdfs_links(p->mem_ctx, jn, ARRAY_SIZE(jn));
	vfs_ChDir(p->conn,p->conn->connectpath);
    
	DEBUG(5,("_dfs_Enum: %d junctions found in Dfs, doing level %d\n", num_jn, r->in.level));

	*r->out.total = num_jn;

	/* Create the return array */
	switch (r->in.level) {
	case 1:
		if ((r->out.info->e.info1->s = TALLOC_ARRAY(p->mem_ctx, struct dfs_Info1, num_jn)) == NULL) {
			return WERR_NOMEM;
		}
		r->out.info->e.info1->count = num_jn;
		break;
	case 2:
		if ((r->out.info->e.info2->s = TALLOC_ARRAY(p->mem_ctx, struct dfs_Info2, num_jn)) == NULL) {
			return WERR_NOMEM;
		}
		r->out.info->e.info2->count = num_jn;
		break;
	case 3:
		if ((r->out.info->e.info3->s = TALLOC_ARRAY(p->mem_ctx, struct dfs_Info3, num_jn)) == NULL) {
			return WERR_NOMEM;
		}
		r->out.info->e.info3->count = num_jn;
		break;
	default:
		return WERR_INVALID_PARAM;
	}

	for (i = 0; i < num_jn; i++) {
		switch (r->in.level) {
		case 1: 
			init_reply_dfs_info_1(p->mem_ctx, &jn[i], &r->out.info->e.info1->s[i]);
			break;
		case 2:
			init_reply_dfs_info_2(p->mem_ctx, &jn[i], &r->out.info->e.info2->s[i]);
			break;
		case 3:
			init_reply_dfs_info_3(p->mem_ctx, &jn[i], &r->out.info->e.info3->s[i]);
			break;
		default:
			return WERR_INVALID_PARAM;
		}
	}
  
	return WERR_OK;
}
      
WERROR _dfs_GetInfo(pipes_struct *p, struct dfs_GetInfo *r)
{
	int consumedcnt = sizeof(pstring);
	struct junction_map jn;
	BOOL self_ref = False;
	BOOL ret;

	if(!create_junction(r->in.path, &jn))
		return WERR_DFS_NO_SUCH_SERVER;
  
	/* The following call can change the cwd. */
	if(!get_referred_path(p->mem_ctx, r->in.path, &jn, &consumedcnt, &self_ref) || consumedcnt < strlen(r->in.path)) {
		vfs_ChDir(p->conn,p->conn->connectpath);
		return WERR_DFS_NO_SUCH_VOL;
	}

	vfs_ChDir(p->conn,p->conn->connectpath);

	switch (r->in.level) {
		case 1: ret = init_reply_dfs_info_1(p->mem_ctx, &jn, r->out.info->info1); break;
		case 2: ret = init_reply_dfs_info_2(p->mem_ctx, &jn, r->out.info->info2); break;
		case 3: ret = init_reply_dfs_info_3(p->mem_ctx, &jn, r->out.info->info3); break;
		case 100: ret = init_reply_dfs_info_100(p->mem_ctx, &jn, r->out.info->info100); break;
		default:
			r->out.info->info1 = NULL;
			return WERR_INVALID_PARAM;
	}

	if (!ret) 
		return WERR_INVALID_PARAM;
  
	return WERR_OK;
}

WERROR _dfs_SetInfo(pipes_struct *p, struct dfs_SetInfo *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_Rename(pipes_struct *p, struct dfs_Rename *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_Move(pipes_struct *p, struct dfs_Move *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_ManagerGetConfigInfo(pipes_struct *p, struct dfs_ManagerGetConfigInfo *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_ManagerSendSiteInfo(pipes_struct *p, struct dfs_ManagerSendSiteInfo *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_AddFtRoot(pipes_struct *p, struct dfs_AddFtRoot *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_RemoveFtRoot(pipes_struct *p, struct dfs_RemoveFtRoot *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_AddStdRoot(pipes_struct *p, struct dfs_AddStdRoot *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_RemoveStdRoot(pipes_struct *p, struct dfs_RemoveStdRoot *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_ManagerInitialize(pipes_struct *p, struct dfs_ManagerInitialize *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_AddStdRootForced(pipes_struct *p, struct dfs_AddStdRootForced *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_GetDcAddress(pipes_struct *p, struct dfs_GetDcAddress *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_SetDcAddress(pipes_struct *p, struct dfs_SetDcAddress *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_FlushFtTable(pipes_struct *p, struct dfs_FlushFtTable *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_Add2(pipes_struct *p, struct dfs_Add2 *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_Remove2(pipes_struct *p, struct dfs_Remove2 *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_EnumEx(pipes_struct *p, struct dfs_EnumEx *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}

WERROR _dfs_SetInfo2(pipes_struct *p, struct dfs_SetInfo2 *r)
{
	/* FIXME: Implement your code here */
	p->rng_fault_state = True;
	return WERR_NOT_SUPPORTED;
}
