/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   SMB parameters and setup
   Copyright (C) Andrew Tridgell 1992-1997
   Copyright (C) John H Terpstra 1996-1997
   Copyright (C) Luke Kenneth Casson Leighton 1996-1997
   Copyright (C) Paul Ashton 1997
   
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
#ifndef _SMB_H
#define _SMB_H

#ifndef MAX_CONNECTIONS
#define MAX_CONNECTIONS 127
#endif

#ifndef MAX_OPEN_FILES
#define MAX_OPEN_FILES 50
#endif

#ifndef GUEST_ACCOUNT
#define GUEST_ACCOUNT "nobody"
#endif

#define BUFFER_SIZE (0xFFFF)
#define SAFETY_MARGIN 1024

/* Default size of shared memory used for share mode locking */
#ifndef SHMEM_SIZE
#define SHMEM_SIZE 102400
#endif

/* Default number of hash buckets used in shared memory share mode */
#ifndef SHMEM_HASH_SIZE
#define SHMEM_HASH_SIZE 113
#endif

#define NMB_PORT 137
#define DGRAM_PORT 138
#define SMB_PORT 139

#define False (0)
#define True (1)
#define BOOLSTR(b) ((b) ? "Yes" : "No")
#define BITSETB(ptr,bit) ((((char *)ptr)[0] & (1<<(bit)))!=0)
#define BITSETW(ptr,bit) ((SVAL(ptr,0) & (1<<(bit)))!=0)
#define PTR_DIFF(p1,p2) ((ptrdiff_t)(((char *)(p1)) - (char *)(p2)))

typedef int BOOL;

/* offset in shared memory */
typedef  int smb_shm_offset_t;
#define NULL_OFFSET (smb_shm_offset_t)(0)


/*
   Samba needs type definitions for int16, int32, uint16 and uint32.
   
   Normally these are signed and unsigned 16 and 32 bit integers, but
   they actually only need to be at least 16 and 32 bits
   respectively. Thus if your word size is 8 bytes just defining them
   as signed and unsigned int will work.
*/

/* afs/stds.h defines int16 and int32 */
#ifndef AFS_AUTH
typedef short int16;
typedef int int32;
#endif

#ifndef uint8
typedef unsigned char uint8;
#endif

#ifndef uint16
typedef unsigned short uint16;
#endif

#ifndef uint32
typedef unsigned int uint32;
#endif

#ifndef uchar
#define uchar unsigned char
#endif
#ifndef int16
#define int16 short
#endif
#ifndef uint16
#define uint16 unsigned short
#endif
#ifndef uint32
#define uint32 unsigned int
#endif

#define SIZEOFWORD 2

#ifndef DEF_CREATE_MASK
#define DEF_CREATE_MASK (0755)
#endif

/* how long to wait for secondary SMB packets (milli-seconds) */
#define SMB_SECONDARY_WAIT (60*1000)

/* debugging code */
#ifndef SYSLOG
#define DEBUG(level,body) ((DEBUGLEVEL>=(level))?(Debug1 body):0)
#else
extern int syslog_level;

#define DEBUG(level,body) ((DEBUGLEVEL>=(level))? (syslog_level = (level), Debug1 body):0)
#endif

/* this defines the error codes that receive_smb can put in smb_read_error */
#define READ_TIMEOUT 1
#define READ_EOF 2
#define READ_ERROR 3


#define DIR_STRUCT_SIZE 43

/* these define all the command types recognised by the server - there
are lots of gaps so probably there are some rare commands that are not
implemented */

#define pSETDIR '\377'

/* these define the attribute byte as seen by DOS */
#define aRONLY (1L<<0)
#define aHIDDEN (1L<<1)
#define aSYSTEM (1L<<2)
#define aVOLID (1L<<3)
#define aDIR (1L<<4)
#define aARCH (1L<<5)

/* deny modes */
#define DENY_DOS 0
#define DENY_ALL 1
#define DENY_WRITE 2
#define DENY_READ 3
#define DENY_NONE 4
#define DENY_FCB 7

/* share types */
#define STYPE_DISKTREE	0	/* Disk drive */
#define STYPE_PRINTQ	1	/* Spooler queue */
#define STYPE_DEVICE	2	/* Serial device */
#define STYPE_IPC	3	/* Interprocess communication (IPC) */

/* SMB X/Open error codes for the ERRdos error class */
#define ERRbadfunc 1 /* Invalid function (or system call) */
#define ERRbadfile 2 /* File not found (pathname error) */
#define ERRbadpath 3 /* Directory not found */
#define ERRnofids 4 /* Too many open files */
#define ERRnoaccess 5 /* Access denied */
#define ERRbadfid 6 /* Invalid fid */
#define ERRnomem 8 /* Out of memory */
#define ERRbadmem 9 /* Invalid memory block address */
#define ERRbadenv 10 /* Invalid environment */
#define ERRbadaccess 12 /* Invalid open mode */
#define ERRbaddata 13 /* Invalid data (only from ioctl call) */
#define ERRres 14 /* reserved */
#define ERRbaddrive 15 /* Invalid drive */
#define ERRremcd 16 /* Attempt to delete current directory */
#define ERRdiffdevice 17 /* rename/move across different filesystems */
#define ERRnofiles 18 /* no more files found in file search */
#define ERRbadshare 32 /* Share mode on file conflict with open mode */
#define ERRlock 33 /* Lock request conflicts with existing lock */
#define ERRfilexists 80 /* File in operation already exists */
#define ERRcannotopen 110 /* Cannot open the file specified */
#define ERRunknownlevel 124
#define ERRbadpipe 230 /* Named pipe invalid */
#define ERRpipebusy 231 /* All instances of pipe are busy */
#define ERRpipeclosing 232 /* named pipe close in progress */
#define ERRnotconnected 233 /* No process on other end of named pipe */
#define ERRmoredata 234 /* More data to be returned */
#define ERRbaddirectory 267 /* Invalid directory name in a path. */
#define ERROR_EAS_DIDNT_FIT 275 /* Extended attributes didn't fit */
#define ERROR_EAS_NOT_SUPPORTED 282 /* Extended attributes not supported */
#define ERRunknownipc 2142


/* here's a special one from observing NT */
#define ERRnoipc 66 /* don't support ipc */

/* Error codes for the ERRSRV class */

#define ERRerror 1 /* Non specific error code */
#define ERRbadpw 2 /* Bad password */
#define ERRbadtype 3 /* reserved */
#define ERRaccess 4 /* No permissions to do the requested operation */
#define ERRinvnid 5 /* tid invalid */
#define ERRinvnetname 6 /* Invalid servername */
#define ERRinvdevice 7 /* Invalid device */
#define ERRqfull 49 /* Print queue full */
#define ERRqtoobig 50 /* Queued item too big */
#define ERRinvpfid 52 /* Invalid print file in smb_fid */
#define ERRsmbcmd 64 /* Unrecognised command */
#define ERRsrverror 65 /* smb server internal error */
#define ERRfilespecs 67 /* fid and pathname invalid combination */
#define ERRbadlink 68 /* reserved */
#define ERRbadpermits 69 /* Access specified for a file is not valid */
#define ERRbadpid 70 /* reserved */
#define ERRsetattrmode 71 /* attribute mode invalid */
#define ERRpaused 81 /* Message server paused */
#define ERRmsgoff 82 /* Not receiving messages */
#define ERRnoroom 83 /* No room for message */
#define ERRrmuns 87 /* too many remote usernames */
#define ERRtimeout 88 /* operation timed out */
#define ERRnoresource  89 /* No resources currently available for request. */
#define ERRtoomanyuids 90 /* too many userids */
#define ERRbaduid 91 /* bad userid */
#define ERRuseMPX 250 /* temporarily unable to use raw mode, use MPX mode */
#define ERRuseSTD 251 /* temporarily unable to use raw mode, use standard mode */
#define ERRcontMPX 252 /* resume MPX mode */
#define ERRbadPW /* reserved */
#define ERRnosupport 0xFFFF
#define ERRunknownsmb 22 /* from NT 3.5 response */


/* Error codes for the ERRHRD class */

#define ERRnowrite 19 /* read only media */
#define ERRbadunit 20 /* Unknown device */
#define ERRnotready 21 /* Drive not ready */
#define ERRbadcmd 22 /* Unknown command */
#define ERRdata 23 /* Data (CRC) error */
#define ERRbadreq 24 /* Bad request structure length */
#define ERRseek 25
#define ERRbadmedia 26
#define ERRbadsector 27
#define ERRnopaper 28
#define ERRwrite 29 /* write fault */
#define ERRread 30 /* read fault */
#define ERRgeneral 31 /* General hardware failure */
#define ERRwrongdisk 34
#define ERRFCBunavail 35
#define ERRsharebufexc 36 /* share buffer exceeded */
#define ERRdiskfull 39


typedef char pstring[1024];
typedef char fstring[128];
typedef fstring string;


/* NETLOGON opcodes and data structures */

#define NET_QUERYFORPDC	     7 /* Query for PDC */
#define NET_QUERYFORPDC_R   12 /* Response to Query for PDC */
#define NET_SAMLOGON        18
#define NET_SAMLOGON_R      19

/* Allowable account control bits */
#define ACB_DISABLED   1 /* 1 = User account disabled */
#define ACB_HOMDIRREQ  2 /* 1 = Home directory required */
#define ACB_PWNOTREQ   4 /* 1 = User password not required */
#define ACB_TEMPDUP      /* 1 = Temporary duplicate account */
#define ACB_NORMAL       /* 1 = Normal user account */
#define ACB_MNS          /* 1 = MNS logon user account */
#define ACB_DOMTRUST     /* 1 = Interdomain trust account */
#define ACB_WSTRUST      /* 1 = Workstation trust account */
#define ACB_SVRTRUST     /* 1 = Server trust account */
#define ACB_PWNOEXP      /* 1 = User password does not expire */
#define ACB_AUTOLOCK     /* 1 = Account auto locked */

#define LSA_OPENPOLICY      0x2c
#define LSA_QUERYINFOPOLICY 0x07
#define LSA_ENUMTRUSTDOM    0x0d
#define LSA_REQCHAL         0x04
#define LSA_SVRPWSET        0x06
#define LSA_SAMLOGON        0x02
#define LSA_AUTH2           0x0f
#define LSA_CLOSE           0x00

/* unknown .... */
#define LSA_OPENSECRET      0xFF
#define LSA_LOOKUPSIDS      0xFE
#define LSA_LOOKUPNAMES     0xFD
#define LSA_SAMLOGOFF       0xFC


/* 32 bit time (sec) since 01jan1970 - cifs6.txt, section 3.5, page 30 */
typedef struct time_info
{
  uint32 time;

} UTIME;

/* 64 bit time (100usec) since ????? - cifs6.txt, section 3.5, page 30 */
typedef struct nttime_info
{
  uint32 low;
  uint32 high;

} NTTIME;
 

#define MAXSUBAUTHS 10 /* max sub authorities in a SID */

/* DOM_SID - security id */
typedef struct sid_info
{
  uint8  sid_no;                  /* SID revision number */
  uint8  num_auths;               /* number of sub-authorities */
  uint8  id_auth[6];              /* Identifier Authority */
  uint16 sub_auths[MAXSUBAUTHS]; /* pointer to sub-authorities. */

} DOM_SID;

/* UNIHDR - unicode string header */
typedef struct unihdr_info
{
  uint16 uni_max_len;
  uint16 uni_str_len;
  uint32 undoc; /* usually has a value of 4 */

} UNIHDR;

/* UNIHDR2 - unicode string header and undocumented buffer */
typedef struct unihdr2_info
{
  UNIHDR unihdr;
  uint32 undoc_buffer; /* undocumented 32 bit buffer pointer */

} UNIHDR2;

/* clueless as to what maximum length should be */
#define MAX_UNISTRLEN 1024

/* UNISTR - unicode string size and buffer */
typedef struct unistr_info
{
  uint16 buffer[MAX_UNISTRLEN]; /* unicode characters. ***MUST*** be null-terminated */

} UNISTR;

/* UNISTR2 - unicode string size and buffer */
typedef struct unistr2_info
{
  uint32 uni_max_len;
  uint32 undoc;
  uint32 uni_str_len;
  uint16 buffer[MAX_UNISTRLEN]; /* unicode characters. **NOT** necessarily null-terminated */

} UNISTR2;

/* DOM_SID2 - domain SID structure - SIDs stored in unicode */
typedef struct domsid2_info
{
  uint32 type; /* value is 5 */
  uint32 undoc; /* value is 0 */

  UNIHDR2 hdr; /* XXXX conflict between hdr and str for length */
  UNISTR  str; /* XXXX conflict between hdr and str for length */

} DOM_SID2;

/* DOM_RID2 - domain RID structure */
typedef struct domrid2_info
{
  uint32 type; /* value is 5 */
  uint32 undoc; /* value is 5 */
  uint32 rid;
  uint32 rid_idx; /* don't know what this is */

} DOM_RID2;

/* DOM_LOG_INFO - login info */
typedef struct log_info
{
  uint32  undoc_buffer; /* undocumented 32 bit buffer pointer */
  UNISTR2 uni_logon_srv; /* logon server name */
  UNISTR2 uni_acct_name; /* account name */
  uint16  sec_chan;      /* secure channel type */
  UNISTR2 uni_comp_name; /* client machine name */

} DOM_LOG_INFO;

/* DOM_CHAL - challenge info */
typedef struct chal_info
{
  uint8 data[8]; /* credentials */

} DOM_CHAL;

/* DOM_CREDs - timestamped client or server credentials */
typedef struct cred_info
{
  DOM_CHAL challenge; /* credentials */
  UTIME timestamp;    /* credential time-stamp */

} DOM_CRED;

/* DOM_CLNT_INFO - client info */
typedef struct clnt_info
{
  DOM_LOG_INFO login;
  DOM_CRED     cred;

} DOM_CLNT_INFO;

/* DOM_LOGON_ID - logon id */
typedef struct logon_info
{
  uint32 low;
  uint32 high;

} DOM_LOGON_ID;

/* ARC4_OWF */
typedef struct arc4_owf_info
{
  uint8 data[16];

} ARC4_OWF;


/* DOM_ID_INFO_1 */
typedef struct id_info_1
{
  UNIHDR            hdr_domain_name;     /* domain name unicode header */
  uint32            param;               /* param control */
  DOM_LOGON_ID      logon_id;            /* logon ID */
  UNIHDR            hdr_user_name;       /* user name unicode header */
  UNIHDR            hdr_workgroup_name;  /* workgroup name unicode header */
  ARC4_OWF          arc4_lm_owf;         /* rc4 LM OWF Password */
  ARC4_OWF          arc4_nt_owf;         /* rc4 NT OWF Password */
  UNISTR2           uni_domain_name;     /* domain name unicode string */
  UNISTR2           uni_user_name;       /* user name unicode string */
  UNISTR2           uni_workgroup_name;  /* workgroup name unicode string */

} DOM_ID_INFO_1;

/* SAM_INFO - sam logon/off id structure */
typedef struct sam_info
{
  DOM_CLNT_INFO client;
  DOM_CRED      rtn_cred; /* return credentials */
  uint16        logon_level;
  uint32        auth_level; /* undocumented */
  
  union
  {
    DOM_ID_INFO_1 id1; /* auth-level 1 */

  } auth;
  
} DOM_SAM_INFO;

/* DOM_GID - group id + user attributes */
typedef struct gid_info
{
  uint32 gid;  /* group id */
  uint32 attr;

} DOM_GID;

/* RPC_HDR - ms rpc header */
typedef struct rpc_hdr_info
{
  uint8  major; /* 5 - RPC major version */
  uint8  minor; /* 0 - RPC minor version */
  uint8  pkt_type; /* 2 - RPC response packet */
  uint8  frag; /* 3 - first frag + last frag */
  uint32 pack_type; /* 0x0000 0010 - packed data representation */
  uint16 frag_len; /* fragment length - data size (bytes) inc header and tail. */
  uint16 auth_len; /* 0 - authentication length  */
  uint32 call_id; /* call identifier.  matches 12th uint32 of incoming RPC data. */
  uint32 alloc_hint; /* allocation hint - data size (bytes) minus header and tail. */
  uint16 context_id; /* 0 - presentation context identifier */
  uint8  cancel_count; /* 0 - cancel count */
  uint8  reserved; /* 0 - reserved */

} RPC_HDR;

/* DOM_QUERY - info class 3 and 5 LSA Query response */
typedef struct dom_query_info
{
  uint16 uni_dom_max_len; /* domain name string length * 2 */
  uint16 padding;         /* 2 padding bytes? */
  uint16 uni_dom_str_len; /* domain name string length * 2 */
  uint32 buffer_dom_name; /* undocumented domain name string buffer pointer */
  uint32 buffer_dom_sid; /* undocumented domain SID string buffer pointer */
  UNISTR2 uni_domain_name; /* domain name (unicode string) */
  DOM_SID dom_sid; /* domain SID */

} DOM_QUERY;

/* level 5 is same as level 3.  we hope. */
typedef DOM_QUERY DOM_QUERY_3;
typedef DOM_QUERY DOM_QUERY_5;

#define POL_HND_SIZE 20

/* LSA_POL_HND */
typedef struct lsa_policy_info
{
  uint8 data[POL_HND_SIZE]; /* policy handle */

} LSA_POL_HND;


/* LSA_R_OPEN_POL - response to LSA Open Policy */
typedef struct lsa_r_open_pol_info
{
	LSA_POL_HND pol; /* policy handle */

	uint32 status; /* return code */

} LSA_R_OPEN_POL;

/* LSA_Q_QUERY_INFO - LSA query info policy */
typedef struct lsa_query_info
{
	LSA_POL_HND pol; /* policy handle */
    uint16 info_class; /* info class */

} LSA_Q_QUERY_INFO;

/* LSA_R_QUERY_INFO - response to LSA query info policy */
typedef struct lsa_r_query_info
{
    uint32 undoc_buffer; /* undocumented buffer pointer */
    uint16 info_class; /* info class (same as info class in request) */
    
	union
    {
        DOM_QUERY_3 id3;
		DOM_QUERY_5 id5;
    } dom;

  uint32 status; /* return code */

} LSA_R_QUERY_INFO;

#define MAX_REF_DOMAINS 10

/* DOM_R_REF */
typedef struct dom_ref_info
{
    uint32 undoc_buffer; /* undocumented buffer pointer. */
    uint32 num_ref_doms_1; /* num referenced domains? */
    uint32 buffer_dom_name; /* undocumented domain name buffer pointer. */
    uint32 max_entries; /* 32 - max number of entries */
    uint32 num_ref_doms_2; /* 4 - num referenced domains? */

    UNIHDR2 hdr_dom_name; /* domain name unicode string header */
    UNIHDR2 hdr_ref_dom[MAX_REF_DOMAINS]; /* referenced domain unicode string headers */

    UNISTR uni_dom_name; /* domain name unicode string */
    DOM_SID ref_dom[MAX_REF_DOMAINS]; /* referenced domain SIDs */

} DOM_R_REF;

#define MAX_LOOKUP_SIDS 10

/* LSA_Q_LOOKUP_SIDS - LSA Lookup SIDs */
typedef struct lsa_q_lookup_sids
{
    LSA_POL_HND pol_hnd; /* policy handle */
    uint32 num_entries;
    uint32 buffer_dom_sid; /* undocumented domain SID buffer pointer */
    uint32 buffer_dom_name; /* undocumented domain name buffer pointer */
    uint32 buffer_lookup_sids[MAX_LOOKUP_SIDS]; /* undocumented domain SID pointers to be looked up. */
    DOM_SID dom_sids[MAX_LOOKUP_SIDS]; /* domain SIDs to be looked up. */
    uint8 undoc[16]; /* completely undocumented 16 bytes */

} LSA_Q_LOOKUP_SIDS;

/* LSA_R_LOOKUP_SIDS - response to LSA Lookup SIDs */
typedef struct lsa_r_lookup_sids
{
    DOM_R_REF dom_ref; /* domain reference info */

    uint32 num_entries;
    uint32 undoc_buffer; /* undocumented buffer pointer */
    uint32 num_entries2; 

    DOM_SID2 dom_sid[MAX_LOOKUP_SIDS]; /* domain SIDs being looked up */

    uint32 num_entries3; 

  uint32 status; /* return code */

} LSA_R_LOOKUP_SIDS;

/* DOM_NAME - XXXX not sure about this structure */
typedef struct dom_name_info
{
    uint32 uni_str_len;
	UNISTR str;

} DOM_NAME;


#define UNKNOWN_LEN 1

/* LSA_Q_LOOKUP_RIDS - LSA Lookup RIDs */
typedef struct lsa_q_lookup_rids
{

    LSA_POL_HND pol_hnd; /* policy handle */
    uint32 num_entries;
    uint32 num_entries2;
    uint32 buffer_dom_sid; /* undocumented domain SID buffer pointer */
    uint32 buffer_dom_name; /* undocumented domain name buffer pointer */
    DOM_NAME lookup_name[MAX_LOOKUP_SIDS]; /* names to be looked up */
    uint8 undoc[UNKNOWN_LEN]; /* completely undocumented bytes of unknown length */

} LSA_Q_LOOKUP_RIDS;

/* LSA_R_LOOKUP_RIDS - response to LSA Lookup Names */
typedef struct lsa_r_lookup_rids
{
    DOM_R_REF dom_ref; /* domain reference info */

    uint32 num_entries;
    uint32 undoc_buffer; /* undocumented buffer pointer */

    uint32 num_entries2; 
    DOM_RID2 dom_rid[MAX_LOOKUP_SIDS]; /* domain RIDs being looked up */

    uint32 num_entries3; 

  uint32 status; /* return code */

} LSA_R_LOOKUP_RIDS;



/* NEG_FLAGS */
typedef struct lsa_neg_flags_info
{
    uint32 neg_flags; /* negotiated flags */

} NEG_FLAGS;


/* LSA_Q_REQ_CHAL */
typedef struct lsa_q_req_chal_info
{
    uint32  undoc_buffer; /* undocumented buffer pointer */
    UNISTR2 uni_logon_srv; /* logon server unicode string */
    UNISTR2 uni_logon_clnt; /* logon client unicode string */
    DOM_CHAL clnt_chal; /* client challenge */

} LSA_Q_REQ_CHAL;


/* LSA_R_REQ_CHAL */
typedef struct lsa_r_req_chal_info
{
    DOM_CHAL srv_chal; /* server challenge */

  uint32 status; /* return code */

} LSA_R_REQ_CHAL;



/* LSA_Q_AUTH_2 */
typedef struct lsa_q_auth2_info
{
    DOM_LOG_INFO clnt_id; /* client identification info */
    DOM_CHAL clnt_chal;     /* client-calculated credentials */

    NEG_FLAGS clnt_flgs; /* usually 0x0000 01ff */

} LSA_Q_AUTH_2;


/* LSA_R_AUTH_2 */
typedef struct lsa_r_auth2_info
{
    DOM_CHAL srv_chal;     /* server-calculated credentials */
    NEG_FLAGS srv_flgs; /* usually 0x0000 01ff */

  uint32 status; /* return code */

} LSA_R_AUTH_2;


/* LSA_Q_SRV_PWSET */
typedef struct lsa_q_srv_pwset_info
{
    DOM_CLNT_INFO clnt_id; /* client identification/authentication info */
    char pwd[16]; /* new password - undocumented. */

} LSA_Q_SRV_PWSET;
    
/* LSA_R_SRV_PWSET */
typedef struct lsa_r_srv_pwset_info
{
    DOM_CRED srv_cred;     /* server-calculated credentials */

  uint32 status; /* return code */

} LSA_R_SRV_PWSET;

#define LSA_MAX_GROUPS 32
#define LSA_MAX_SIDS 32

/* LSA_USER_INFO */
typedef struct lsa_q_user_info
{
	uint32 undoc_buffer;

	NTTIME logon_time;            /* logon time */
	NTTIME logoff_time;           /* logoff time */
	NTTIME kickoff_time;          /* kickoff time */
	NTTIME pass_last_set_time;    /* password last set time */
	NTTIME pass_can_change_time;  /* password can change time */
	NTTIME pass_must_change_time; /* password must change time */

	UNIHDR hdr_user_name;    /* username unicode string header */
	UNIHDR hdr_full_name;    /* user's full name unicode string header */
	UNIHDR hdr_logon_script; /* logon script unicode string header */
	UNIHDR hdr_profile_path; /* profile path unicode string header */
	UNIHDR hdr_home_dir;     /* home directory unicode string header */
	UNIHDR hdr_dir_drive;    /* home directory drive unicode string header */

	uint16 logon_count;  /* logon count */
	uint16 bad_pw_count; /* bad password count */

	uint32 user_id;       /* User ID */
	uint32 group_id;      /* Group ID */
	uint32 num_groups;    /* num groups */
	uint32 buffer_groups; /* undocumented buffer pointer to groups. */
	uint32 user_flgs;     /* user flags */

	char sess_key[16]; /* unused user session key */

	UNIHDR hdr_logon_srv; /* logon server unicode string header */
	UNIHDR hdr_logon_dom; /* logon domain unicode string header */

	uint32 buffer_dom_id; /* undocumented logon domain id pointer */
	char padding[40];    /* unused padding bytes? */

	uint32 num_other_sids; /* 0 - num_sids */
	uint32 buffer_other_sids; /* NULL - undocumented pointer to SIDs. */
	
	UNISTR2 uni_user_name;    /* username unicode string */
	UNISTR2 uni_full_name;    /* user's full name unicode string */
	UNISTR2 uni_logon_script; /* logon script unicode string */
	UNISTR2 uni_profile_path; /* profile path unicode string */
	UNISTR2 uni_home_dir;     /* home directory unicode string */
	UNISTR2 uni_dir_drive;    /* home directory drive unicode string */

	uint32 num_groups2;        /* num groups */
	DOM_GID gids[LSA_MAX_GROUPS]; /* group info */

	UNISTR2 uni_logon_srv; /* logon server unicode string */
	UNISTR2 uni_logon_dom; /* logon domain unicode string */

	DOM_SID dom_sid;           /* domain SID */
	DOM_SID other_sids[LSA_MAX_SIDS]; /* undocumented - domain SIDs */

} LSA_USER_INFO;


/* LSA_Q_SAM_LOGON */
typedef struct lsa_q_sam_logon_info
{
    DOM_SAM_INFO sam_id;

} LSA_Q_SAM_LOGON;

/* LSA_R_SAM_LOGON */
typedef struct lsa_r_sam_logon_info
{
    uint32 buffer_creds; /* undocumented buffer pointer */
    DOM_CRED srv_creds; /* server credentials.  server time stamp appears to be ignored. */
    
    uint32 buffer_user;
    LSA_USER_INFO *user;

    uint32 auth_resp; /* 1 - Authoritative response; 0 - Non-Auth? */

  uint32 status; /* return code */

} LSA_R_SAM_LOGON;


/* LSA_Q_SAM_LOGOFF */
typedef struct lsa_q_sam_logoff_info
{
    DOM_SAM_INFO sam_id;

} LSA_Q_SAM_LOGOFF;

/* LSA_R_SAM_LOGOFF */
typedef struct lsa_r_sam_logoff_info
{
    uint32 buffer_creds; /* undocumented buffer pointer */
    DOM_CRED srv_creds; /* server credentials.  server time stamp appears to be ignored. */
    
  uint32 status; /* return code */

} LSA_R_SAM_LOGOFF;

/*

Yet to be turned into structures:

6) \\MAILSLOT\NET\NTLOGON
-------------------------

6.1) Query for PDC
------------------

Request:

    uint16         0x0007 - Query for PDC
    STR            machine name
    STR            response mailslot
    uint8[]        padding to 2-byte align with start of mailslot.
    UNISTR         machine name
    uint32         NTversion
    uint16         LMNTtoken
    uint16         LM20token

Response:

    uint16         0x000A - Respose to Query for PDC
    STR            machine name (in uppercase)
    uint8[]        padding to 2-byte align with start of mailslot.
    UNISTR         machine name
    UNISTR         domain name
    uint32         NTversion (same as received in request)
    uint16         LMNTtoken (same as received in request)
    uint16         LM20token (same as received in request)


6.2) SAM Logon
--------------

Request:

    uint16         0x0012 - SAM Logon
    uint16         request count
    UNISTR         machine name
    UNISTR         user name
    STR            response mailslot
    uint32         alloweable account
    uint32         domain SID size
    char[sid_size] domain SID, of sid_size bytes.
    uint8[]        ???? padding to 4? 2? -byte align with start of mailslot.
    uint32         NTversion
    uint16         LMNTtoken
    uint16         LM20token
    
Response:

    uint16         0x0013 - Response to SAM Logon
    UNISTR         machine name
    UNISTR         user name - workstation trust account
    UNISTR         domain name 
    uint32         NTversion
    uint16         LMNTtoken
    uint16         LM20token

*/


struct smb_passwd {
	int smb_userid;
	char *smb_name;
	unsigned char *smb_passwd; /* Null if no password */
	unsigned char *smb_nt_passwd; /* Null if no password */
	/* Other fields / flags may be added later */
};


struct current_user {
  int cnum, id;
  int uid, gid;
  int ngroups;
  gid_t *groups;
  int *igroups;
};

typedef struct
{
  int size;
  int mode;
  int uid;
  int gid;
  /* these times are normally kept in GMT */
  time_t mtime;
  time_t atime;
  time_t ctime;
  pstring name;
} file_info;


/* Structure used when SMBwritebmpx is active */
typedef struct
        {
	int   wr_total_written; /* So we know when to discard this */
	int32 wr_timeout;
	int32 wr_errclass;
	int32 wr_error; /* Cached errors */
	BOOL  wr_mode; /* write through mode) */
	BOOL  wr_discard; /* discard all further data */
        } write_bmpx_struct;

/*
 * Structure used to indirect fd's from the files_struct.
 * Needed as POSIX locking is based on file and process, not
 * file descriptor and process.
 */

typedef struct
{
  uint16 ref_count;
  uint32 dev;
  uint32 inode;
  int fd;
  int fd_readonly;
  int fd_writeonly;
  int real_open_flags;
} file_fd_struct;

typedef struct
{
  int cnum;
  file_fd_struct *fd_ptr;
  int pos;
  uint32 size;
  int mode;
  int uid;
  char *mmap_ptr;
  uint32 mmap_size;
  write_bmpx_struct *wbmpx_ptr;
  struct timeval open_time;
  BOOL open;
  BOOL can_lock;
  BOOL can_read;
  BOOL can_write;
  BOOL share_mode;
  BOOL print_file;
  BOOL modified;
  BOOL granted_oplock;
  char *name;
} files_struct;


struct uid_cache {
  int entries;
  int list[UID_CACHE_SIZE];
};

typedef struct
{
  char *name;
  BOOL is_wild;
} name_compare_entry;

typedef struct
{
  int service;
  BOOL force_user;
  struct uid_cache uid_cache;
  void *dirptr;
  BOOL open;
  BOOL printer;
  BOOL ipc;
  BOOL read_only;
  BOOL admin_user;
  char *dirpath;
  char *connectpath;
  char *origpath;
  char *user; /* name of user who *opened* this connection */
  int uid; /* uid of user who *opened* this connection */
  int gid; /* gid of user who *opened* this connection */
  uint16 vuid; /* vuid of user who *opened* this connection, or UID_FIELD_INVALID */
  /* following groups stuff added by ih */
  /* This groups info is valid for the user that *opened* the connection */
  int ngroups;
  gid_t *groups;
  int *igroups; /* an integer version - some OSes are broken :-( */
  time_t lastused;
  BOOL used;
  int num_files_open;
  name_compare_entry *hide_list; /* Per-share list of files to return as hidden. */
  name_compare_entry *veto_list; /* Per-share list of files to veto (never show). */
} connection_struct;


typedef struct
{
  int uid; /* uid of a validated user */
  int gid; /* gid of a validated user */
  fstring name; /* name of a validated user */
  BOOL guest;
  /* following groups stuff added by ih */
  /* This groups info is needed for when we become_user() for this uid */
  int user_ngroups;
  gid_t *user_groups;
  int *user_igroups; /* an integer version - some OSes are broken :-( */
#if (defined(NETGROUP) && defined(AUTOMOUNT))
  char *home_share;  /* to store NIS home of a user - simeon */
#endif
  char *real_name;   /* to store real name from password file - simeon */
} user_struct;


enum {LPQ_QUEUED,LPQ_PAUSED,LPQ_SPOOLING,LPQ_PRINTING};

typedef struct
{
  int job;
  int size;
  int status;
  int priority;
  time_t time;
  char user[30];
  char file[100];
} print_queue_struct;

enum {LPSTAT_OK, LPSTAT_STOPPED, LPSTAT_ERROR};

typedef struct
{
  fstring message;
  int status;
}  print_status_struct;

/* used for server information: client, nameserv and ipc */
struct server_info_struct
{
  fstring name;
  uint32 type;
  fstring comment;
  fstring domain; /* used ONLY in ipc.c NOT namework.c */
  BOOL server_added; /* used ONLY in ipc.c NOT namework.c */
};


/* used for network interfaces */
struct interface
{
	struct interface *next;
	struct in_addr ip;
	struct in_addr bcast;
	struct in_addr nmask;
};

/* share mode record pointed to in shared memory hash bucket */
typedef struct
{
  smb_shm_offset_t next_offset; /* offset of next record in chain from hash bucket */
  int locking_version;
  int32 st_dev;
  int32 st_ino;
  int num_share_mode_entries;
  smb_shm_offset_t share_mode_entries; /* Chain of share mode entries for this file */
  char file_name[1];
} share_mode_record;

/* share mode entry pointed to by share_mode_record struct */
typedef struct
{
  smb_shm_offset_t next_share_mode_entry;
  int pid;
  uint16 op_port;
  uint16 op_type;
  int share_mode;
  struct timeval time;
} share_mode_entry;

/* struct returned by get_share_modes */
typedef struct
{
  int pid;
  uint16 op_port;
  uint16 op_type;
  int share_mode;
  struct timeval time;
} min_share_mode_entry;

/* Token returned by lock_share_entry (actually ignored by FAST_SHARE_MODES code) */
typedef int share_lock_token;

/* Conversion to hash entry index from device and inode numbers. */
#define HASH_ENTRY(dev,ino) ((( (uint32)(dev) )* ( (uint32)(ino) )) % lp_shmem_hash_size())

/* this is used for smbstatus */
struct connect_record
{
  int magic;
  int pid;
  int cnum;
  int uid;
  int gid;
  char name[24];
  char addr[24];
  char machine[128];
  time_t start;
};

#ifndef LOCKING_VERSION
#define LOCKING_VERSION 4
#endif /* LOCKING_VERSION */

#if !defined(FAST_SHARE_MODES)
/* 
 * Defines for slow share modes.
 */

/* 
 * Locking file header lengths & offsets. 
 */
#define SMF_VERSION_OFFSET 0
#define SMF_NUM_ENTRIES_OFFSET 4
#define SMF_FILENAME_LEN_OFFSET 8
#define SMF_HEADER_LENGTH 10

#define SMF_ENTRY_LENGTH 20

/*
 * Share mode record offsets.
 */

#define SME_SEC_OFFSET 0
#define SME_USEC_OFFSET 4
#define SME_SHAREMODE_OFFSET 8
#define SME_PID_OFFSET 12
#define SME_PORT_OFFSET 16
#define SME_OPLOCK_TYPE_OFFSET 18

#endif /* FAST_SHARE_MODES */

/* these are useful macros for checking validity of handles */
#define VALID_FNUM(fnum)   (((fnum) >= 0) && ((fnum) < MAX_OPEN_FILES))
#define OPEN_FNUM(fnum)    (VALID_FNUM(fnum) && Files[fnum].open)
#define VALID_CNUM(cnum)   (((cnum) >= 0) && ((cnum) < MAX_CONNECTIONS))
#define OPEN_CNUM(cnum)    (VALID_CNUM(cnum) && Connections[cnum].open)
#define IS_IPC(cnum)       (VALID_CNUM(cnum) && Connections[cnum].ipc)
#define IS_PRINT(cnum)       (VALID_CNUM(cnum) && Connections[cnum].printer)
#define FNUM_OK(fnum,c) (OPEN_FNUM(fnum) && (c)==Files[fnum].cnum)

#define CHECK_FNUM(fnum,c) if (!FNUM_OK(fnum,c)) \
                               return(ERROR(ERRDOS,ERRbadfid))
#define CHECK_READ(fnum) if (!Files[fnum].can_read) \
                               return(ERROR(ERRDOS,ERRbadaccess))
#define CHECK_WRITE(fnum) if (!Files[fnum].can_write) \
                               return(ERROR(ERRDOS,ERRbadaccess))
#define CHECK_ERROR(fnum) if (HAS_CACHED_ERROR(fnum)) \
                               return(CACHED_ERROR(fnum))

/* translates a connection number into a service number */
#define SNUM(cnum)         (Connections[cnum].service)

/* access various service details */
#define SERVICE(snum)      (lp_servicename(snum))
#define PRINTCAP           (lp_printcapname())
#define PRINTCOMMAND(snum) (lp_printcommand(snum))
#define PRINTERNAME(snum)  (lp_printername(snum))
#define CAN_WRITE(cnum)    (OPEN_CNUM(cnum) && !Connections[cnum].read_only)
#define VALID_SNUM(snum)   (lp_snum_ok(snum))
#define GUEST_OK(snum)     (VALID_SNUM(snum) && lp_guest_ok(snum))
#define GUEST_ONLY(snum)   (VALID_SNUM(snum) && lp_guest_only(snum))
#define CAN_SETDIR(snum)   (!lp_no_set_dir(snum))
#define CAN_PRINT(cnum)    (OPEN_CNUM(cnum) && lp_print_ok(SNUM(cnum)))
#define POSTSCRIPT(cnum)   (OPEN_CNUM(cnum) && lp_postscript(SNUM(cnum)))
#define MAP_HIDDEN(cnum)   (OPEN_CNUM(cnum) && lp_map_hidden(SNUM(cnum)))
#define MAP_SYSTEM(cnum)   (OPEN_CNUM(cnum) && lp_map_system(SNUM(cnum)))
#define MAP_ARCHIVE(cnum)   (OPEN_CNUM(cnum) && lp_map_archive(SNUM(cnum)))
#define IS_HIDDEN_PATH(cnum,path)  (is_in_path((path),Connections[(cnum)].hide_list))
#define IS_VETO_PATH(cnum,path)  (is_in_path((path),Connections[(cnum)].veto_list))

#define SMBENCRYPT()       (lp_encrypted_passwords())

/* the basic packet size, assuming no words or bytes */
#define smb_size 39

/* offsets into message for common items */
#define smb_com 8
#define smb_rcls 9
#define smb_reh 10
#define smb_err 11
#define smb_flg 13
#define smb_flg2 14
#define smb_reb 13
#define smb_tid 28
#define smb_pid 30
#define smb_uid 32
#define smb_mid 34
#define smb_wct 36
#define smb_vwv 37
#define smb_vwv0 37
#define smb_vwv1 39
#define smb_vwv2 41
#define smb_vwv3 43
#define smb_vwv4 45
#define smb_vwv5 47
#define smb_vwv6 49
#define smb_vwv7 51
#define smb_vwv8 53
#define smb_vwv9 55
#define smb_vwv10 57
#define smb_vwv11 59
#define smb_vwv12 61
#define smb_vwv13 63
#define smb_vwv14 65
#define smb_vwv15 67
#define smb_vwv16 69
#define smb_vwv17 71


/* the complete */
#define SMBmkdir      0x00   /* create directory */
#define SMBrmdir      0x01   /* delete directory */
#define SMBopen       0x02   /* open file */
#define SMBcreate     0x03   /* create file */
#define SMBclose      0x04   /* close file */
#define SMBflush      0x05   /* flush file */
#define SMBunlink     0x06   /* delete file */
#define SMBmv         0x07   /* rename file */
#define SMBgetatr     0x08   /* get file attributes */
#define SMBsetatr     0x09   /* set file attributes */
#define SMBread       0x0A   /* read from file */
#define SMBwrite      0x0B   /* write to file */
#define SMBlock       0x0C   /* lock byte range */
#define SMBunlock     0x0D   /* unlock byte range */
#define SMBctemp      0x0E   /* create temporary file */
#define SMBmknew      0x0F   /* make new file */
#define SMBchkpth     0x10   /* check directory path */
#define SMBexit       0x11   /* process exit */
#define SMBlseek      0x12   /* seek */
#define SMBtcon       0x70   /* tree connect */
#define SMBtconX      0x75   /* tree connect and X*/
#define SMBtdis       0x71   /* tree disconnect */
#define SMBnegprot    0x72   /* negotiate protocol */
#define SMBdskattr    0x80   /* get disk attributes */
#define SMBsearch     0x81   /* search directory */
#define SMBsplopen    0xC0   /* open print spool file */
#define SMBsplwr      0xC1   /* write to print spool file */
#define SMBsplclose   0xC2   /* close print spool file */
#define SMBsplretq    0xC3   /* return print queue */
#define SMBsends      0xD0   /* send single block message */
#define SMBsendb      0xD1   /* send broadcast message */
#define SMBfwdname    0xD2   /* forward user name */
#define SMBcancelf    0xD3   /* cancel forward */
#define SMBgetmac     0xD4   /* get machine name */
#define SMBsendstrt   0xD5   /* send start of multi-block message */
#define SMBsendend    0xD6   /* send end of multi-block message */
#define SMBsendtxt    0xD7   /* send text of multi-block message */

/* Core+ protocol */
#define SMBlockread	  0x13   /* Lock a range and read */
#define SMBwriteunlock 0x14 /* Unlock a range then write */
#define SMBreadbraw   0x1a  /* read a block of data with no smb header */
#define SMBwritebraw  0x1d  /* write a block of data with no smb header */
#define SMBwritec     0x20  /* secondary write request */
#define SMBwriteclose 0x2c  /* write a file then close it */

/* dos extended protocol */
#define SMBreadBraw      0x1A   /* read block raw */
#define SMBreadBmpx      0x1B   /* read block multiplexed */
#define SMBreadBs        0x1C   /* read block (secondary response) */
#define SMBwriteBraw     0x1D   /* write block raw */
#define SMBwriteBmpx     0x1E   /* write block multiplexed */
#define SMBwriteBs       0x1F   /* write block (secondary request) */
#define SMBwriteC        0x20   /* write complete response */
#define SMBsetattrE      0x22   /* set file attributes expanded */
#define SMBgetattrE      0x23   /* get file attributes expanded */
#define SMBlockingX      0x24   /* lock/unlock byte ranges and X */
#define SMBtrans         0x25   /* transaction - name, bytes in/out */
#define SMBtranss        0x26   /* transaction (secondary request/response) */
#define SMBioctl         0x27   /* IOCTL */
#define SMBioctls        0x28   /* IOCTL  (secondary request/response) */
#define SMBcopy          0x29   /* copy */
#define SMBmove          0x2A   /* move */
#define SMBecho          0x2B   /* echo */
#define SMBopenX         0x2D   /* open and X */
#define SMBreadX         0x2E   /* read and X */
#define SMBwriteX        0x2F   /* write and X */
#define SMBsesssetupX    0x73   /* Session Set Up & X (including User Logon) */
#define SMBffirst        0x82   /* find first */
#define SMBfunique       0x83   /* find unique */
#define SMBfclose        0x84   /* find close */
#define SMBinvalid       0xFE   /* invalid command */

/* Extended 2.0 protocol */
#define SMBtrans2        0x32   /* TRANS2 protocol set */
#define SMBtranss2       0x33   /* TRANS2 protocol set, secondary command */
#define SMBfindclose     0x34   /* Terminate a TRANSACT2_FINDFIRST */
#define SMBfindnclose    0x35   /* Terminate a TRANSACT2_FINDNOTIFYFIRST */
#define SMBulogoffX      0x74   /* user logoff */


/* these are the TRANS2 sub commands */
#define TRANSACT2_OPEN          0
#define TRANSACT2_FINDFIRST     1
#define TRANSACT2_FINDNEXT      2
#define TRANSACT2_QFSINFO       3
#define TRANSACT2_SETFSINFO     4
#define TRANSACT2_QPATHINFO     5
#define TRANSACT2_SETPATHINFO   6
#define TRANSACT2_QFILEINFO     7
#define TRANSACT2_SETFILEINFO   8
#define TRANSACT2_FSCTL         9
#define TRANSACT2_IOCTL           10
#define TRANSACT2_FINDNOTIFYFIRST 11
#define TRANSACT2_FINDNOTIFYNEXT  12
#define TRANSACT2_MKDIR           13


/* these are the trans2 sub fields for primary requests */
#define smb_tpscnt smb_vwv0
#define smb_tdscnt smb_vwv1
#define smb_mprcnt smb_vwv2
#define smb_mdrcnt smb_vwv3
#define smb_msrcnt smb_vwv4
#define smb_flags smb_vwv5
#define smb_timeout smb_vwv6
#define smb_pscnt smb_vwv9
#define smb_psoff smb_vwv10
#define smb_dscnt smb_vwv11
#define smb_dsoff smb_vwv12
#define smb_suwcnt smb_vwv13
#define smb_setup smb_vwv14
#define smb_setup0 smb_setup
#define smb_setup1 (smb_setup+2)
#define smb_setup2 (smb_setup+4)

/* these are for the secondary requests */
#define smb_spscnt smb_vwv2
#define smb_spsoff smb_vwv3
#define smb_spsdisp smb_vwv4
#define smb_sdscnt smb_vwv5
#define smb_sdsoff smb_vwv6
#define smb_sdsdisp smb_vwv7
#define smb_sfid smb_vwv8

/* and these for responses */
#define smb_tprcnt smb_vwv0
#define smb_tdrcnt smb_vwv1
#define smb_prcnt smb_vwv3
#define smb_proff smb_vwv4
#define smb_prdisp smb_vwv5
#define smb_drcnt smb_vwv6
#define smb_droff smb_vwv7
#define smb_drdisp smb_vwv8

/* where to find the base of the SMB packet proper */
#define smb_base(buf) (((char *)(buf))+4)


#define SUCCESS 0  /* The request was successful. */
#define ERRDOS 0x01 /*  Error is from the core DOS operating system set. */
#define ERRSRV 0x02  /* Error is generated by the server network file manager.*/
#define ERRHRD 0x03  /* Error is an hardware error. */
#define ERRCMD 0xFF  /* Command was not in the "SMB" format. */

#ifdef __STDC__
int Debug1(char *, ...);
#else
int Debug1();
#endif

#ifdef DFS_AUTH
void dfs_unlogin(void);
extern int dcelogin_atmost_once;
#endif

#if AJT
void ajt_panic(void);
#endif

#ifdef NOSTRDUP
char *strdup(char *s);
#endif

#ifdef REPLACE_STRLEN
int Strlen(char *);
#endif

#ifdef REPLACE_STRSTR
char *Strstr(char *s, char *p);
#endif

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#ifndef ABS
#define ABS(a) ((a)>0?(a):(-(a)))
#endif

#ifndef SIGNAL_CAST
#define SIGNAL_CAST
#endif

#ifndef SELECT_CAST
#define SELECT_CAST
#endif


/* Some POSIX definitions for those without */
 
#ifndef S_IFDIR
#define S_IFDIR         0x4000
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode)   ((mode & 0xF000) == S_IFDIR)
#endif
#ifndef S_IRWXU
#define S_IRWXU 00700           /* read, write, execute: owner */
#endif
#ifndef S_IRUSR
#define S_IRUSR 00400           /* read permission: owner */
#endif
#ifndef S_IWUSR
#define S_IWUSR 00200           /* write permission: owner */
#endif
#ifndef S_IXUSR
#define S_IXUSR 00100           /* execute permission: owner */
#endif
#ifndef S_IRWXG
#define S_IRWXG 00070           /* read, write, execute: group */
#endif
#ifndef S_IRGRP
#define S_IRGRP 00040           /* read permission: group */
#endif
#ifndef S_IWGRP
#define S_IWGRP 00020           /* write permission: group */
#endif
#ifndef S_IXGRP
#define S_IXGRP 00010           /* execute permission: group */
#endif
#ifndef S_IRWXO
#define S_IRWXO 00007           /* read, write, execute: other */
#endif
#ifndef S_IROTH
#define S_IROTH 00004           /* read permission: other */
#endif
#ifndef S_IWOTH
#define S_IWOTH 00002           /* write permission: other */
#endif
#ifndef S_IXOTH
#define S_IXOTH 00001           /* execute permission: other */
#endif


/* these are used in NetServerEnum to choose what to receive */
#define SV_TYPE_WORKSTATION         0x00000001
#define SV_TYPE_SERVER              0x00000002
#define SV_TYPE_SQLSERVER           0x00000004
#define SV_TYPE_DOMAIN_CTRL         0x00000008
#define SV_TYPE_DOMAIN_BAKCTRL      0x00000010
#define SV_TYPE_TIME_SOURCE         0x00000020
#define SV_TYPE_AFP                 0x00000040
#define SV_TYPE_NOVELL              0x00000080
#define SV_TYPE_DOMAIN_MEMBER       0x00000100
#define SV_TYPE_PRINTQ_SERVER       0x00000200
#define SV_TYPE_DIALIN_SERVER       0x00000400
#define SV_TYPE_SERVER_UNIX         0x00000800
#define SV_TYPE_NT                  0x00001000
#define SV_TYPE_WFW                 0x00002000
#define SV_TYPE_SERVER_MFPN         0x00004000
#define SV_TYPE_SERVER_NT           0x00008000
#define SV_TYPE_POTENTIAL_BROWSER   0x00010000
#define SV_TYPE_BACKUP_BROWSER      0x00020000
#define SV_TYPE_MASTER_BROWSER      0x00040000
#define SV_TYPE_DOMAIN_MASTER       0x00080000
#define SV_TYPE_SERVER_OSF          0x00100000
#define SV_TYPE_SERVER_VMS          0x00200000
#define SV_TYPE_WIN95_PLUS          0x00400000
#define SV_TYPE_ALTERNATE_XPORT     0x20000000  
#define SV_TYPE_LOCAL_LIST_ONLY     0x40000000  
#define SV_TYPE_DOMAIN_ENUM         0x80000000
#define SV_TYPE_ALL                 0xFFFFFFFF  

/* what server type are we currently  - JHT Says we ARE 4.20 */
/* this was set by JHT in liaison with Jeremy Allison early 1997 */
/* setting to 4.20 at same time as announcing ourselves as NT Server */
/* History: */
/* Version 4.0 - never made public */
/* Version 4.10 - New to 1.9.16p2, lost in space 1.9.16p3 to 1.9.16p9 */
/*		- Reappeared in 1.9.16p11 with fixed smbd services */
/* Version 4.20 - To indicate that nmbd and browsing now works better */

#define DEFAULT_MAJOR_VERSION 0x04
#define DEFAULT_MINOR_VERSION 0x02

/* Browser Election Values */
#define BROWSER_ELECTION_VERSION	0x010f
#define BROWSER_CONSTANT	0xaa55


/* Capabilities.  see ftp.microsoft.com/developr/drg/cifs/cifs/cifs4.txt */

#define CAP_RAW_MODE         0x0001
#define CAP_MPX_MODE         0x0002
#define CAP_UNICODE          0x0004
#define CAP_LARGE_FILES      0x0008
#define CAP_NT_SMBS          0x0010
#define CAP_RPC_REMOTE_APIS  0x0020
#define CAP_STATUS32         0x0040
#define CAP_LEVEL_II_OPLOCKS 0x0080
#define CAP_LOCK_AND_READ    0x0100
#define CAP_NT_FIND          0x0200
#define CAP_DFS              0x1000
#define CAP_LARGE_READX      0x4000

/* protocol types. It assumes that higher protocols include lower protocols
   as subsets */
enum protocol_types {PROTOCOL_NONE,PROTOCOL_CORE,PROTOCOL_COREPLUS,PROTOCOL_LANMAN1,PROTOCOL_LANMAN2,PROTOCOL_NT1};

/* security levels */
enum security_types {SEC_SHARE,SEC_USER,SEC_SERVER};

/* printing types */
enum printing_types {PRINT_BSD,PRINT_SYSV,PRINT_AIX,PRINT_HPUX,
		     PRINT_QNX,PRINT_PLP,PRINT_LPRNG};

/* Remote architectures we know about. */
enum remote_arch_types {RA_UNKNOWN, RA_WFWG, RA_OS2, RA_WIN95, RA_WINNT, RA_SAMBA};

/* case handling */
enum case_handling {CASE_LOWER,CASE_UPPER};


/* Macros to get at offsets within smb_lkrng and smb_unlkrng
   structures. We cannot define these as actual structures
   due to possible differences in structure packing
   on different machines/compilers. */

#define SMB_LPID_OFFSET(indx) (10 * (indx))
#define SMB_LKOFF_OFFSET(indx) ( 2 + (10 * (indx)))
#define SMB_LKLEN_OFFSET(indx) ( 6 + (10 * (indx)))

/* Macro to cache an error in a write_bmpx_struct */
#define CACHE_ERROR(w,c,e) ((w)->wr_errclass = (c), (w)->wr_error = (e), \
			    w->wr_discard = True, -1)
/* Macro to test if an error has been cached for this fnum */
#define HAS_CACHED_ERROR(fnum) (Files[(fnum)].open && \
				Files[(fnum)].wbmpx_ptr && \
				Files[(fnum)].wbmpx_ptr->wr_discard)
/* Macro to turn the cached error into an error packet */
#define CACHED_ERROR(fnum) cached_error_packet(inbuf,outbuf,fnum,__LINE__)

/* these are the datagram types */
#define DGRAM_DIRECT_UNIQUE 0x10

#define ERROR(class,x) error_packet(inbuf,outbuf,class,x,__LINE__)

/* this is how errors are generated */
#define UNIXERROR(defclass,deferror) unix_error_packet(inbuf,outbuf,defclass,deferror,__LINE__)

#define ROUNDUP(x,g) (((x)+((g)-1))&~((g)-1))

/*
 * Global value meaing that the smb_uid field should be
 * ingored (in share level security and protocol level == CORE)
 */

#define UID_FIELD_INVALID 0
#define VUID_OFFSET 100 /* Amount to bias returned vuid numbers */

#endif 

/* Defines needed for multi-codepage support. */
#define KANJI_CODEPAGE 932

#ifdef KANJI
/* 
 * Default client code page - Japanese 
 */
#define DEFAULT_CLIENT_CODE_PAGE KANJI_CODEPAGE
#else /* KANJI */
/* 
 * Default client code page - 850 - Western European 
 */
#define DEFAULT_CLIENT_CODE_PAGE 850
#endif /* KANJI */

/* 
 * Size of buffer to use when moving files across filesystems. 
 */
#define COPYBUF_SIZE (8*1024)

/* 
 * Integers used to override error codes. 
 */
extern int unix_ERR_class;
extern int unix_ERR_code;

/*
 * Map the Core and Extended Oplock requesst bits down
 * to common bits (EXCLUSIVE_OPLOCK & BATCH_OPLOCK).
 */

/*
 * Core protocol.
 */
#define CORE_OPLOCK_REQUEST(inbuf) (((CVAL(inbuf,smb_flg)|(1<<5))>>5) | \
                                    ((CVAL(inbuf,smb_flg)|(1<<6))>>5))

/*
 * Extended protocol.
 */
#define EXTENDED_OPLOCK_REQUEST(inbuf) (((SVAL(inbuf,smb_vwv2)|(1<<1))>>1) | \
                                        ((SVAL(inbuf,smb_vwv2)|(1<<2))>>1))

/* Lock types. */
#define LOCKING_ANDX_SHARED_LOCK 0x1
#define LOCKING_ANDX_OPLOCK_RELEASE 0x2
#define LOCKING_ANDX_CHANGE_LOCKTYPE 0x4
#define LOCKING_ANDX_CANCEL_LOCK 0x8
#define LOCKING_ANDX_LARGE_FILES 0x10

/* Oplock levels */
#define OPLOCKLEVEL_NONE 0
#define OPLOCKLEVEL_II 1

/*
 * Bits we test with.
 */
#define EXCLUSIVE_OPLOCK 1
#define BATCH_OPLOCK 2

#define CORE_OPLOCK_GRANTED (1<<5)
#define EXTENDED_OPLOCK_GRANTED (1<<15)

/*
 * Loopback command offsets.
 */

#define UDP_CMD_LEN_OFFSET 0
#define UDP_CMD_PORT_OFFSET 4
#define UDP_CMD_HEADER_LEN 6

#define UDP_MESSAGE_CMD_OFFSET 0

/*
 * Oplock break command code to send over the udp socket.
 * 
 * Form of this is :
 *
 *  0     2       6        10       14      18       22
 *  +----+--------+--------+--------+-------+--------+
 *  | cmd| pid    | dev    | inode  | sec   |  usec  |
 *  +----+--------+--------+--------+-------+--------+
 */

#define OPLOCK_BREAK_CMD 0x1
#define OPLOCK_BREAK_PID_OFFSET 2
#define OPLOCK_BREAK_DEV_OFFSET 6
#define OPLOCK_BREAK_INODE_OFFSET 10
#define OPLOCK_BREAK_SEC_OFFSET 14
#define OPLOCK_BREAK_USEC_OFFSET 18
#define OPLOCK_BREAK_MSG_LEN 22


#define CMD_REPLY 0x8000

/* _SMB_H */
