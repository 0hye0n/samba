/* 
   Unix SMB/Netbios implementation.
   Version 3.0
   MSDfs services for Samba
   Copyright (C) Shirish Kalele 2000

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "includes.h"

extern int DEBUGLEVEL;
extern pstring global_myname;

/**********************************************************************
 Create a tcon relative path from a dfs_path structure
 **********************************************************************/

static void create_nondfs_path(char* pathname, struct dfs_path* pdp)
{
	pstrcpy(pathname,pdp->volumename); 
	pstrcat(pathname,"\\"); 
	pstrcat(pathname,pdp->restofthepath); 
}

/**********************************************************************
  Parse the pathname  of the form \hostname\service\volume\restofthepath
  into the dfs_path structure 
 **********************************************************************/

static BOOL parse_dfs_path(char* pathname, struct dfs_path* pdp)
{
	pstring pathname_local;
	char* p,*temp;

	pstrcpy(pathname_local,pathname);
	p = temp = pathname_local;

	ZERO_STRUCTP(pdp);

	trim_string(temp,"\\","\\");
	DEBUG(10,("temp in parse_dfs_path: .%s. after trimming \\'s\n",temp));

	/* now tokenize */
	/* parse out hostname */
	p = strchr_m(temp,'\\');
	if(p == NULL)
		return False;
	*p = '\0';
	pstrcpy(pdp->hostname,temp);
	DEBUG(10,("hostname: %s\n",pdp->hostname));

	/* parse out servicename */
	temp = p+1;
	p = strchr_m(temp,'\\');
	if(p == NULL) {
		pstrcpy(pdp->servicename,temp);
		return True;
	}
	*p = '\0';
	pstrcpy(pdp->servicename,temp);
	DEBUG(10,("servicename: %s\n",pdp->servicename));

	/* parse out volumename */
	temp = p+1;
	p = strchr_m(temp,'\\');
	if(p == NULL) {
		pstrcpy(pdp->volumename,temp);
		return True;
	}
	*p = '\0';
	pstrcpy(pdp->volumename,temp);
	DEBUG(10,("volumename: %s\n",pdp->volumename));

	/* remaining path .. */
	pstrcpy(pdp->restofthepath,p+1);
	DEBUG(10,("rest of the path: %s\n",pdp->restofthepath));
	return True;
}

/********************************************************
 Fake up a connection struct for the VFS layer.
*********************************************************/

static BOOL create_conn_struct( connection_struct *conn, int snum, char *path)
{
	ZERO_STRUCTP(conn);
	conn->service = snum;
	conn->connectpath = path;

	if (!vfs_init(conn)) {
		DEBUG(0,("create_conn_struct: vfs init failed.\n"));
		return False;
	}
	return True;
}

/**********************************************************************
 Forms a valid Unix pathname from the junction 
 **********************************************************************/

static BOOL form_path_from_junction(struct junction_map* jn, char* path, int max_pathlen,
								connection_struct *conn)
{
	int snum;

	if(!path || !jn)
		return False;

	snum = lp_servicenumber(jn->service_name);
	if(snum < 0)
		return False;

	safe_strcpy(path, lp_pathname(snum), max_pathlen-1);
	safe_strcat(path, "/", max_pathlen-1);
	strlower(jn->volume_name);
	safe_strcat(path, jn->volume_name, max_pathlen-1);

	if (!create_conn_struct(conn, snum, path))
		return False;

	return True;
}

/**********************************************************************
 Creates a junction structure from the Dfs pathname
 **********************************************************************/

BOOL create_junction(char* pathname, struct junction_map* jn)
{
	struct dfs_path dp;
  
	parse_dfs_path(pathname,&dp);

	/* check if path is dfs : check hostname is the first token */
	if(global_myname && (strcasecmp(global_myname,dp.hostname)!=0)) {
		DEBUG(4,("create_junction: Invalid hostname %s in dfs path %s\n", dp.hostname, pathname));
		return False;
	}

	/* Check for a non-DFS share */
	if(!lp_msdfs_root(lp_servicenumber(dp.servicename))) {
		DEBUG(4,("create_junction: %s is not an msdfs root.\n", dp.servicename));
		return False;
	}

	pstrcpy(jn->service_name,dp.servicename);
	pstrcpy(jn->volume_name,dp.volumename);
	return True;
}

/**********************************************************************
 Parse the contents of a symlink to verify if it is an msdfs referral
 A valid referral is of the form: msdfs:server1\share1,server2\share2
 **********************************************************************/

static BOOL parse_symlink(char* buf,struct referral** preflist, int* refcount)
{
	pstring temp;
	char* prot;
	char* alt_path[MAX_REFERRAL_COUNT];
	int count=0, i;
	struct referral* reflist;

	pstrcpy(temp,buf);
  
	prot = strtok(temp,":");

	if(!strequal(prot, "msdfs"))
		return False;

	/* It's an msdfs referral */
	if(!preflist) 
		return True;

	/* parse out the alternate paths */
	while(((alt_path[count] = strtok(NULL,",")) != NULL) && count<MAX_REFERRAL_COUNT)
		count++;

	DEBUG(10,("parse_symlink: count=%d\n", count));

	reflist = *preflist = (struct referral*) malloc(count * sizeof(struct referral));
	if(reflist == NULL) {
		DEBUG(0,("parse_symlink: Malloc failed!\n"));
		return False;
	}
  
	for(i=0;i<count;i++) {
		/* replace / in the alternate path by a \ */
		char* p = strchr_m(alt_path[i],'/');
		if(p)
			*p = '\\'; 

		pstrcpy(reflist[i].alternate_path, "\\");
		pstrcat(reflist[i].alternate_path, alt_path[i]);
		reflist[i].proximity = 0;
		reflist[i].ttl = REFERRAL_TTL;
		DEBUG(10, ("parse_symlink: Created alt path: %s\n", reflist[i].alternate_path));
	}

	if(refcount)
		*refcount = count;

	return True;
}
 
/**********************************************************************
 Returns true if the unix path is a valid msdfs symlink
 **********************************************************************/

BOOL is_msdfs_link(connection_struct* conn, char* path)
{
	SMB_STRUCT_STAT st;
	pstring referral;
	int referral_len = 0;

	if(!path || !conn)
		return False;

	strlower(path);

	if(conn->vfs_ops.lstat(conn,path,&st) != 0) {
		DEBUG(5,("is_msdfs_link: %s does not exist.\n",path));
		return False;
	}
  
	if(S_ISLNK(st.st_mode)) {
		/* open the link and read it */
		referral_len = conn->vfs_ops.readlink(conn, path, referral, sizeof(pstring));
		if(referral_len == -1)
			DEBUG(0,("is_msdfs_link: Error reading msdfs link %s: %s\n", path, strerror(errno)));

		referral[referral_len] = '\0';
		DEBUG(5,("is_msdfs_link: %s -> %s\n",path,referral));
		if(parse_symlink(referral, NULL, NULL))
			return True;
	}
	return False;
}

/**********************************************************************
 Fills in the junction_map struct with the referrals from the 
 symbolic link
 **********************************************************************/

BOOL get_referred_path(struct junction_map* junction)
{
	pstring path;
	pstring buf;
	SMB_STRUCT_STAT st;
	connection_struct conns;
 	connection_struct *conn = &conns;
 
	if(!form_path_from_junction(junction, path, sizeof(path), conn))
		return False;

	DEBUG(5,("get_referred_path: lstat target: %s\n", path));
  
	if(conn->vfs_ops.lstat(conn,path,&st) != 0) {
		DEBUG(5,("get_referred_path: %s does not exist.\n",path));
		return False;
	}
  
	if(S_ISLNK(st.st_mode)) {
		/* open the link and read it to get the dfs referral */
		int linkcnt = 0;
		linkcnt = conn->vfs_ops.readlink(conn, path, buf, sizeof(buf));
		buf[linkcnt] = '\0';
		DEBUG(5,("get_referred_path: Referral: %s\n",buf));
		if(parse_symlink(buf, &junction->referral_list, &junction->referral_count))
			return True;
	}
	return False;
}

/**************************************************************
Decides if given pathname is Dfs and if it should be redirected
Converts pathname to non-dfs format if Dfs redirection not required 
**************************************************************/

BOOL dfs_redirect(char* pathname, connection_struct* conn)
{
	struct dfs_path dp;
	pstring temp;
	fstring path;

	pstrcpy(temp,pathname);

	if(!lp_msdfs_root(SNUM(conn)) )
		return False;

	parse_dfs_path(pathname,&dp);

	if(global_myname && (strcasecmp(global_myname,dp.hostname)!=0))
		return False;

	/* check if need to redirect */
	fstrcpy(path, conn->connectpath);
	fstrcat(path, "/");
	fstrcat(path, dp.volumename);
	if(is_msdfs_link(conn, path)) {
		DEBUG(4,("dfs_redirect: Redirecting %s\n",temp));
		return True;
	} else {
		create_nondfs_path(pathname,&dp);
		DEBUG(4,("dfs_redirect: Not redirecting %s. Converted to non-dfs pathname \'%s\'\n",
				temp,pathname));
		return False;
	}
}

/*
  Special DFS redirect call for findfirst's. 
  If the findfirst is for the dfs junction, then no redirection,
  if it is for the underlying directory contents, redirect.
  */

BOOL dfs_findfirst_redirect(char* pathname, connection_struct* conn)
{
	struct dfs_path dp;
  
	pstring temp;

	pstrcpy(temp,pathname);

	/* Is the path Dfs-redirectable? */
	if(!dfs_redirect(temp,conn)) {
		pstrcpy(pathname,temp);
		return False;
	}

	parse_dfs_path(pathname,&dp);
	DEBUG(8,("dfs_findfirst_redirect: path %s is in Dfs. dp.restofthepath=.%s.\n",
				pathname,dp.restofthepath));
	if(!(*(dp.restofthepath))) {
		create_nondfs_path(pathname,&dp);
		return False;
	}

	return True;
}

static int setup_ver2_dfs_referral(char* pathname, char** ppdata, 
				   struct junction_map* junction,
				   BOOL self_referral)
{
	char* pdata = *ppdata;

	unsigned char uni_requestedpath[1024];
	int uni_reqpathoffset1,uni_reqpathoffset2;
	int uni_curroffset;
	int requestedpathlen=0;
	int offset;
	int reply_size = 0;
	int i=0;

	DEBUG(10,("setting up version2 referral\nRequested path:\n"));

        requestedpathlen = rpcstr_push(uni_requestedpath, pathname, -1,
                                       STR_TERMINATE);

	dump_data(10,uni_requestedpath,requestedpathlen);

	DEBUG(10,("ref count = %u\n",junction->referral_count));

	uni_reqpathoffset1 = REFERRAL_HEADER_SIZE + 
			VERSION2_REFERRAL_SIZE * junction->referral_count;

	uni_reqpathoffset2 = uni_reqpathoffset1 + requestedpathlen;

	uni_curroffset = uni_reqpathoffset2 + requestedpathlen;

	reply_size = REFERRAL_HEADER_SIZE + VERSION2_REFERRAL_SIZE*junction->referral_count +
					2 * requestedpathlen;
	DEBUG(10,("reply_size: %u\n",reply_size));

	/* add up the unicode lengths of all the referral paths */
	for(i=0;i<junction->referral_count;i++) {
		DEBUG(10,("referral %u : %s\n",i,junction->referral_list[i].alternate_path));
		reply_size += (strlen(junction->referral_list[i].alternate_path)+1)*2;
	}

	DEBUG(10,("reply_size = %u\n",reply_size));
	/* add the unexplained 0x16 bytes */
	reply_size += 0x16;

	pdata = Realloc(pdata,reply_size);
	if(pdata == NULL) {
		DEBUG(0,("malloc failed for Realloc!\n"));
		return -1;
	}
	else *ppdata = pdata;

	/* copy in the dfs requested paths.. required for offset calculations */
	memcpy(pdata+uni_reqpathoffset1,uni_requestedpath,requestedpathlen);
	memcpy(pdata+uni_reqpathoffset2,uni_requestedpath,requestedpathlen);

	/* create the header */
	SSVAL(pdata,0,requestedpathlen-2); /* path consumed */
	SSVAL(pdata,2,junction->referral_count); /* number of referral in this pkt */
	if(self_referral)
		SIVAL(pdata,4,DFSREF_REFERRAL_SERVER | DFSREF_STORAGE_SERVER); 
	else
		SIVAL(pdata,4,DFSREF_STORAGE_SERVER);

	offset = 8;
	/* add the referral elements */
	for(i=0;i<junction->referral_count;i++) {
		struct referral* ref = &(junction->referral_list[i]);
		int unilen;

		SSVAL(pdata,offset,2); /* version 2 */
		SSVAL(pdata,offset+2,VERSION2_REFERRAL_SIZE);
		if(self_referral)
			SSVAL(pdata,offset+4,1);
		else
			SSVAL(pdata,offset+4,0);
		SSVAL(pdata,offset+6,0); /* ref_flags :use path_consumed bytes? */
		SIVAL(pdata,offset+8,ref->proximity);
		SIVAL(pdata,offset+12,ref->ttl);

		SSVAL(pdata,offset+16,uni_reqpathoffset1-offset);
		SSVAL(pdata,offset+18,uni_reqpathoffset2-offset);
		/* copy referred path into current offset */
                unilen = rpcstr_push(pdata+uni_curroffset, ref->alternate_path,
                                     -1, STR_UNICODE);
		SSVAL(pdata,offset+20,uni_curroffset-offset);

		uni_curroffset += unilen;
		offset += VERSION2_REFERRAL_SIZE;
	}
	/* add in the unexplained 22 (0x16) bytes at the end */
	memset(pdata+uni_curroffset,'\0',0x16);
	free(junction->referral_list);
	return reply_size;
}

static int setup_ver3_dfs_referral(char* pathname, char** ppdata, 
				   struct junction_map* junction,
				   BOOL self_referral)
{
	char* pdata = *ppdata;

	unsigned char uni_reqpath[1024];
	int uni_reqpathoffset1, uni_reqpathoffset2;
	int uni_curroffset;
	int reply_size = 0;

	int reqpathlen = 0;
	int offset,i=0;
	
	DEBUG(10,("setting up version3 referral\n"));

        reqpathlen = rpcstr_push(uni_reqpath, pathname, -1, STR_TERMINATE);

	dump_data(10,uni_reqpath,reqpathlen);

	uni_reqpathoffset1 = REFERRAL_HEADER_SIZE + VERSION3_REFERRAL_SIZE * junction->referral_count;
	uni_reqpathoffset2 = uni_reqpathoffset1 + reqpathlen;
	reply_size = uni_curroffset = uni_reqpathoffset2 + reqpathlen;

	for(i=0;i<junction->referral_count;i++) {
		DEBUG(10,("referral %u : %s\n",i,junction->referral_list[i].alternate_path));
		reply_size += (strlen(junction->referral_list[i].alternate_path)+1)*2;
	}

	pdata = Realloc(pdata,reply_size);
	if(pdata == NULL) {
		DEBUG(0,("version3 referral setup: malloc failed for Realloc!\n"));
		return -1;
	}
	else *ppdata = pdata;
	
	/* create the header */
	SSVAL(pdata,0,reqpathlen-2); /* path consumed */
	SSVAL(pdata,2,junction->referral_count); /* number of referral in this pkt */
	if(self_referral)
		SIVAL(pdata,4,DFSREF_REFERRAL_SERVER | DFSREF_STORAGE_SERVER); 
	else
		SIVAL(pdata,4,DFSREF_STORAGE_SERVER);
	
	/* copy in the reqpaths */
	memcpy(pdata+uni_reqpathoffset1,uni_reqpath,reqpathlen);
	memcpy(pdata+uni_reqpathoffset2,uni_reqpath,reqpathlen);
	
	offset = 8;
	for(i=0;i<junction->referral_count;i++) {
		struct referral* ref = &(junction->referral_list[i]);
		int unilen;

		SSVAL(pdata,offset,3); /* version 3 */
		SSVAL(pdata,offset+2,VERSION3_REFERRAL_SIZE);
		if(self_referral)
			SSVAL(pdata,offset+4,1);
		else
			SSVAL(pdata,offset+4,0);

		SSVAL(pdata,offset+6,0); /* ref_flags :use path_consumed bytes? */
		SIVAL(pdata,offset+8,ref->ttl);
	    
		SSVAL(pdata,offset+12,uni_reqpathoffset1-offset);
		SSVAL(pdata,offset+14,uni_reqpathoffset2-offset);
		/* copy referred path into current offset */

                unilen = rpcstr_push(pdata+uni_curroffset, ref->alternate_path,
                                     -1, STR_UNICODE|STR_TERMINATE);
		SSVAL(pdata,offset+16,uni_curroffset-offset);
		/* copy 0x10 bytes of 00's in the ServiceSite GUID */
		memset(pdata+offset+18,'\0',16);

		uni_curroffset += unilen;
		offset += VERSION3_REFERRAL_SIZE;
	}
	free(junction->referral_list);
	return reply_size;
}

/******************************************************************
 * Set up the Dfs referral for the dfs pathname
 ******************************************************************/

int setup_dfs_referral(char* pathname, int max_referral_level, char** ppdata)
{
	struct junction_map junction;

	BOOL self_referral;

	int reply_size = 0;

	ZERO_STRUCT(junction);

	if(!create_junction(pathname, &junction))
		return -1;

	/* get the junction entry */
	if(!get_referred_path(&junction)) {
    
		/* refer the same pathname, create a standard referral struct */
		struct referral* ref;
		self_referral = True;
		junction.referral_count = 1;
		if((ref = (struct referral*) malloc(sizeof(struct referral))) == NULL) {
			DEBUG(0,("malloc failed for referral\n"));
			return -1;
		}
      
		pstrcpy(ref->alternate_path,pathname);
		ref->proximity = 0;
		ref->ttl = REFERRAL_TTL;
		junction.referral_list = ref;
	} else {
		self_referral = False;
		if( DEBUGLVL( 3 ) ) {
			int i=0;
			dbgtext("setup_dfs_referral: Path %s to alternate path(s):",pathname);
			for(i=0;i<junction.referral_count;i++)
				dbgtext(" %s",junction.referral_list[i].alternate_path);
			dbgtext(".\n");
		}
	}
      
	/* create the referral depeding on version */
	DEBUG(10,("max_referral_level :%d\n",max_referral_level));
	if(max_referral_level<2 || max_referral_level>3)
		max_referral_level = 2;

	switch(max_referral_level) {
	case 2:
		{
		reply_size = setup_ver2_dfs_referral(pathname, ppdata, &junction, self_referral);
		break;
		}
	case 3:
		{
		reply_size = setup_ver3_dfs_referral(pathname, ppdata, &junction, self_referral);
		break;
		}
	default:
		{
		DEBUG(0,("setup_dfs_referral: Invalid dfs referral version: %d\n", max_referral_level));
		return -1;
		}
	}
      
	DEBUG(10,("DFS Referral pdata:\n"));
	dump_data(10,*ppdata,reply_size);
	return reply_size;
}

/**********************************************************************
 The following functions are called by the NETDFS RPC pipe functions
 **********************************************************************/

BOOL create_msdfs_link(struct junction_map* jn, BOOL exists)
{
	pstring path;
	pstring msdfs_link;
	connection_struct conns;
 	connection_struct *conn = &conns;
	int i=0;

	if(!form_path_from_junction(jn, path, sizeof(path), conn))
		return False;
  
	/* form the msdfs_link contents */
	pstrcpy(msdfs_link, "msdfs:");
	for(i=0; i<jn->referral_count; i++) {
		char* refpath = jn->referral_list[i].alternate_path;
      
		trim_string(refpath, "\\", "\\");
		if(*refpath == '\0')
			continue;
      
		if(i>0)
			pstrcat(msdfs_link, ",");
      
		pstrcat(msdfs_link, refpath);
	}

	DEBUG(5,("create_msdfs_link: Creating new msdfs link: %s -> %s\n", path, msdfs_link));

	if(exists)
		if(conn->vfs_ops.unlink(conn,path)!=0)
			return False;

	if(conn->vfs_ops.symlink(conn, msdfs_link, path) < 0) {
		DEBUG(1,("create_msdfs_link: symlink failed %s -> %s\nError: %s\n", 
				path, msdfs_link, strerror(errno)));
		return False;
	}
	return True;
}

BOOL remove_msdfs_link(struct junction_map* jn)
{
	pstring path;
	connection_struct conns;
 	connection_struct *conn = &conns;

	if(!form_path_from_junction(jn, path, sizeof(path), conn))
		return False;
     
	if(conn->vfs_ops.unlink(conn, path)!=0)
		return False;
  
	return True;
}

static BOOL form_junctions(int snum, struct junction_map* jn, int* jn_count)
{
	int cnt = *jn_count;
	DIR *dirp;
	char* dname;
	pstring connect_path;
	char* service_name = lp_servicename(snum);
	connection_struct conns;
	connection_struct *conn = &conns;
 
	pstrcpy(connect_path,lp_pathname(snum));

	if(*connect_path == '\0')
		return False;

	/*
	 * Fake up a connection struct for the VFS layer.
	 */

	if (!create_conn_struct(conn, snum, connect_path))
		return False;

	/* form a junction for the msdfs root - convention */ 
	/*
		pstrpcy(jn[cnt].service_name, service_name);
		jn[cnt].volume_name[0] = '\0';
		jn[cnt].referral_count = 1;
  
		slprintf(alt_path,sizeof(alt_path)-1"\\\\%s\\%s", global_myname, service_name);
		jn[cnt].referral_l
	*/

	dirp = conn->vfs_ops.opendir(conn, connect_path);
	if(!dirp)
		return False;

	while((dname = vfs_readdirname(conn, dirp)) != NULL) {
		SMB_STRUCT_STAT st;
		pstring pathreal;
		fstring buf;
		int buflen = 0;
		pstrcpy(pathreal, connect_path);
		pstrcat(pathreal, "/");
		pstrcat(pathreal, dname);
 
		if(conn->vfs_ops.lstat(conn,pathreal,&st) != 0) {
			DEBUG(4,("lstat error for %s: %s\n",pathreal, strerror(errno)));
			continue;
		}
		if(S_ISLNK(st.st_mode)) {
			buflen = conn->vfs_ops.readlink(conn, pathreal, buf, sizeof(fstring));
			buf[buflen] = '\0';
			if(parse_symlink(buf, &(jn[cnt].referral_list), &(jn[cnt].referral_count))) {
				pstrcpy(jn[cnt].service_name, service_name);
				pstrcpy(jn[cnt].volume_name, dname);
				cnt++;
			}
		}
	}
  
	conn->vfs_ops.closedir(conn,dirp);
	*jn_count = cnt;
	return True;
}

int enum_msdfs_links(struct junction_map* jn)
{
	int i=0;
	int jn_count = 0;

	if(!lp_host_msdfs())
		return -1;

	for(i=0;*lp_servicename(i);i++) {
		if(lp_msdfs_root(i)) 
			form_junctions(i,jn,&jn_count);
	}
	return jn_count;
}


