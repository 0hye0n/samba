/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   Pipe SMB reply routines
   Copyright (C) Andrew Tridgell 1992-1997,
   Copyright (C) Luke Kenneth Casson Leighton 1996-1997.
   Copyright (C) Paul Ashton  1997.
   
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
/*
   This file handles reply_ calls on named pipes that the server
   makes to handle specific protocols
*/


#include "includes.h"
#include "trans2.h"
#include "nterr.h"

#define	PIPE		"\\PIPE\\"
#define	PIPELEN		strlen(PIPE)

#define REALLOC(ptr,size) Realloc(ptr,MAX((size),4*1024))

/* look in server.c for some explanation of these variables */
extern int Protocol;
extern int DEBUGLEVEL;
extern int chain_fnum;
extern char magic_char;
extern connection_struct Connections[];
extern files_struct Files[];
extern BOOL case_sensitive;
extern pstring sesssetup_user;
extern int Client;
extern fstring myworkgroup;

/* this macro should always be used to extract an fnum (smb_fid) from
a packet to ensure chaining works correctly */
#define GETFNUM(buf,where) (chain_fnum!= -1?chain_fnum:SVAL(buf,where))

char * known_pipes [] =
{
  "lsarpc",
#if NTDOMAIN
  "NETLOGON",
#endif
  NULL
};

/****************************************************************************
  reply to an open and X on a named pipe

  In fact what we do is to open a regular file with the same name in
  /tmp. This can then be closed as normal. Reading and writing won't
  make much sense, but will do *something*. The real reason for this
  support is to be able to do transactions on them (well, on lsarpc
  for domain login purposes...).

  This code is basically stolen from reply_open_and_X with some
  wrinkles to handle pipes.
****************************************************************************/
int reply_open_pipe_and_X(char *inbuf,char *outbuf,int length,int bufsize)
{
  pstring fname;
  int cnum = SVAL(inbuf,smb_tid);
  int fnum = -1;
  int smb_mode = SVAL(inbuf,smb_vwv3);
  int smb_attr = SVAL(inbuf,smb_vwv5);
#if 0
  int open_flags = SVAL(inbuf,smb_vwv2);
  int smb_sattr = SVAL(inbuf,smb_vwv4); 
  uint32 smb_time = make_unix_date3(inbuf+smb_vwv6);
#endif
  int smb_ofun = SVAL(inbuf,smb_vwv8);
  int unixmode;
  int size=0,fmode=0,mtime=0,rmode=0;
  struct stat sbuf;
  int smb_action = 0;
  int i;
  BOOL bad_path = False;

  /* XXXX we need to handle passed times, sattr and flags */
  pstrcpy(fname,smb_buf(inbuf));

  /* If the name doesn't start \PIPE\ then this is directed */
  /* at a mailslot or something we really, really don't understand, */
  /* not just something we really don't understand. */
  if ( strncmp(fname,PIPE,PIPELEN) != 0 )
    return(ERROR(ERRSRV,ERRaccess));

  DEBUG(4,("Opening pipe %s.\n", fname));

  /* Strip \PIPE\ off the name. */
  pstrcpy(fname,smb_buf(inbuf) + PIPELEN);

  /* See if it is one we want to handle. */
  for( i = 0; known_pipes[i] ; i++ )
    if( strcmp(fname,known_pipes[i]) == 0 )
      break;

  if ( known_pipes[i] == NULL )
    return(ERROR(ERRSRV,ERRaccess));

  /* Known pipes arrive with DIR attribs. Remove it so a regular file */
  /* can be opened and add it in after the open. */
  DEBUG(3,("Known pipe %s opening.\n",fname));
  smb_attr &= ~aDIR;
  Connections[cnum].read_only = 0;
  smb_ofun |= 0x10;		/* Add Create it not exists flag */

  unix_convert(fname,cnum,0,&bad_path);
    
  fnum = find_free_file();
  if (fnum < 0)
    return(ERROR(ERRSRV,ERRnofids));

  if (!check_name(fname,cnum))
    return(UNIXERROR(ERRDOS,ERRnoaccess));

  unixmode = unix_mode(cnum,smb_attr);
      
  open_file_shared(fnum,cnum,fname,smb_mode,smb_ofun,unixmode,
		   0, &rmode,&smb_action);
      
  if (!Files[fnum].open)
  {
    /* Change the error code if bad_path was set. */
    if((errno == ENOENT) && bad_path)
    {
      unix_ERR_class = ERRDOS;
      unix_ERR_code = ERRbadpath;
    }
    return(UNIXERROR(ERRDOS,ERRnoaccess));
  }

  if (fstat(Files[fnum].fd_ptr->fd,&sbuf) != 0) {
    close_file(fnum);
    return(ERROR(ERRDOS,ERRnoaccess));
  }

  size = sbuf.st_size;
  fmode = dos_mode(cnum,fname,&sbuf);
  mtime = sbuf.st_mtime;
  if (fmode & aDIR) {
    close_file(fnum);
    return(ERROR(ERRDOS,ERRnoaccess));
  }

  /* Prepare the reply */
  set_message(outbuf,15,0,True);

  /* Put things back the way they were. */
  Connections[cnum].read_only = 1;

  /* Mark the opened file as an existing named pipe in message mode. */
  SSVAL(outbuf,smb_vwv9,2);
  SSVAL(outbuf,smb_vwv10,0xc700);
  if (rmode == 2)
  {
    DEBUG(4,("Resetting open result to open from create.\n"));
    rmode = 1;
  }

  SSVAL(outbuf,smb_vwv2,fnum);
  SSVAL(outbuf,smb_vwv3,fmode);
  put_dos_date3(outbuf,smb_vwv4,mtime);
  SIVAL(outbuf,smb_vwv6,size);
  SSVAL(outbuf,smb_vwv8,rmode);
  SSVAL(outbuf,smb_vwv11,smb_action);

  chain_fnum = fnum;

  DEBUG(4,("Opened pipe %s with handle %d, saved name %s.\n",
	   fname, fnum, Files[fnum].name));
  
  return chain_reply(inbuf,outbuf,length,bufsize);
}


/****************************************************************************
 api_LsarpcSNPHS

 SetNamedPipeHandleState on \PIPE\lsarpc. We can't really do much here,
 so just blithely return True. This is really only for NT domain stuff,
 we we're only handling that - don't assume Samba now does complete
 named pipe handling.
****************************************************************************/
BOOL api_LsarpcSNPHS(int cnum,int uid, char *param,char *data,
		     int mdrcnt,int mprcnt,
		     char **rdata,char **rparam,
		     int *rdata_len,int *rparam_len)
{
  uint16 id;

  id = param[0] + (param[1] << 8);
  DEBUG(4,("lsarpc SetNamedPipeHandleState to code %x\n",id));
  return(True);
}


/****************************************************************************
 api_LsarpcTNP

 TransactNamedPipe on \PIPE\lsarpc.
****************************************************************************/
static void LsarpcTNP1(char *data,char **rdata, int *rdata_len)
{
  uint32 dword1, dword2;
  char pname[] = "\\PIPE\\lsass";

  /* All kinds of mysterious numbers here */
  *rdata_len = 68;
  *rdata = REALLOC(*rdata,*rdata_len);

  dword1 = IVAL(data,0xC);
  dword2 = IVAL(data,0x10);

  SIVAL(*rdata,0,0xc0005);
  SIVAL(*rdata,4,0x10);
  SIVAL(*rdata,8,0x44);
  SIVAL(*rdata,0xC,dword1);
  
  SIVAL(*rdata,0x10,dword2);
  SIVAL(*rdata,0x14,0x15);
  SSVAL(*rdata,0x18,sizeof(pname));
  strcpy(*rdata + 0x1a,pname);
  SIVAL(*rdata,0x28,1);
  memcpy(*rdata + 0x30, data + 0x34, 0x14);
}

static void LsarpcTNP2(char *data,char **rdata, int *rdata_len)
{
  uint32 dword1;

  /* All kinds of mysterious numbers here */
  *rdata_len = 48;
  *rdata = REALLOC(*rdata,*rdata_len);

  dword1 = IVAL(data,0xC);

  SIVAL(*rdata,0,0x03020005);
  SIVAL(*rdata,4,0x10);
  SIVAL(*rdata,8,0x30);
  SIVAL(*rdata,0xC,dword1);
  SIVAL(*rdata,0x10,0x18);
  SIVAL(*rdata,0x1c,0x44332211);
  SIVAL(*rdata,0x20,0x88776655);
  SIVAL(*rdata,0x24,0xCCBBAA99);
  SIVAL(*rdata,0x28,0x11FFEEDD);
}

static void LsarpcTNP3(char *data,char **rdata, int *rdata_len)
{
  uint32 dword1;
  uint16 word1;
  char * workgroup = myworkgroup;
  int wglen = strlen(workgroup);
  int i;

  /* All kinds of mysterious numbers here */
  *rdata_len = 90 + 2 * wglen;
  *rdata = REALLOC(*rdata,*rdata_len);

  dword1 = IVAL(data,0xC);
  word1 = SVAL(data,0x2C);

  SIVAL(*rdata,0,0x03020005);
  SIVAL(*rdata,4,0x10);
  SIVAL(*rdata,8,0x60);
  SIVAL(*rdata,0xC,dword1);
  SIVAL(*rdata,0x10,0x48);
  SSVAL(*rdata,0x18,0x5988);	/* This changes */
  SSVAL(*rdata,0x1A,0x15);
  SSVAL(*rdata,0x1C,word1);
  SSVAL(*rdata,0x20,6);
  SSVAL(*rdata,0x22,8);
  SSVAL(*rdata,0x24,0x8E8);	/* So does this */
  SSVAL(*rdata,0x26,0x15);
  SSVAL(*rdata,0x28,0x4D48);	/* And this */
  SSVAL(*rdata,0x2A,0x15);
  SIVAL(*rdata,0x2C,4);
  SIVAL(*rdata,0x34,wglen);
  for ( i = 0 ; i < wglen ; i++ )
    (*rdata)[0x38 + i * 2] = workgroup[i];
   
  /* Now fill in the rest */
  i = 0x38 + wglen * 2;
  SSVAL(*rdata,i,0x648);
  SIVAL(*rdata,i+2,4);
  SIVAL(*rdata,i+6,0x401);
  SSVAL(*rdata,i+0xC,0x500);
  SIVAL(*rdata,i+0xE,0x15);
  SIVAL(*rdata,i+0x12,0x2372FE1);
  SIVAL(*rdata,i+0x16,0x7E831BEF);
  SIVAL(*rdata,i+0x1A,0x4B454B2);
}

static void LsarpcTNP4(char *data,char **rdata, int *rdata_len)
{
  uint32 dword1;

  /* All kinds of mysterious numbers here */
  *rdata_len = 48;
  *rdata = REALLOC(*rdata,*rdata_len);

  dword1 = IVAL(data,0xC);

  SIVAL(*rdata,0,0x03020005);
  SIVAL(*rdata,4,0x10);
  SIVAL(*rdata,8,0x30);
  SIVAL(*rdata,0xC,dword1);
  SIVAL(*rdata,0x10,0x18);
}


BOOL api_LsarpcTNP(int cnum,int uid, char *param,char *data,
		     int mdrcnt,int mprcnt,
		     char **rdata,char **rparam,
		     int *rdata_len,int *rparam_len)
{
  uint32 id,id2;

  id = IVAL(data,0);

  DEBUG(4,("lsarpc TransactNamedPipe id %lx\n",id));
  switch (id)
  {
    case 0xb0005:
      LsarpcTNP1(data,rdata,rdata_len);
      break;

    case 0x03000005:
      id2 = IVAL(data,8);
      DEBUG(4,("\t- Suboperation %lx\n",id2));
      switch (id2 & 0xF)
      {
        case 8:
          LsarpcTNP2(data,rdata,rdata_len);
          break;

        case 0xC:
          LsarpcTNP4(data,rdata,rdata_len);
          break;

        case 0xE:
          LsarpcTNP3(data,rdata,rdata_len);
          break;
      }
      break;
  }
  return(True);
}


#ifdef NTDOMAIN
/*
   PAXX: Someone fix above.
   The above API is indexing RPC calls based on RPC flags and 
   fragment length. I've decided to do it based on operation number :-)
*/

/* this function is due to be replaced */
static void initrpcreply(char *inbuf, char *q)
{
	uint32 callid;

	SCVAL(q, 0, 5); q++; /* RPC version 5 */
	SCVAL(q, 0, 0); q++; /* minor version 0 */
	SCVAL(q, 0, 2); q++; /* RPC response packet */
	SCVAL(q, 0, 3); q++; /* first frag + last frag */
	RSIVAL(q, 0, 0x10000000); q += 4; /* packed data representation */
	RSSVAL(q, 0, 0); q += 2; /* fragment length, fill in later */
	SSVAL(q, 0, 0); q += 2; /* authentication length */
	callid = RIVAL(inbuf, 12);
	RSIVAL(q, 0, callid); q += 4; /* call identifier - match incoming RPC */
	SIVAL(q, 0, 0x18); q += 4; /* allocation hint (no idea) */
	SSVAL(q, 0, 0); q += 2; /* presentation context identifier */
	SCVAL(q, 0, 0); q++; /* cancel count */
	SCVAL(q, 0, 0); q++; /* reserved */
}

/* this function is due to be replaced */
static void endrpcreply(char *inbuf, char *q, int datalen, int rtnval, int *rlen)
{
	SSVAL(q, 8, datalen + 4);
	SIVAL(q,0x10,datalen+4-0x18); /* allocation hint */
	SIVAL(q, datalen, rtnval);
	*rlen = datalen + 4;
	{ int fd; fd = open("/tmp/rpc", O_RDWR); write(fd, q, datalen + 4); }
}

/* RID username mapping function.  just for fun, it maps to the unix uid */
static uint32 name_to_rid(char *user_name)
{
    struct passwd *pw = Get_Pwnam(user_name, False);
    if (!pw)
	{
      DEBUG(1,("Username %s is invalid on this system\n", user_name));
      return (uint32)(-1);
    }

    return (uint32)(pw->pw_uid);
}


/* BIG NOTE: this function only does SIDS where the identauth is not >= 2^32 */
char *dom_sid_to_string(DOM_SID *sid)
{
  static pstring sidstr;
  char subauth[16];
  int i;
  uint32 ia = (sid->id_auth[0]) +
              (sid->id_auth[1] << 8 ) +
              (sid->id_auth[2] << 16) +
              (sid->id_auth[3] << 24);

  sprintf(sidstr, "S-%d-%d", sid->sid_no, ia);

  for (i = 0; i < sid->num_auths; i++)
  {
    sprintf(subauth, "-%d", sid->sub_auths[i]);
    strcat(sidstr, subauth);
  }

  DEBUG(5,("dom_sid_to_string returning %s\n", sidstr));
  return sidstr;
}

/* BIG NOTE: this function only does SIDS where the identauth is not >= 2^32 */
/* identauth >= 2^32 can be detected because it will be specified in hex */
static void make_dom_sid(DOM_SID *sid, char *domsid)
{
	int identauth;
	char *p;

	DEBUG(4,("netlogon domain SID: %s\n", domsid));

	/* assume, but should check, that domsid starts "S-" */
	p = strtok(domsid+2,"-");
	sid->sid_no = atoi(p);

	/* identauth in decimal should be <  2^32 */
	/* identauth in hex     should be >= 2^32 */
	identauth = atoi(strtok(0,"-"));

	DEBUG(4,("netlogon rev %d\n", sid->sid_no));
	DEBUG(4,("netlogon %s ia %d\n", p, identauth));

	sid->id_auth[0] = 0;
	sid->id_auth[1] = 0;
	sid->id_auth[2] = (identauth & 0xff000000) >> 24;
	sid->id_auth[3] = (identauth & 0x00ff0000) >> 16;
	sid->id_auth[4] = (identauth & 0x0000ff00) >> 8;
	sid->id_auth[5] = (identauth & 0x000000ff);

	sid->num_auths = 0;

	while ((p = strtok(0, "-")) != NULL)
	{
		sid->sub_auths[sid->num_auths++] = atoi(p);
	}
}

static void create_rpc_reply(RPC_HDR *hdr, uint32 call_id, int data_len)
{
	if (hdr == NULL) return;

	hdr->major        = 5;               /* RPC version 5 */
	hdr->minor        = 0;               /* minor version 0 */
	hdr->pkt_type     = 2;               /* RPC response packet */
	hdr->frag         = 3;               /* first frag + last frag */
	hdr->pack_type    = 1;               /* packed data representation */
	hdr->frag_len     = data_len;        /* fragment length, fill in later */
	hdr->auth_len     = 0;               /* authentication length */
	hdr->call_id      = call_id;         /* call identifier - match incoming RPC */
	hdr->alloc_hint   = data_len - 0x18; /* allocation hint (no idea) */
	hdr->context_id   = 0;               /* presentation context identifier */
	hdr->cancel_count = 0;               /* cancel count */
	hdr->reserved     = 0;               /* reserved */
}

static void make_rpc_reply(char *inbuf, char *q, int data_len)
{
	uint32 callid = RIVAL(inbuf, 12);
	RPC_HDR hdr;

	create_rpc_reply(&hdr, callid, data_len);
	smb_io_rpc_hdr(False, &hdr, q, q, 4);
}

static int lsa_reply_open_policy(char *q, char *base)
{
	char *start = q;
	LSA_R_OPEN_POL r_o;

	/* set up the LSA QUERY INFO response */
	bzero(&(r_o.pol.data), POL_HND_SIZE);
	r_o.status = 0x0;

	/* store the response in the SMB stream */
	q = lsa_io_r_open_pol(False, &r_o, q, base, 4);

	/* return length of SMB data stored */
	return q - start; 
}

static void make_uni_hdr(UNIHDR *hdr, int max_len, int len, uint16 terminate)
{
	hdr->uni_max_len = max_len;
	hdr->uni_str_len = len;
	hdr->undoc       = terminate;
}

static void make_uni_hdr2(UNIHDR2 *hdr, int max_len, int len, uint16 terminate)
{
	make_uni_hdr(&(hdr->unihdr), max_len, len, terminate);
	hdr->undoc_buffer = len > 0 ? 1 : 0;
}

static void make_unistr(UNISTR *str, char *buf)
{
	/* store the string (null-terminated copy) */
	PutUniCode((char *)(str->buffer), buf);
}

static void make_unistr2(UNISTR2 *str, char *buf, int len, char terminate)
{
	/* set up string lengths. add one if string is not null-terminated */
	str->uni_max_len = len + (terminate != 0 ? 1 : 0);
	str->undoc       = 0;
	str->uni_str_len = len;

	/* store the string (null-terminated copy) */
	PutUniCode((char *)str->buffer, buf);

	/* overwrite the last character: some strings are terminated with 4 not 0 */
	str->buffer[len] = (uint16)terminate;
}

static void make_dom_rid2(DOM_RID2 *rid2, uint32 rid)
{
	rid2->type    = 0x5;
	rid2->undoc   = 0x5;
	rid2->rid     = rid;
	rid2->rid_idx = 0;
}

static void make_dom_sid2(DOM_SID2 *sid2, char *sid_str)
{
	int len_sid_str = strlen(sid_str);

	sid2->type = 0x5;
	sid2->undoc = 0;
	make_uni_hdr2(&(sid2->hdr), len_sid_str, len_sid_str, 0);
	make_unistr  (&(sid2->str), sid_str);
}

static void make_dom_query(DOM_QUERY *d_q, char *dom_name, char *dom_sid)
{
	int domlen = strlen(dom_name);

	d_q->uni_dom_max_len = domlen * 2;
	d_q->padding = 0;
	d_q->uni_dom_str_len = domlen * 2;

	d_q->buffer_dom_name = 0; /* domain buffer pointer */
	d_q->buffer_dom_sid  = 0; /* domain sid pointer */

	/* NOT null-terminated: 4-terminated instead! */
	make_unistr2(&(d_q->uni_domain_name), dom_name, domlen, 4);

	make_dom_sid(&(d_q->dom_sid), dom_sid);
}

static int lsa_reply_query_info(LSA_Q_QUERY_INFO *q_q, char *q, char *base,
				char *dom_name, char *dom_sid)
{
	char *start = q;
	LSA_R_QUERY_INFO r_q;

	/* set up the LSA QUERY INFO response */

	r_q.undoc_buffer = 1; /* not null */
	r_q.info_class = q_q->info_class;

	make_dom_query(&r_q.dom.id5, dom_name, dom_sid);

	r_q.status = 0x0;

	/* store the response in the SMB stream */
	q = lsa_io_r_query(False, &r_q, q, base, 4);

	/* return length of SMB data stored */
	return q - start; 
}

/* pretty much hard-coded choice of "other" sids, unfortunately... */
static void make_dom_ref(DOM_R_REF *ref,
				char *dom_name, char *dom_sid,
				char *other_sid1, char *other_sid2, char *other_sid3)
{
	int len_dom_name   = strlen(dom_name);
	int len_other_sid1 = strlen(other_sid1);
	int len_other_sid2 = strlen(other_sid2);
	int len_other_sid3 = strlen(other_sid3);

	ref->undoc_buffer = 1;
	ref->num_ref_doms_1 = 4;
	ref->buffer_dom_name = 1;
	ref->max_entries = 32;
	ref->num_ref_doms_2 = 4;

	make_uni_hdr2(&(ref->hdr_dom_name  ), len_dom_name  , len_dom_name  , 0);
	make_uni_hdr2(&(ref->hdr_ref_dom[0]), len_other_sid1, len_other_sid1, 0);
	make_uni_hdr2(&(ref->hdr_ref_dom[1]), len_other_sid2, len_other_sid2, 0);
	make_uni_hdr2(&(ref->hdr_ref_dom[2]), len_other_sid3, len_other_sid3, 0);

	if (dom_name != NULL)
	{
		make_unistr(&(ref->uni_dom_name), dom_name);
	}

	make_dom_sid(&(ref->ref_dom[0]), dom_sid   );
	make_dom_sid(&(ref->ref_dom[1]), other_sid1);
	make_dom_sid(&(ref->ref_dom[2]), other_sid2);
	make_dom_sid(&(ref->ref_dom[3]), other_sid3);
}

static void make_reply_lookup_rids(LSA_R_LOOKUP_RIDS *r_l,
				int num_entries, uint32 dom_rids[MAX_LOOKUP_SIDS],
				char *dom_name, char *dom_sid,
				char *other_sid1, char *other_sid2, char *other_sid3)
{
	int i;

	make_dom_ref(&(r_l->dom_ref), dom_name, dom_sid,
	             other_sid1, other_sid2, other_sid3);

	r_l->num_entries = num_entries;
	r_l->undoc_buffer = 1;
	r_l->num_entries2 = num_entries;

	for (i = 0; i < num_entries; i++)
	{
		make_dom_rid2(&(r_l->dom_rid[i]), dom_rids[i]);
	}

	r_l->num_entries3 = num_entries;
}

static void make_reply_lookup_sids(LSA_R_LOOKUP_SIDS *r_l,
				int num_entries, fstring dom_sids[MAX_LOOKUP_SIDS],
				char *dom_name, char *dom_sid,
				char *other_sid1, char *other_sid2, char *other_sid3)
{
	int i;

	make_dom_ref(&(r_l->dom_ref), dom_name, dom_sid,
	             other_sid1, other_sid2, other_sid3);

	r_l->num_entries = num_entries;
	r_l->undoc_buffer = 1;
	r_l->num_entries2 = num_entries;

	for (i = 0; i < num_entries; i++)
	{
		make_dom_sid2(&(r_l->dom_sid[i]), dom_sids[i]);
	}

	r_l->num_entries3 = num_entries;
}

static int lsa_reply_lookup_sids(char *q, char *base,
				int num_entries, fstring dom_sids[MAX_LOOKUP_SIDS],
				char *dom_name, char *dom_sid,
				char *other_sid1, char *other_sid2, char *other_sid3)
{
	char *start = q;
	LSA_R_LOOKUP_SIDS r_l;

	/* set up the LSA Lookup SIDs response */
	make_reply_lookup_sids(&r_l, num_entries, dom_sids,
				dom_name, dom_sid, other_sid1, other_sid2, other_sid3);
	r_l.status = 0x0;

	/* store the response in the SMB stream */
	q = lsa_io_r_lookup_sids(False, &r_l, q, base, 4);

	/* return length of SMB data stored */
	return q - start; 
}

static int lsa_reply_lookup_rids(char *q, char *base,
				int num_entries, uint32 dom_rids[MAX_LOOKUP_SIDS],
				char *dom_name, char *dom_sid,
				char *other_sid1, char *other_sid2, char *other_sid3)
{
	char *start = q;
	LSA_R_LOOKUP_RIDS r_l;

	/* set up the LSA Lookup RIDs response */
	make_reply_lookup_rids(&r_l, num_entries, dom_rids,
				dom_name, dom_sid, other_sid1, other_sid2, other_sid3);
	r_l.status = 0x0;

	/* store the response in the SMB stream */
	q = lsa_io_r_lookup_rids(False, &r_l, q, base, 4);

	/* return length of SMB data stored */
	return q - start; 
}

static void make_lsa_r_req_chal(LSA_R_REQ_CHAL *r_c, char chal[8], int status)
{
	memcpy(r_c->srv_chal.data, chal, sizeof(r_c->srv_chal.data));
	r_c->status = status;
}

#if 0
	char chal[8];
	/* PAXX: set these to random values */
	for (int i = 0; i < 8; i+++)
	{
		chal[i] = 0xA5;
	}
#endif

static int lsa_reply_req_chal(LSA_Q_REQ_CHAL *q_c, char *q, char *base,
					char chal[8])
{
	char *start = q;
	LSA_R_REQ_CHAL r_c;

	/* set up the LSA REQUEST CHALLENGE response */

	make_lsa_r_req_chal(&r_c, chal, 0);

	/* store the response in the SMB stream */
	q = lsa_io_r_req_chal(False, &r_c, q, base, 4);

	/* return length of SMB data stored */
	return q - start; 
}

static void make_lsa_chal(DOM_CHAL *cred, char resp_cred[8])
{
	memcpy(cred->data, resp_cred, sizeof(cred->data));
}

static void make_lsa_r_auth_2(LSA_R_AUTH_2 *r_a,
                              char resp_cred[8], NEG_FLAGS *flgs, int status)
{
	make_lsa_chal(&(r_a->srv_chal), resp_cred);
	memcpy(&(r_a->srv_flgs), flgs, sizeof(r_a->srv_flgs));
	r_a->status = status;
}

static int lsa_reply_auth_2(LSA_Q_AUTH_2 *q_a, char *q, char *base,
				char resp_cred[8], int status)
{
	char *start = q;
	LSA_R_AUTH_2 r_a;

	/* set up the LSA AUTH 2 response */

	make_lsa_r_auth_2(&r_a, resp_cred, &(q_a->clnt_flgs), status);

	/* store the response in the SMB stream */
	q = lsa_io_r_auth_2(False, &r_a, q, base, 4);

	/* return length of SMB data stored */
	return q - start; 
}

static void make_lsa_dom_chal(DOM_CRED *cred, char srv_chal[8], UTIME srv_time)
{
	make_lsa_chal(&(cred->challenge), srv_chal);
	cred->timestamp = srv_time;
}
	

static void make_lsa_r_srv_pwset(LSA_R_SRV_PWSET *r_a,
                              char srv_chal[8], UTIME srv_time, int status)
{
	make_lsa_dom_chal(&(r_a->srv_cred), srv_chal, srv_time);
	r_a->status = status;
}

static int lsa_reply_srv_pwset(LSA_Q_SRV_PWSET *q_s, char *q, char *base,
				char srv_cred[8], UTIME srv_time,
				int status)
{
	char *start = q;
	LSA_R_SRV_PWSET r_s;

	/* set up the LSA Server Password Set response */
	make_lsa_r_srv_pwset(&r_s, srv_cred, srv_time, status);

	/* store the response in the SMB stream */
	q = lsa_io_r_srv_pwset(False, &r_s, q, base, 4);

	/* return length of SMB data stored */
	return q - start; 
}

static void make_lsa_user_info(LSA_USER_INFO *usr,

	NTTIME *logon_time,
	NTTIME *logoff_time,
	NTTIME *kickoff_time,
	NTTIME *pass_last_set_time,
	NTTIME *pass_can_change_time,
	NTTIME *pass_must_change_time,

	char *user_name,
	char *full_name,
	char *logon_script,
	char *profile_path,
	char *home_dir,
	char *dir_drive,

	uint16 logon_count,
	uint16 bad_pw_count,

	uint32 user_id,
	uint32 group_id,
	uint32 num_groups,
	DOM_GID *gids,
	uint32 user_flgs,

	char sess_key[16],

	char *logon_srv,
	char *logon_dom,

	char *dom_sid,
	char *other_sids) /* space-delimited set of SIDs */ 
{
	/* only cope with one "other" sid, right now. */
	/* need to count the number of space-delimited sids */
	int i;
	int num_other_sids = other_sids != NULL ? 1 : 0;

	int len_user_name    = strlen(user_name   );
	int len_full_name    = strlen(full_name   );
	int len_logon_script = strlen(logon_script);
	int len_profile_path = strlen(profile_path);
	int len_home_dir     = strlen(home_dir    );
	int len_dir_drive    = strlen(dir_drive   );

	int len_logon_srv    = strlen(logon_srv);
	int len_logon_dom    = strlen(logon_dom);

	usr->undoc_buffer = 1; /* yes, we're bothering to put USER_INFO data here */

	usr->logon_time            = *logon_time;
	usr->logoff_time           = *logoff_time;
	usr->kickoff_time          = *kickoff_time;
	usr->pass_last_set_time    = *pass_last_set_time;
	usr->pass_can_change_time  = *pass_can_change_time;
	usr->pass_must_change_time = *pass_must_change_time;

	make_uni_hdr(&(usr->hdr_user_name   ), len_user_name   , len_user_name   , 4);
	make_uni_hdr(&(usr->hdr_full_name   ), len_full_name   , len_full_name   , 4);
	make_uni_hdr(&(usr->hdr_logon_script), len_logon_script, len_logon_script, 4);
	make_uni_hdr(&(usr->hdr_profile_path), len_profile_path, len_profile_path, 4);
	make_uni_hdr(&(usr->hdr_home_dir    ), len_home_dir    , len_home_dir    , 4);
	make_uni_hdr(&(usr->hdr_dir_drive   ), len_dir_drive   , len_dir_drive   , 4);

	usr->logon_count = logon_count;
	usr->bad_pw_count = bad_pw_count;

	usr->user_id = user_id;
	usr->group_id = group_id;
	usr->num_groups = num_groups;
	usr->buffer_groups = num_groups ? 1 : 0; /* yes, we're bothering to put group info in */
	usr->user_flgs = user_flgs;

	if (sess_key != NULL)
	{
		memcpy(usr->sess_key, sess_key, sizeof(usr->sess_key));
	}
	else
	{
		bzero(usr->sess_key, sizeof(usr->sess_key));
	}

	make_uni_hdr(&(usr->hdr_logon_srv), len_logon_srv, len_logon_srv, 4);
	make_uni_hdr(&(usr->hdr_logon_dom), len_logon_dom, len_logon_dom, 4);

	usr->buffer_dom_id = dom_sid ? 1 : 0; /* yes, we're bothering to put a domain SID in */

	bzero(usr->padding, sizeof(usr->padding));

	usr->num_other_sids = num_other_sids;
	usr->buffer_other_sids = num_other_sids != 0 ? 1 : 0; 
	
	make_unistr2(&(usr->uni_user_name   ), user_name   , len_user_name   , 0);
	make_unistr2(&(usr->uni_full_name   ), full_name   , len_full_name   , 0);
	make_unistr2(&(usr->uni_logon_script), logon_script, len_logon_script, 0);
	make_unistr2(&(usr->uni_profile_path), profile_path, len_profile_path, 0);
	make_unistr2(&(usr->uni_home_dir    ), home_dir    , len_home_dir    , 0);
	make_unistr2(&(usr->uni_dir_drive   ), dir_drive   , len_dir_drive   , 0);

	usr->num_groups2 = num_groups;
	for (i = 0; i < num_groups; i++)
	{
		usr->gids[i] = gids[i];
	}

	make_unistr2(&(usr->uni_logon_srv), logon_srv, len_logon_srv, 0);
	make_unistr2(&(usr->uni_logon_dom), logon_dom, len_logon_dom, 0);

	make_dom_sid(&(usr->dom_sid), dom_sid);
	make_dom_sid(&(usr->other_sids[0]), other_sids);
}


static int lsa_reply_sam_logon(LSA_Q_SAM_LOGON *q_s, char *q, char *base,
				char srv_cred[8], UTIME srv_time,
				LSA_USER_INFO *user_info)
{
	char *start = q;
	LSA_R_SAM_LOGON r_s;

	/* XXXX maybe we want to say 'no', reject the client's credentials */
	r_s.buffer_creds = 1; /* yes, we have valid server credentials */
	make_lsa_dom_chal(&(r_s.srv_creds), srv_cred, srv_time);

	/* store the user information, if there is any. */
	r_s.user = user_info;
	r_s.buffer_user = user_info != NULL ? 1 : 0;
	r_s.status = user_info != NULL ? 0 : (0xC000000|NT_STATUS_NO_SUCH_USER);

	/* store the response in the SMB stream */
	q = lsa_io_r_sam_logon(False, &r_s, q, base, 4);

	/* return length of SMB data stored */
	return q - start; 
}


static int lsa_reply_sam_logoff(LSA_Q_SAM_LOGOFF *q_s, char *q, char *base,
				char srv_cred[8], UTIME srv_time,
				uint32 status)
{
	char *start = q;
	LSA_R_SAM_LOGOFF r_s;

	/* XXXX maybe we want to say 'no', reject the client's credentials */
	r_s.buffer_creds = 1; /* yes, we have valid server credentials */
	make_lsa_dom_chal(&(r_s.srv_creds), srv_cred, srv_time);

	r_s.status = status;

	/* store the response in the SMB stream */
	q = lsa_io_r_sam_logoff(False, &r_s, q, base, 4);

	/* return length of SMB data stored */
	return q - start; 
}


static void api_lsa_open_policy( char *param, char *data,
                             char **rdata, int *rdata_len )
{
	int reply_len;

	/* we might actually want to decode the query, but it's not necessary */
	/* lsa_io_q_open_policy(...); */

	/* return a 20 byte policy handle */
	reply_len = lsa_reply_open_policy(*rdata + 0x18, *rdata + 0x18);

	/* construct header, now that we know the reply length */
	make_rpc_reply(data, *rdata, reply_len);
	*rdata_len = reply_len + 0x18;
}

static void api_lsa_query_info( char *param, char *data,
                                char **rdata, int *rdata_len )
{
	int reply_len;

	LSA_Q_QUERY_INFO q_i;
	pstring dom_name;
	pstring dom_sid;

	/* grab the info class and policy handle */
	lsa_io_q_query(True, &q_i, data + 0x18, data + 0x18, 4);

	pstrcpy(dom_name, lp_workgroup());
	pstrcpy(dom_sid , lp_domainsid());

	/* construct reply.  return status is always 0x0 */
	reply_len = lsa_reply_query_info(&q_i, *rdata + 0x18, *rdata + 0x18, 
									 dom_name, dom_sid);

	/* construct header, now that we know the reply length */
	make_rpc_reply(data, *rdata, reply_len);
	*rdata_len = reply_len + 0x18;
}

static void api_lsa_lookup_sids( char *param, char *data,
                                 char **rdata, int *rdata_len )
{
	int reply_len;

	int i;
	LSA_Q_LOOKUP_SIDS q_l;
	pstring dom_name;
	pstring dom_sid;
	fstring dom_sids[MAX_LOOKUP_SIDS];

	/* grab the info class and policy handle */
	lsa_io_q_lookup_sids(True, &q_l, data + 0x18, data + 0x18, 4);

	pstrcpy(dom_name, lp_workgroup());
	pstrcpy(dom_sid , lp_domainsid());

	/* convert received SIDs to strings, so we can do them. */
	for (i = 0; i < q_l.num_entries; i++)
	{
		fstrcpy(dom_sids[i], dom_sid_to_string(&(q_l.dom_sids[i])));
	}

	/* construct reply.  return status is always 0x0 */
	reply_len = lsa_reply_lookup_sids(*rdata + 0x18, *rdata + 0x18,
	            q_l.num_entries, dom_sids, /* text-converted SIDs */
				dom_name, dom_sid, /* domain name, domain SID */
				"S-1-1", "S-1-3", "S-1-5"); /* the three other SIDs */

	/* construct header, now that we know the reply length */
	make_rpc_reply(data, *rdata, reply_len);
	*rdata_len = reply_len + 0x18;
}

static void api_lsa_lookup_names( char *param, char *data,
                                  char **rdata, int *rdata_len )
{
	int reply_len;

	int i;
	LSA_Q_LOOKUP_RIDS q_l;
	pstring dom_name;
	pstring dom_sid;
	uint32 dom_rids[MAX_LOOKUP_SIDS];

	/* grab the info class and policy handle */
	lsa_io_q_lookup_rids(True, &q_l, data + 0x18, data + 0x18, 4);

	pstrcpy(dom_name, lp_workgroup());
	pstrcpy(dom_sid , lp_domainsid());

	/* convert received RIDs to strings, so we can do them. */
	for (i = 0; i < q_l.num_entries; i++)
	{
		char *user_name = unistr2(q_l.lookup_name[i].str.buffer);
		dom_rids[i] = name_to_rid(user_name);
	}

	/* construct reply.  return status is always 0x0 */
	reply_len = lsa_reply_lookup_rids(*rdata + 0x18, *rdata + 0x18,
	            q_l.num_entries, dom_rids, /* text-converted SIDs */
				dom_name, dom_sid, /* domain name, domain SID */
				"S-1-1", "S-1-3", "S-1-5"); /* the three other SIDs */

	/* construct header, now that we know the reply length */
	make_rpc_reply(data, *rdata, reply_len);
	*rdata_len = reply_len + 0x18;
}

BOOL api_ntlsarpcTNP(int cnum,int uid, char *param,char *data,
		     int mdrcnt,int mprcnt,
		     char **rdata,char **rparam,
		     int *rdata_len,int *rparam_len)
{
	uint16 opnum = SVAL(data,22);

	int pkttype = CVAL(data, 2);
	if (pkttype == 0x0b) /* RPC BIND */
	{
		DEBUG(4,("netlogon rpc bind %x\n",pkttype));
		LsarpcTNP1(data,rdata,rdata_len);
		return True;
	}

	DEBUG(4,("ntlsa TransactNamedPipe op %x\n",opnum));
	switch (opnum)
	{
		case LSA_OPENPOLICY:
		{
			DEBUG(3,("LSA_OPENPOLICY\n"));
			api_lsa_open_policy(param, data, rdata, rdata_len);
			break;
		}

		case LSA_QUERYINFOPOLICY:
		{
			DEBUG(3,("LSA_QUERYINFOPOLICY\n"));

			api_lsa_query_info(param, data, rdata, rdata_len);
			break;
		}

		case LSA_ENUMTRUSTDOM:
		{
			char *q = *rdata + 0x18;

			DEBUG(3,("LSA_ENUMTRUSTDOM\n"));

			initrpcreply(data, *rdata);

			SIVAL(q, 0, 0); /* enumeration context */
			SIVAL(q, 0, 4); /* entries read */
			SIVAL(q, 0, 8); /* trust information */

			endrpcreply(data, *rdata, q-*rdata, 0x8000001a, rdata_len);

			break;
		}

		case LSA_CLOSE:
		{
			char *q = *rdata + 0x18;

			DEBUG(3,("LSA_CLOSE\n"));

			initrpcreply(data, *rdata);

			SIVAL(q, 0, 0);
			SIVAL(q, 0, 4);
			SIVAL(q, 0, 8);
			SIVAL(q, 0, 12);
			SIVAL(q, 0, 16);

			endrpcreply(data, *rdata, q-*rdata, 0, rdata_len);

			break;
		}

		case LSA_OPENSECRET:
		{
			char *q = *rdata + 0x18;
			DEBUG(3,("LSA_OPENSECRET\n"));

			initrpcreply(data, *rdata);

			SIVAL(q, 0, 0);
			SIVAL(q, 0, 4);
			SIVAL(q, 0, 8);
			SIVAL(q, 0, 12);
			SIVAL(q, 0, 16);

			endrpcreply(data, *rdata, q-*rdata, 0xc000034, rdata_len);

			break;
		}

		case LSA_LOOKUPSIDS:
		{
			DEBUG(3,("LSA_OPENSECRET\n"));
			api_lsa_lookup_sids(param, data, rdata, rdata_len);
			break;
		}

		case LSA_LOOKUPNAMES:
		{
			DEBUG(3,("LSA_LOOKUPNAMES\n"));
			api_lsa_lookup_names(param, data, rdata, rdata_len);
			break;
		}

		default:
		{
			DEBUG(4, ("NTLSARPC, unknown code: %lx\n", opnum));
			break;
		}
	}
	return True;
}

#endif /* NTDOMAIN */
