/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   SMB parameters and setup
   Copyright (C) Andrew Tridgell              1992-2000
   Copyright (C) John H Terpstra              1996-2000
   Copyright (C) Luke Kenneth Casson Leighton 1996-2000
   Copyright (C) Paul Ashton                  1998-2000
   
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

#define BUFFER_SIZE (0xFFFF)
#define SAFETY_MARGIN 1024

#define NMB_PORT 137
#define DGRAM_PORT 138
#define SMB_PORT 139

#define False (0)
#define True (1)
#define Auto (2)

#ifndef _BOOL
typedef int BOOL;
#define _BOOL       /* So we don't typedef BOOL again in vfs.h */
#endif

/* limiting size of ipc replies */
#define REALLOC(ptr,size) Realloc(ptr,MAX((size),4*1024))

#define SIZEOFWORD 2

#ifndef DEF_CREATE_MASK
#define DEF_CREATE_MASK (0755)
#endif

/* string manipulation flags - see clistr.c and srvstr.c */
#define STR_TERMINATE 1
#define STR_CONVERT 2
#define STR_UPPER 4
#define STR_ASCII 8
#define STR_UNICODE 16


/* how long to wait for secondary SMB packets (milli-seconds) */
#define SMB_SECONDARY_WAIT (60*1000)

/* Debugging stuff */
#include "debug.h"

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

/* open modes */
#define DOS_OPEN_RDONLY 0
#define DOS_OPEN_WRONLY 1
#define DOS_OPEN_RDWR 2
#define DOS_OPEN_FCB 0xF

/* define shifts and masks for share and open modes. */
#define OPEN_MODE_MASK 0xF
#define SHARE_MODE_SHIFT 4
#define SHARE_MODE_MASK 0x7
#define GET_OPEN_MODE(x) ((x) & OPEN_MODE_MASK)
#define SET_OPEN_MODE(x) ((x) & OPEN_MODE_MASK)
#define GET_DENY_MODE(x) (((x)>>SHARE_MODE_SHIFT) & SHARE_MODE_MASK)
#define SET_DENY_MODE(x) ((x)<<SHARE_MODE_SHIFT)

/* Sync on open file (not sure if used anymore... ?) */
#define FILE_SYNC_OPENMODE (1<<14)
#define GET_FILE_SYNC_OPENMODE(x) (((x) & FILE_SYNC_OPENMODE) ? True : False)

/* allow delete on open file mode (used by NT SMB's). */
#define ALLOW_SHARE_DELETE (1<<15)
#define GET_ALLOW_SHARE_DELETE(x) (((x) & ALLOW_SHARE_DELETE) ? True : False)
#define SET_ALLOW_SHARE_DELETE(x) ((x) ? ALLOW_SHARE_DELETE : 0)

/* delete on close flag (used by NT SMB's). */
#define DELETE_ON_CLOSE_FLAG (1<<16)
#define GET_DELETE_ON_CLOSE_FLAG(x) (((x) & DELETE_ON_CLOSE_FLAG) ? True : False)
#define SET_DELETE_ON_CLOSE_FLAG(x) ((x) ? DELETE_ON_CLOSE_FLAG : 0)

/* was delete access requested in NT open ? */
#define DELETE_ACCESS_REQUESTED (1<<17)
#define GET_DELETE_ACCESS_REQUESTED(x) (((x) & DELETE_ACCESS_REQUESTED) ? True : False)
#define SET_DELETE_ACCESS_REQUESTED(x) ((x) ? DELETE_ACCESS_REQUESTED : 0)

/* open disposition values */
#define FILE_EXISTS_FAIL 0
#define FILE_EXISTS_OPEN 1
#define FILE_EXISTS_TRUNCATE 2

/* mask for open disposition. */
#define FILE_OPEN_MASK 0x3

#define GET_FILE_OPEN_DISPOSITION(x) ((x) & FILE_OPEN_MASK)
#define SET_FILE_OPEN_DISPOSITION(x) ((x) & FILE_OPEN_MASK)

/* The above can be OR'ed with... */
#define FILE_CREATE_IF_NOT_EXIST 0x10
#define FILE_FAIL_IF_NOT_EXIST 0

#define GET_FILE_CREATE_DISPOSITION(x) ((x) & (FILE_CREATE_IF_NOT_EXIST|FILE_FAIL_IF_NOT_EXIST))

/* share types */
#define STYPE_DISKTREE  0	/* Disk drive */
#define STYPE_PRINTQ    1	/* Spooler queue */
#define STYPE_DEVICE    2	/* Serial device */
#define STYPE_IPC       3	/* Interprocess communication (IPC) */
#define STYPE_HIDDEN    0x80000000 /* share is a hidden one (ends with $) */

/* SMB X/Open error codes for the ERRDOS error class */
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
#define ERRunsup 50 /* Request unsupported, returned by Win 95, RJS 20Jun98 */
#define ERRnosuchshare 67 /* You specified an invalid share name */
#define ERRfilexists 80 /* File in operation already exists */
#define ERRcannotopen 110 /* Cannot open the file specified */
#define ERRunknownlevel 124
#define ERRrename 183
#define ERRbadpipe 230 /* Named pipe invalid */
#define ERRpipebusy 231 /* All instances of pipe are busy */
#define ERRpipeclosing 232 /* named pipe close in progress */
#define ERRnotconnected 233 /* No process on other end of named pipe */
#define ERRmoredata 234 /* More data to be returned */
#define ERRbaddirectory 267 /* Invalid directory name in a path. */
#define ERRunknownipc 2142
#define ERRbuftoosmall 2123
#define ERRnosuchprintjob 2151

#define ERROR_SUCCESS                     (0)
#define ERROR_INVALID_FUNCTION		  (1)
#define ERROR_ACCESS_DENIED		  (5)
#define ERROR_INVALID_HANDLE		  (6)
#define ERROR_NOT_ENOUGH_MEMORY		  (8)
#define ERROR_INVALID_PARAMETER		 (87)
#define ERROR_INSUFFICIENT_BUFFER	(122)
#define ERROR_INVALID_NAME		(123)
#define ERROR_INVALID_LEVEL		(124)
#define ERROR_MORE_DATA         (234)
#define ERROR_NO_MORE_ITEMS		(259)
#define ERROR_EAS_DIDNT_FIT		(275) /* Extended attributes didn't fit */
#define ERROR_EAS_NOT_SUPPORTED		(282) /* Extended attributes not supported */
#define ERROR_NOTIFY_ENUM_DIR	       (1022) /* Buffer too small to return change notify. */
#define ERROR_UNKNOWN_PRINTER_DRIVER   (1797)
#define ERROR_INVALID_PRINTER_NAME     (1801)
#define ERROR_INVALID_DATATYPE	       (1804)
#define ERROR_INVALID_ENVIRONMENT      (1805)
#define ERROR_PRINTER_DRIVER_IN_USE    (3001) 

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

#ifndef _PSTRING

#define PSTRING_LEN 1024
#define FSTRING_LEN 256

typedef char pstring[PSTRING_LEN];
typedef char fstring[FSTRING_LEN];

#define _PSTRING

#endif

/*
 * SMB UCS2 (16-bit unicode) internal type.
 */

typedef uint16 smb_ucs2_t;

/* ucs2 string types. */
typedef smb_ucs2_t wpstring[PSTRING_LEN];
typedef smb_ucs2_t wfstring[FSTRING_LEN];

/* pipe string names */
#define PIPE_LANMAN   "\\PIPE\\LANMAN"
#define PIPE_SRVSVC   "\\PIPE\\srvsvc"
#define PIPE_SAMR     "\\PIPE\\samr"
#define PIPE_WINREG   "\\PIPE\\winreg"
#define PIPE_WKSSVC   "\\PIPE\\wkssvc"
#define PIPE_NETLOGON "\\PIPE\\NETLOGON"
#define PIPE_NTLSA    "\\PIPE\\ntlsa"
#define PIPE_NTSVCS   "\\PIPE\\ntsvcs"
#define PIPE_LSASS    "\\PIPE\\lsass"
#define PIPE_LSARPC   "\\PIPE\\lsarpc"
#define PIPE_SPOOLSS  "\\PIPE\\spoolss"
#define PIPE_NETDFS   "\\PIPE\\netdfs"

/* 64 bit time (100usec) since ????? - cifs6.txt, section 3.5, page 30 */
typedef struct nttime_info
{
  uint32 low;
  uint32 high;

} NTTIME;

/* Allowable account control bits */
#define ACB_DISABLED   0x0001  /* 1 = User account disabled */
#define ACB_HOMDIRREQ  0x0002  /* 1 = Home directory required */
#define ACB_PWNOTREQ   0x0004  /* 1 = User password not required */
#define ACB_TEMPDUP    0x0008  /* 1 = Temporary duplicate account */
#define ACB_NORMAL     0x0010  /* 1 = Normal user account */
#define ACB_MNS        0x0020  /* 1 = MNS logon user account */
#define ACB_DOMTRUST   0x0040  /* 1 = Interdomain trust account */
#define ACB_WSTRUST    0x0080  /* 1 = Workstation trust account */
#define ACB_SVRTRUST   0x0100  /* 1 = Server trust account */
#define ACB_PWNOEXP    0x0200  /* 1 = User password does not expire */
#define ACB_AUTOLOCK   0x0400  /* 1 = Account auto locked */
 
#define MAX_HOURS_LEN 32

struct sam_passwd
{
	time_t logon_time;            /* logon time */
	time_t logoff_time;           /* logoff time */
	time_t kickoff_time;          /* kickoff time */
	time_t pass_last_set_time;    /* password last set time */
	time_t pass_can_change_time;  /* password can change time */
	time_t pass_must_change_time; /* password must change time */

	char *smb_name;     /* username string */
	char *full_name;    /* user's full name string */
	char *home_dir;     /* home directory string */
	char *dir_drive;    /* home directory drive string */
	char *logon_script; /* logon script string */
	char *profile_path; /* profile path string */
	char *acct_desc  ;  /* user description string */
	char *workstations; /* login from workstations string */
	char *unknown_str ; /* don't know what this is, yet. */
	char *munged_dial ; /* munged path name and dial-back tel number */

	uid_t smb_userid;       /* this is actually the unix uid_t */
	gid_t smb_grpid;        /* this is actually the unix gid_t */
	uint32 user_rid;      /* Primary User ID */
	uint32 group_rid;     /* Primary Group ID */

	unsigned char *smb_passwd; /* Null if no password */
	unsigned char *smb_nt_passwd; /* Null if no password */

	uint16 acct_ctrl; /* account info (ACB_xxxx bit-mask) */
	uint32 unknown_3; /* 0x00ff ffff */

	uint16 logon_divs; /* 168 - number of hours in a week */
	uint32 hours_len; /* normally 21 bytes */
	uint8 hours[MAX_HOURS_LEN];

	uint32 unknown_5; /* 0x0002 0000 */
	uint32 unknown_6; /* 0x0000 04ec */
};

struct smb_passwd
{
	uid_t smb_userid;     /* this is actually the unix uid_t */
	char *smb_name;     /* username string */

	unsigned char *smb_passwd; /* Null if no password */
	unsigned char *smb_nt_passwd; /* Null if no password */

	uint16 acct_ctrl; /* account info (ACB_xxxx bit-mask) */
	time_t pass_last_set_time;    /* password last set time */
};


struct sam_disp_info
{
	uint32 user_rid;      /* Primary User ID */
	char *smb_name;     /* username string */
	char *full_name;    /* user's full name string */
};

typedef struct
{
        uint32 pid;
        uint16 vuid;

}
vuser_key;


struct use_info
{
	BOOL connected;
	char *srv_name;
	vuser_key key;
	char *user_name;
	char *domain;
};

#ifndef MAXSUBAUTHS
#define MAXSUBAUTHS 15 /* max sub authorities in a SID */
#endif

#ifndef _DOM_SID
/* DOM_SID - security id */
typedef struct sid_info
{
  uint8  sid_rev_num;             /* SID revision number */
  uint8  num_auths;               /* number of sub-authorities */
  uint8  id_auth[6];              /* Identifier Authority */
  /*
   * Note that the values in these uint32's are in *native* byteorder,
   * not neccessarily little-endian...... JRA.
   */
  uint32 sub_auths[MAXSUBAUTHS];  /* pointer to sub-authorities. */

} DOM_SID;
#define _DOM_SID
#endif

/*
 * The complete list of SIDS belonging to this user.
 * Created when a vuid is registered.
 * The definition of the user_sids array is as follows :
 *
 * token->user_sids[0] = primary user SID.
 * token->user_sids[1] = primary group SID.
 * token->user_sids[2-num_sids] = supplementary group SIDS.
 */

#ifndef _NT_USER_TOKEN
typedef struct _nt_user_token {
	size_t num_sids;
	DOM_SID *user_sids;
} NT_USER_TOKEN;
#define _NT_USER_TOKEN
#endif

/*** query a local group, get a list of these: shows who is in that group ***/

/* local group member info */
typedef struct local_grp_member_info
{
	DOM_SID sid    ; /* matches with name */
	uint8   sid_use; /* usr=1 grp=2 dom=3 alias=4 wkng=5 del=6 inv=7 unk=8 */
	fstring name   ; /* matches with sid: must be of the form "DOMAIN\account" */

} LOCAL_GRP_MEMBER;

/* enumerate these to get list of local groups */

/* local group info */
typedef struct local_grp_info
{
	fstring name;
	fstring comment;

} LOCAL_GRP;

/*** enumerate these to get list of domain groups ***/

/* domain group member info */
typedef struct domain_grp_info
{
	fstring name;
	fstring comment;
	uint32  rid; /* group rid */
	uint8   attr; /* attributes forced to be set to 0x7: SE_GROUP_xxx */

} DOMAIN_GRP;

/*** query a domain group, get a list of these: shows who is in that group ***/

/* domain group info */
typedef struct domain_grp_member_info
{
	fstring name;
	uint8   attr; /* attributes forced to be set to 0x7: SE_GROUP_xxx */

} DOMAIN_GRP_MEMBER;

/* 32 bit time (sec) since 01jan1970 - cifs6.txt, section 3.5, page 30 */
typedef struct time_info
{
  uint32 time;
} UTIME;

/* Structure used when SMBwritebmpx is active */
typedef struct
{
  size_t wr_total_written; /* So we know when to discard this */
  int32 wr_timeout;
  int32 wr_errclass;
  int32 wr_error; /* Cached errors */
  BOOL  wr_mode; /* write through mode) */
  BOOL  wr_discard; /* discard all further data */
} write_bmpx_struct;

typedef struct write_cache
{
    SMB_OFF_T file_size;
    SMB_OFF_T offset;
    size_t alloc_size;
    size_t data_size;
    char *data;
} write_cache;

typedef struct files_struct
{
	struct files_struct *next, *prev;
	int fnum;
	struct connection_struct *conn;
	int fd;
	int print_jobid;
	SMB_DEV_T dev;
	SMB_INO_T inode;
	BOOL delete_on_close;
	SMB_OFF_T pos;
	SMB_OFF_T size;
	mode_t mode;
	uint16 vuid;
	write_bmpx_struct *wbmpx_ptr;
	write_cache *wcp;
	struct timeval open_time;
	int share_mode;
	time_t pending_modtime;
	int oplock_type;
	int sent_oplock_break;
	BOOL can_lock;
	BOOL can_read;
	BOOL can_write;
	BOOL print_file;
	BOOL modified;
	BOOL is_directory;
	BOOL directory_delete_on_close;
	BOOL stat_open;
	char *fsp_name;
} files_struct;

/*
 * Structure used to keep directory state information around.
 * Used in NT change-notify code.
 */

typedef struct
{
  time_t modify_time;
  time_t status_time;
} dir_status_struct;

struct uid_cache {
  int entries;
  uid_t list[UID_CACHE_SIZE];
};

typedef struct
{
  char *name;
  BOOL is_wild;
} name_compare_entry;

/* Include VFS stuff */

#include "vfs.h"

typedef struct connection_struct
{
	struct connection_struct *next, *prev;
	unsigned cnum; /* an index passed over the wire */
	int service;
	BOOL force_user;
	struct uid_cache uid_cache;
	void *dirptr;
	BOOL printer;
	BOOL ipc;
	BOOL read_only;
	BOOL admin_user;
	char *dirpath;
	char *connectpath;
	char *origpath;

	struct vfs_ops vfs_ops;                   /* Filesystem operations */
	/* Handle on dlopen() call */
	void *dl_handle;

	char *user; /* name of user who *opened* this connection */
	uid_t uid; /* uid of user who *opened* this connection */
	gid_t gid; /* gid of user who *opened* this connection */
	char client_address[18]; /* String version of client IP address. */

	uint16 vuid; /* vuid of user who *opened* this connection, or UID_FIELD_INVALID */

	/* following groups stuff added by ih */

	/* This groups info is valid for the user that *opened* the connection */
	int ngroups;
	gid_t *groups;
	NT_USER_TOKEN *nt_user_token;
	
	time_t lastused;
	BOOL used;
	int num_files_open;
	name_compare_entry *hide_list; /* Per-share list of files to return as hidden. */
	name_compare_entry *veto_list; /* Per-share list of files to veto (never show). */
	name_compare_entry *veto_oplock_list; /* Per-share list of files to refuse oplocks on. */       

} connection_struct;

struct current_user
{
	connection_struct *conn;
	uint16 vuid;
	uid_t uid;
	gid_t gid;
	int ngroups;
	gid_t *groups;
	NT_USER_TOKEN *nt_user_token;
};

/* Defines for the sent_oplock_break field above. */
#define NO_BREAK_SENT 0
#define EXCLUSIVE_BREAK_SENT 1
#define LEVEL_II_BREAK_SENT 2

typedef struct {
	fstring smb_name; /* user name from the client */
	fstring unix_name; /* unix user name of a validated user */
	fstring full_name; /* to store full name (such as "Joe Bloggs") from gecos field of password file */
	fstring domain; /* domain that the client specified */
} userdom_struct;

/* Extra fields above "LPQ_PRINTING" are used to map extra NT status codes. */

enum {LPQ_QUEUED=0,LPQ_PAUSED,LPQ_SPOOLING,LPQ_PRINTING,LPQ_ERROR,LPQ_DELETING,
      LPQ_OFFLINE,LPQ_PAPEROUT,LPQ_PRINTED,LPQ_DELETED,LPQ_BLOCKED,LPQ_USER_INTERVENTION};

typedef struct _print_queue_struct
{
  int job;
  int size;
  int status;
  int priority;
  time_t time;
  fstring user;
  fstring file;
} print_queue_struct;

enum {LPSTAT_OK, LPSTAT_STOPPED, LPSTAT_ERROR};

typedef struct
{
  fstring message;
  int qcount;
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
	struct interface *next, *prev;
	struct in_addr ip;
	struct in_addr bcast;
	struct in_addr nmask;
};

/* struct returned by get_share_modes */
typedef struct
{
  pid_t pid;
  uint16 op_port;
  uint16 op_type;
  int share_mode;
  struct timeval time;
} share_mode_entry;


#define SHAREMODE_FN_CAST() \
	void (*)(share_mode_entry *, char*)

#define SHAREMODE_FN(fn) \
	void (*fn)(share_mode_entry *, char*)

/*
 * Each implementation of the password database code needs
 * to support the following operations.
 */

struct passdb_ops {
  /*
   * Password database ops.
   */
  void *(*startsmbpwent)(BOOL);
  void (*endsmbpwent)(void *);
  SMB_BIG_UINT (*getsmbpwpos)(void *);
  BOOL (*setsmbpwpos)(void *, SMB_BIG_UINT);

  /*
   * smb password database query functions.
   */
  struct smb_passwd *(*getsmbpwnam)(char *);
  struct smb_passwd *(*getsmbpwuid)(uid_t);
  struct smb_passwd *(*getsmbpwrid)(uint32);
  struct smb_passwd *(*getsmbpwent)(void *);

  /*
   * smb password database modification functions.
   */
  BOOL (*add_smbpwd_entry)(struct smb_passwd *);
  BOOL (*mod_smbpwd_entry)(struct smb_passwd *, BOOL);
  BOOL (*del_smbpwd_entry)(const char *);

  /*
   * Functions that manupulate a struct sam_passwd.
   */
  struct sam_passwd *(*getsam21pwent)(void *);

  /*
   * sam password database query functions.
   */
  struct sam_passwd *(*getsam21pwnam)(char *);
  struct sam_passwd *(*getsam21pwuid)(uid_t);
  struct sam_passwd *(*getsam21pwrid)(uint32);

  /*
   * sam password database modification functions.
   */
  BOOL (*add_sam21pwd_entry)(struct sam_passwd *);
  BOOL (*mod_sam21pwd_entry)(struct sam_passwd *, BOOL);

  /*
   * sam query display info functions.
   */
  struct sam_disp_info *(*getsamdispnam)(char *);
  struct sam_disp_info *(*getsamdisprid)(uint32);
  struct sam_disp_info *(*getsamdispent)(void *);

#if 0
  /*
   * password checking functions
   */
  struct smb_passwd *(*smb_password_chal  )(char *username, char lm_pass[24], char nt_pass[24], char chal[8]);
  struct smb_passwd *(*smb_password_check )(char *username, char lm_hash[16], char nt_hash[16]);
  struct passwd     *(*unix_password_check)(char *username, char *pass, int pass_len);
#endif
};

/*
 * Flags for local user manipulation.
 */

#define LOCAL_ADD_USER 0x1
#define LOCAL_DELETE_USER 0x2
#define LOCAL_DISABLE_USER 0x4
#define LOCAL_ENABLE_USER 0x8
#define LOCAL_TRUST_ACCOUNT 0x10
#define LOCAL_SET_NO_PASSWORD 0x20

/* key and data in the connections database - used in smbstatus and smbd */
struct connections_key {
	pid_t pid;
	int cnum;
	fstring name;
};

struct connections_data {
	int magic;
	pid_t pid;
	int cnum;
	uid_t uid;
	gid_t gid;
	char name[24];
	char addr[24];
	char machine[FSTRING_LEN];
	time_t start;
};


/* key and data records in the tdb locking database */
struct locking_key {
	SMB_DEV_T dev;
	SMB_INO_T inode;
};

struct locking_data {
	int num_share_mode_entries;
	/* the following two entries are implicit 
	   share_mode_entry modes[num_share_mode_entries];
           char file_name[];
	*/
};


/* the following are used by loadparm for option lists */
typedef enum
{
  P_BOOL,P_BOOLREV,P_CHAR,P_INTEGER,P_OCTAL,
  P_STRING,P_USTRING,P_GSTRING,P_UGSTRING,P_ENUM,P_SEP
} parm_type;

typedef enum
{
  P_LOCAL,P_GLOBAL,P_SEPARATOR,P_NONE
} parm_class;

/* passed to br lock code */
enum brl_type {READ_LOCK, WRITE_LOCK};

struct enum_list {
	int value;
	char *name;
};

#define BRLOCK_FN_CAST() \
	void (*)(SMB_DEV_T dev, SMB_INO_T ino, int pid, \
				 enum brl_type lock_type, \
				 br_off start, br_off size)
#define BRLOCK_FN(fn) \
	void (*fn)(SMB_DEV_T dev, SMB_INO_T ino, int pid, \
				 enum brl_type lock_type, \
				 br_off start, br_off size)
struct parm_struct
{
	char *label;
	parm_type type;
	parm_class class;
	void *ptr;
	BOOL (*special)(char *, char **);
	struct enum_list *enum_list;
	unsigned flags;
	union {
		BOOL bvalue;
		int ivalue;
		char *svalue;
		char cvalue;
	} def;
};

struct bitmap {
	uint32 *b;
	int n;
};

#define FLAG_BASIC 	0x01 /* fundamental options */
#define FLAG_SHARE 	0x02 /* file sharing options */
#define FLAG_PRINT 	0x04 /* printing options */
#define FLAG_GLOBAL 	0x08 /* local options that should be globally settable in SWAT */
#define FLAG_DEPRECATED 0x10 /* options that should no longer be used */
#define FLAG_HIDE  	0x20 /* options that should be hidden in SWAT */
#define FLAG_DOS_STRING 0x40 /* convert from UNIX to DOS codepage when reading this string. */

#ifndef LOCKING_VERSION
#define LOCKING_VERSION 4
#endif /* LOCKING_VERSION */


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

/* flag defines. CIFS spec 3.1.1 */
#define FLAG_SUPPORT_LOCKREAD       0x01
#define FLAG_CLIENT_BUF_AVAIL       0x02
#define FLAG_RESERVED               0x04
#define FLAG_CASELESS_PATHNAMES     0x08
#define FLAG_CANONICAL_PATHNAMES    0x10
#define FLAG_REQUEST_OPLOCK         0x20
#define FLAG_REQUEST_BATCH_OPLOCK   0x40
#define FLAG_REPLY                  0x80

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

/* NT SMB extensions. */
#define SMBnttrans       0xA0   /* NT transact */
#define SMBnttranss      0xA1   /* NT transact secondary */
#define SMBntcreateX     0xA2   /* NT create and X */
#define SMBntcancel      0xA4   /* NT cancel */

/* These are the TRANS2 sub commands */
#define TRANSACT2_OPEN                        0
#define TRANSACT2_FINDFIRST                   1
#define TRANSACT2_FINDNEXT                    2
#define TRANSACT2_QFSINFO                     3
#define TRANSACT2_SETFSINFO                   4
#define TRANSACT2_QPATHINFO                   5
#define TRANSACT2_SETPATHINFO                 6
#define TRANSACT2_QFILEINFO                   7
#define TRANSACT2_SETFILEINFO                 8
#define TRANSACT2_FSCTL                       9
#define TRANSACT2_IOCTL                     0xA
#define TRANSACT2_FINDNOTIFYFIRST           0xB
#define TRANSACT2_FINDNOTIFYNEXT            0xC
#define TRANSACT2_MKDIR                     0xD
#define TRANSACT2_SESSION_SETUP             0xE
#define TRANSACT2_GET_DFS_REFERRAL         0x10
#define TRANSACT2_REPORT_DFS_INCONSISTANCY 0x11

/* These are the NT transact sub commands. */
#define NT_TRANSACT_CREATE                1
#define NT_TRANSACT_IOCTL                 2
#define NT_TRANSACT_SET_SECURITY_DESC     3
#define NT_TRANSACT_NOTIFY_CHANGE         4
#define NT_TRANSACT_RENAME                5
#define NT_TRANSACT_QUERY_SECURITY_DESC   6

/* Relevant IOCTL codes */
#define IOCTL_QUERY_JOB_INFO      0x530060

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

/* these are for the NT trans primary request. */
#define smb_nt_MaxSetupCount smb_vwv0
#define smb_nt_Flags (smb_vwv0 + 1)
#define smb_nt_TotalParameterCount (smb_vwv0 + 3)
#define smb_nt_TotalDataCount (smb_vwv0 + 7)
#define smb_nt_MaxParameterCount (smb_vwv0 + 11)
#define smb_nt_MaxDataCount (smb_vwv0 + 15)
#define smb_nt_ParameterCount (smb_vwv0 + 19)
#define smb_nt_ParameterOffset (smb_vwv0 + 23)
#define smb_nt_DataCount (smb_vwv0 + 27)
#define smb_nt_DataOffset (smb_vwv0 + 31)
#define smb_nt_SetupCount (smb_vwv0 + 35)
#define smb_nt_Function (smb_vwv0 + 36)
#define smb_nt_SetupStart (smb_vwv0 + 38)

/* these are for the NT trans secondary request. */
#define smb_nts_TotalParameterCount (smb_vwv0 + 3)
#define smb_nts_TotalDataCount (smb_vwv0 + 7)
#define smb_nts_ParameterCount (smb_vwv0 + 11)
#define smb_nts_ParameterOffset (smb_vwv0 + 15)
#define smb_nts_ParameterDisplacement (smb_vwv0 + 19)
#define smb_nts_DataCount (smb_vwv0 + 23)
#define smb_nts_DataOffset (smb_vwv0 + 27)
#define smb_nts_DataDisplacement (smb_vwv0 + 31)

/* these are for the NT trans reply. */
#define smb_ntr_TotalParameterCount (smb_vwv0 + 3)
#define smb_ntr_TotalDataCount (smb_vwv0 + 7)
#define smb_ntr_ParameterCount (smb_vwv0 + 11)
#define smb_ntr_ParameterOffset (smb_vwv0 + 15)
#define smb_ntr_ParameterDisplacement (smb_vwv0 + 19)
#define smb_ntr_DataCount (smb_vwv0 + 23)
#define smb_ntr_DataOffset (smb_vwv0 + 27)
#define smb_ntr_DataDisplacement (smb_vwv0 + 31)

/* these are for the NT create_and_X */
#define smb_ntcreate_NameLength (smb_vwv0 + 5)
#define smb_ntcreate_Flags (smb_vwv0 + 7)
#define smb_ntcreate_RootDirectoryFid (smb_vwv0 + 11)
#define smb_ntcreate_DesiredAccess (smb_vwv0 + 15)
#define smb_ntcreate_AllocationSize (smb_vwv0 + 19)
#define smb_ntcreate_FileAttributes (smb_vwv0 + 27)
#define smb_ntcreate_ShareAccess (smb_vwv0 + 31)
#define smb_ntcreate_CreateDisposition (smb_vwv0 + 35)
#define smb_ntcreate_CreateOptions (smb_vwv0 + 39)
#define smb_ntcreate_ImpersonationLevel (smb_vwv0 + 43)
#define smb_ntcreate_SecurityFlags (smb_vwv0 + 47)

/* this is used on a TConX. I'm not sure the name is very helpful though */
#define SMB_SUPPORT_SEARCH_BITS        0x0001
#define SMB_SHARE_IN_DFS               0x0002

/* Named pipe write mode flags. Used in writeX calls. */
#define PIPE_RAW_MODE 0x4
#define PIPE_START_MESSAGE 0x8

/* these are the constants used in the above call. */
/* DesiredAccess */
/* File Specific access rights. */
#define FILE_READ_DATA        0x001
#define FILE_WRITE_DATA       0x002
#define FILE_APPEND_DATA      0x004
#define FILE_READ_EA          0x008
#define FILE_WRITE_EA         0x010
#define FILE_EXECUTE          0x020
#define FILE_DELETE_CHILD     0x040
#define FILE_READ_ATTRIBUTES  0x080
#define FILE_WRITE_ATTRIBUTES 0x100

#define FILE_ALL_ACCESS       0x1FF

/* the desired access to use when opening a pipe */
#define DESIRED_ACCESS_PIPE 0x2019f
 
/* Generic access masks & rights. */
#define SPECIFIC_RIGHTS_MASK 0x00FFFFL
#define STANDARD_RIGHTS_MASK 0xFF0000L
#define DELETE_ACCESS        (1L<<16)
#define READ_CONTROL_ACCESS  (1L<<17)
#define WRITE_DAC_ACCESS     (1L<<18)
#define WRITE_OWNER_ACCESS   (1L<<19)
#define SYNCHRONIZE_ACCESS   (1L<<20)

/* Combinations of standard masks. */
#define STANDARD_RIGHTS_ALL_ACCESS (DELETE_ACCESS|READ_CONTROL_ACCESS|WRITE_DAC_ACCESS|WRITE_OWNER_ACCESS|SYNCHRONIZE_ACCESS)
#define STANDARD_RIGHTS_EXECUTE_ACCESS (READ_CONTROL_ACCESS)
#define STANDARD_RIGHTS_READ_ACCESS (READ_CONTROL_ACCESS)
#define STANDARD_RIGHTS_REQUIRED_ACCESS (DELETE_ACCESS|READ_CONTROL_ACCESS|WRITE_DAC_ACCESS|WRITE_OWNER_ACCESS)
#define STANDARD_RIGHTS_WRITE_ACCESS (READ_CONTROL_ACCESS)

#define SYSTEM_SECURITY_ACCESS (1L<<24)
#define MAXIMUM_ALLOWED_ACCESS (1L<<25)
#define GENERIC_ALL_ACCESS   (1<<28)
#define GENERIC_EXECUTE_ACCESS  (1<<29)
#define GENERIC_WRITE_ACCESS   (1<<30)
#define GENERIC_READ_ACCESS   (((unsigned)1)<<31)

/* Mapping of generic access rights for files to specific rights. */

#define FILE_GENERIC_ALL (STANDARD_RIGHTS_REQUIRED_ACCESS| SYNCHRONIZE_ACCESS|FILE_ALL_ACCESS)

#define FILE_GENERIC_READ (STANDARD_RIGHTS_READ_ACCESS|FILE_READ_DATA|FILE_READ_ATTRIBUTES|\
							FILE_READ_EA|SYNCHRONIZE_ACCESS)

#define FILE_GENERIC_WRITE (STANDARD_RIGHTS_WRITE_ACCESS|FILE_WRITE_DATA|FILE_WRITE_ATTRIBUTES|\
							FILE_WRITE_EA|FILE_APPEND_DATA|SYNCHRONIZE_ACCESS)

#define FILE_GENERIC_EXECUTE (STANDARD_RIGHTS_EXECUTE_ACCESS|FILE_READ_ATTRIBUTES|\
								FILE_EXECUTE|SYNCHRONIZE_ACCESS)

/* Mapping of access rights to UNIX perms. */
#define UNIX_ACCESS_RWX (UNIX_ACCESS_R|UNIX_ACCESS_W|UNIX_ACCESS_X)
#define UNIX_ACCESS_R (READ_CONTROL_ACCESS|SYNCHRONIZE_ACCESS|\
			FILE_READ_ATTRIBUTES|FILE_READ_EA|FILE_READ_DATA)
#define UNIX_ACCESS_W (READ_CONTROL_ACCESS|SYNCHRONIZE_ACCESS|\
			FILE_WRITE_ATTRIBUTES|FILE_WRITE_EA|\
			FILE_APPEND_DATA|FILE_WRITE_DATA)
#define UNIX_ACCESS_X (READ_CONTROL_ACCESS|SYNCHRONIZE_ACCESS|\
			FILE_EXECUTE|FILE_READ_ATTRIBUTES)

#define UNIX_ACCESS_NONE (WRITE_OWNER_ACCESS)

/* Flags field. */
#define REQUEST_OPLOCK 2
#define REQUEST_BATCH_OPLOCK 4
#define OPEN_DIRECTORY 8

/* ShareAccess field. */
#define FILE_SHARE_NONE 0 /* Cannot be used in bitmask. */
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define FILE_SHARE_DELETE 4

/* FileAttributesField */
#define FILE_ATTRIBUTE_READONLY aRONLY
#define FILE_ATTRIBUTE_HIDDEN aHIDDEN
#define FILE_ATTRIBUTE_SYSTEM aSYSTEM
#define FILE_ATTRIBUTE_DIRECTORY aDIR
#define FILE_ATTRIBUTE_ARCHIVE aARCH
#define FILE_ATTRIBUTE_NORMAL 0x80L
#define FILE_ATTRIBUTE_TEMPORARY 0x100L
#define FILE_ATTRIBUTE_COMPRESSED 0x800L
#define SAMBA_ATTRIBUTES_MASK 0x7F

/* Flags - combined with attributes. */
#define FILE_FLAG_WRITE_THROUGH    0x80000000L
#define FILE_FLAG_NO_BUFFERING     0x20000000L
#define FILE_FLAG_RANDOM_ACCESS    0x10000000L
#define FILE_FLAG_SEQUENTIAL_SCAN  0x08000000L
#define FILE_FLAG_DELETE_ON_CLOSE  0x04000000L
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000L
#define FILE_FLAG_POSIX_SEMANTICS  0x01000000L

/* CreateDisposition field. */
#define FILE_SUPERSEDE 0
#define FILE_OPEN 1
#define FILE_CREATE 2
#define FILE_OPEN_IF 3
#define FILE_OVERWRITE 4
#define FILE_OVERWRITE_IF 5

/* CreateOptions field. */
#define FILE_DIRECTORY_FILE       0x0001
#define FILE_WRITE_THROUGH        0x0002
#define FILE_SEQUENTIAL_ONLY      0x0004
#define FILE_NON_DIRECTORY_FILE   0x0040
#define FILE_NO_EA_KNOWLEDGE      0x0200
#define FILE_EIGHT_DOT_THREE_ONLY 0x0400
#define FILE_RANDOM_ACCESS        0x0800
#define FILE_DELETE_ON_CLOSE      0x1000

/* Responses when opening a file. */
#define FILE_WAS_OPENED 1
#define FILE_WAS_CREATED 2
#define FILE_WAS_OVERWRITTEN 3

/* File type flags */
#define FILE_TYPE_DISK  0
#define FILE_TYPE_BYTE_MODE_PIPE 1
#define FILE_TYPE_MESSAGE_MODE_PIPE 2
#define FILE_TYPE_PRINTER 3
#define FILE_TYPE_COMM_DEVICE 4
#define FILE_TYPE_UNKNOWN 0xFFFF

/* Flag for NT transact rename call. */
#define RENAME_REPLACE_IF_EXISTS 1

/* Filesystem Attributes. */
#define FILE_CASE_SENSITIVE_SEARCH 0x01
#define FILE_CASE_PRESERVED_NAMES 0x02
#define FILE_UNICODE_ON_DISK 0x04
/* According to cifs9f, this is 4, not 8 */
/* Acconding to testing, this actually sets the security attribute! */
#define FILE_PERSISTENT_ACLS 0x08
/* These entries added from cifs9f --tsb */
#define FILE_FILE_COMPRESSION 0x08
#define FILE_VOLUME_QUOTAS 0x10
#define FILE_DEVICE_IS_MOUNTED 0x20
#define FILE_VOLUME_IS_COMPRESSED 0x8000

/* ChangeNotify flags. */
#define FILE_NOTIFY_CHANGE_FILE        0x001
#define FILE_NOTIFY_CHANGE_DIR_NAME    0x002
#define FILE_NOTIFY_CHANGE_ATTRIBUTES  0x004
#define FILE_NOTIFY_CHANGE_SIZE        0x008
#define FILE_NOTIFY_CHANGE_LAST_WRITE  0x010
#define FILE_NOTIFY_CHANGE_LAST_ACCESS 0x020
#define FILE_NOTIFY_CHANGE_CREATION    0x040
#define FILE_NOTIFY_CHANGE_EA          0x080
#define FILE_NOTIFY_CHANGE_SECURITY    0x100
#define FILE_NOTIFY_CHANGE_FILE_NAME   0x200

/* where to find the base of the SMB packet proper */
#define smb_base(buf) (((char *)(buf))+4)

/* we don't allow server strings to be longer than 48 characters as
   otherwise NT will not honour the announce packets */
#define MAX_SERVER_STRING_LENGTH 48


#define SMB_SUCCESS 0  /* The request was successful. */
#define ERRDOS 0x01 /*  Error is from the core DOS operating system set. */
#define ERRSRV 0x02  /* Error is generated by the server network file manager.*/
#define ERRHRD 0x03  /* Error is an hardware error. */
#define ERRCMD 0xFF  /* Command was not in the "SMB" format. */

#ifdef HAVE_STDARG_H
int slprintf(char *str, int n, char *format, ...)
#ifdef __GNUC__
     __attribute__ ((format (__printf__, 3, 4)))
#endif
;
#else
int slprintf();
#endif

#ifdef HAVE_STDARG_H
int fdprintf(int fd, char *format, ...)
#ifdef __GNUC__
     __attribute__ ((format (__printf__, 2, 3)))
#endif
;
#else
int fdprintf();
#endif

#ifdef WITH_DFS
void dfs_unlogin(void);
extern int dcelogin_atmost_once;
#endif

#ifdef NOSTRDUP
char *strdup(char *s);
#endif

#ifndef SIGNAL_CAST
#define SIGNAL_CAST (RETSIGTYPE (*)(int))
#endif

#ifndef SELECT_CAST
#define SELECT_CAST
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
#define SV_TYPE_DFS_SERVER	    0x00800000
#define SV_TYPE_ALTERNATE_XPORT     0x20000000  
#define SV_TYPE_LOCAL_LIST_ONLY     0x40000000  
#define SV_TYPE_DOMAIN_ENUM         0x80000000
#define SV_TYPE_ALL                 0xFFFFFFFF  

/* This was set by JHT in liaison with Jeremy Allison early 1997
 * History:
 * Version 4.0 - never made public
 * Version 4.10 - New to 1.9.16p2, lost in space 1.9.16p3 to 1.9.16p9
 *		- Reappeared in 1.9.16p11 with fixed smbd services
 * Version 4.20 - To indicate that nmbd and browsing now works better
 * Version 4.50 - Set at release of samba-2.2.0 by JHT
 *
 *  Note: In the presence of NT4.X do not set above 4.9
 *        Setting this above 4.9 can have undesired side-effects.
 *        This may change again in Samba-3.0 after further testing. JHT
 */

#define DEFAULT_MAJOR_VERSION 0x04
#define DEFAULT_MINOR_VERSION 0x05

/* Browser Election Values */
#define BROWSER_ELECTION_VERSION	0x010f
#define BROWSER_CONSTANT	0xaa55

/* NT Flags2 bits - cifs6.txt section 3.1.2 */
   
#define FLAGS2_LONG_PATH_COMPONENTS   0x0001
#define FLAGS2_EXTENDED_ATTRIBUTES    0x0002
#define FLAGS2_DFS_PATHNAMES          0x1000
#define FLAGS2_READ_PERMIT_NO_EXECUTE 0x2000
#define FLAGS2_32_BIT_ERROR_CODES     0x4000 
#define FLAGS2_UNICODE_STRINGS        0x8000

#define FLAGS2_WIN2K_SIGNATURE        0xC852 /* Hack alert ! For now... JRA. */

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
#define CAP_EXTENDED_SECURITY 0x80000000

/* protocol types. It assumes that higher protocols include lower protocols
   as subsets */
enum protocol_types {PROTOCOL_NONE,PROTOCOL_CORE,PROTOCOL_COREPLUS,PROTOCOL_LANMAN1,PROTOCOL_LANMAN2,PROTOCOL_NT1};

/* security levels */
enum security_types {SEC_SHARE,SEC_USER,SEC_SERVER,SEC_DOMAIN};

/* server roles */
enum server_types
{
	ROLE_STANDALONE,
	ROLE_DOMAIN_MEMBER,
	ROLE_DOMAIN_BDC,
	ROLE_DOMAIN_PDC
};

/* printing types */
enum printing_types {PRINT_BSD,PRINT_SYSV,PRINT_AIX,PRINT_HPUX,
		     PRINT_QNX,PRINT_PLP,PRINT_LPRNG,PRINT_SOFTQ,
		     PRINT_CUPS,PRINT_LPRNT,PRINT_LPROS2
#ifdef DEVELOPER
,PRINT_TEST,PRINT_VLP
#endif /* DEVELOPER */
};

/* Remote architectures we know about. */
enum remote_arch_types {RA_UNKNOWN, RA_WFWG, RA_OS2, RA_WIN95, RA_WINNT, RA_WIN2K, RA_SAMBA};

/* case handling */
enum case_handling {CASE_LOWER,CASE_UPPER};

#ifdef WITH_SSL
/* SSL version options */
enum ssl_version_enum {SMB_SSL_V2,SMB_SSL_V3,SMB_SSL_V23,SMB_SSL_TLS1};
#endif /* WITH_SSL */

/*
 * Global value meaing that the smb_uid field should be
 * ingored (in share level security and protocol level == CORE)
 */

#define UID_FIELD_INVALID 0
#define VUID_OFFSET 100 /* Amount to bias returned vuid numbers */

/* Defines needed for multi-codepage support. */
#define MSDOS_LATIN_1_CODEPAGE 850
#define KANJI_CODEPAGE 932
#define HANGUL_CODEPAGE 949
#define BIG5_CODEPAGE 950
#define SIMPLIFIED_CHINESE_CODEPAGE 936

#ifdef KANJI
/* 
 * Default client code page - Japanese 
 */
#define DEFAULT_CLIENT_CODE_PAGE KANJI_CODEPAGE
#else /* KANJI */
/* 
 * Default client code page - 850 - Western European 
 */
#define DEFAULT_CLIENT_CODE_PAGE MSDOS_LATIN_1_CODEPAGE
#endif /* KANJI */

/* Global val set if multibyte codepage. */
extern int global_is_multibyte_codepage;

#define get_character_len(x) (global_is_multibyte_codepage ? skip_multibyte_char((x)) : 0)

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
 * Used in chaining code.
 */
extern int chain_size;

/*
 * Map the Core and Extended Oplock requesst bits down
 * to common bits (EXCLUSIVE_OPLOCK & BATCH_OPLOCK).
 */

/*
 * Core protocol.
 */
#define CORE_OPLOCK_REQUEST(inbuf) \
    ((CVAL(inbuf,smb_flg)&(FLAG_REQUEST_OPLOCK|FLAG_REQUEST_BATCH_OPLOCK))>>5)

/*
 * Extended protocol.
 */
#define EXTENDED_OPLOCK_REQUEST(inbuf) ((SVAL(inbuf,smb_vwv2)&((1<<1)|(1<<2)))>>1)

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

#define NO_OPLOCK 0
#define EXCLUSIVE_OPLOCK 1
#define BATCH_OPLOCK 2
#define LEVEL_II_OPLOCK 4

#define EXCLUSIVE_OPLOCK_TYPE(lck) ((lck) & (EXCLUSIVE_OPLOCK|BATCH_OPLOCK))
#define BATCH_OPLOCK_TYPE(lck) ((lck) & BATCH_OPLOCK)
#define LEVEL_II_OPLOCK_TYPE(lck) ((lck) & LEVEL_II_OPLOCK)

#define CORE_OPLOCK_GRANTED (1<<5)
#define EXTENDED_OPLOCK_GRANTED (1<<15)

/*
 * Return values for oplock types.
 */

#define NO_OPLOCK_RETURN 0
#define EXCLUSIVE_OPLOCK_RETURN 1
#define BATCH_OPLOCK_RETURN 2
#define LEVEL_II_OPLOCK_RETURN 3

/*
 * Loopback command offsets.
 */

#define OPBRK_CMD_LEN_OFFSET 0
#define OPBRK_CMD_PORT_OFFSET 4
#define OPBRK_CMD_HEADER_LEN 6

#define OPBRK_MESSAGE_CMD_OFFSET 0

/*
 * Oplock break command code to send over the udp socket.
 * The same message is sent for both exlusive and level II breaks. 
 * 
 * The form of this is :
 *
 *  0     2       6        10       14    14+devsize 14+devsize+inodesize
 *  +----+--------+--------+--------+-------+--------+
 *  | cmd| pid    | sec    | usec   | dev   |  inode |
 *  +----+--------+--------+--------+-------+--------+
 */

#define OPLOCK_BREAK_CMD 0x1
#define OPLOCK_BREAK_PID_OFFSET 2
#define OPLOCK_BREAK_SEC_OFFSET (OPLOCK_BREAK_PID_OFFSET + sizeof(pid_t))
#define OPLOCK_BREAK_USEC_OFFSET (OPLOCK_BREAK_SEC_OFFSET + sizeof(time_t))
#define OPLOCK_BREAK_DEV_OFFSET (OPLOCK_BREAK_USEC_OFFSET + sizeof(long))
#define OPLOCK_BREAK_INODE_OFFSET (OPLOCK_BREAK_DEV_OFFSET + sizeof(SMB_DEV_T))
#define OPLOCK_BREAK_MSG_LEN (OPLOCK_BREAK_INODE_OFFSET + sizeof(SMB_INO_T))

#define KERNEL_OPLOCK_BREAK_CMD 0x2
#define LEVEL_II_OPLOCK_BREAK_CMD 0x3

/*
 * Capabilities abstracted for different systems.
 */

#define KERNEL_OPLOCK_CAPABILITY 0x1

/*
 * Oplock break command code sent via the kernel interface (if it exists).
 *
 * Form of this is :
 *
 *  0     2       2+devsize 2+devsize+inodesize
 *  +----+--------+--------+
 *  | cmd| dev    |  inode |
 *  +----+--------+--------+
 */
#define KERNEL_OPLOCK_BREAK_DEV_OFFSET 2
#define KERNEL_OPLOCK_BREAK_INODE_OFFSET (KERNEL_OPLOCK_BREAK_DEV_OFFSET + sizeof(SMB_DEV_T))
#define KERNEL_OPLOCK_BREAK_MSG_LEN (KERNEL_OPLOCK_BREAK_INODE_OFFSET + sizeof(SMB_INO_T))


/* if a kernel does support oplocks then a structure of the following
   typee is used to describe how to interact with the kernel */
struct kernel_oplocks {
	BOOL (*receive_message)(fd_set *fds, char *buffer, int buffer_len);
	BOOL (*set_oplock)(files_struct *fsp, int oplock_type);
	void (*release_oplock)(files_struct *fsp);
	BOOL (*parse_message)(char *msg_start, int msg_len, SMB_INO_T *inode, SMB_DEV_T *dev);
	BOOL (*msg_waiting)(fd_set *fds);
	int notification_fd;
};


#define CMD_REPLY 0x8000

/* this structure defines the functions for doing change notify in
   various implementations */
struct cnotify_fns {
	void * (*register_notify)(connection_struct *conn, char *path, uint32 flags);
	BOOL (*check_notify)(connection_struct *conn, uint16 vuid, char *path, uint32 flags, void *data, time_t t);
	void (*remove_notify)(void *data);
	int select_time;
};



#include "smb_macros.h"

/* A netbios name structure. */
struct nmb_name {
  char         name[17];
  char         scope[64];
  unsigned int name_type;
};


/* A netbios node status array element. */
struct node_status {
	char name[16];
	unsigned char type;
	unsigned char flags;
};



#define AGENT_CMD_CON       0
#define AGENT_CMD_CON_ANON  2
#define AGENT_CMD_CON_REUSE 1

struct pwd_info
{
	BOOL null_pwd;
	BOOL cleartext;
	BOOL crypted;

	fstring password;

	uchar smb_lm_pwd[16];
	uchar smb_nt_pwd[16];

	uchar smb_lm_owf[24];
	uchar smb_nt_owf[128];
	uint32 nt_owf_len;

	uchar lm_cli_chal[8];
	uchar nt_cli_chal[128];
	uint32 nt_cli_chal_len;

	uchar sess_key[16];
};

/*
 * Network Computing Architechture Context Name Named Pipe
 * See MSDN docs for more information
 */
struct ncacn_np
{
        fstring pipe_name;
        struct cli_state *smb;
        uint16 fnum;
        BOOL initialised;
};

#include "rpc_creds.h"
#include "rpc_misc.h"
#include "rpc_secdes.h"
#include "nt_printing.h"

typedef struct user_struct
{
	struct user_struct *next, *prev;
	uint16 vuid; /* Tag for this entry. */
	uid_t uid; /* uid of a validated user */
	gid_t gid; /* gid of a validated user */

	userdom_struct user;
	BOOL guest;

	/* following groups stuff added by ih */
	/* This groups info is needed for when we become_user() for this uid */
	int n_groups;
	gid_t *groups;

	NT_USER_TOKEN *nt_user_token;

	int session_id; /* used by utmp and pam session code */
} user_struct;

#include "ntdomain.h"

#include "client.h"
#include "rpcclient.h"

/*
 * Size of new password account encoding string. DO NOT CHANGE.
 */

#define NEW_PW_FORMAT_SPACE_PADDED_LEN 14

/*
   Do you want session setups at user level security with a invalid
   password to be rejected or allowed in as guest? WinNT rejects them
   but it can be a pain as it means "net view" needs to use a password

   You have 3 choices in the setting of map_to_guest:

   "NEVER_MAP_TO_GUEST" means session setups with an invalid password
   are rejected. This is the default.

   "MAP_TO_GUEST_ON_BAD_USER" means session setups with an invalid password
   are rejected, unless the username does not exist, in which case it
   is treated as a guest login

   "MAP_TO_GUEST_ON_BAD_PASSWORD" means session setups with an invalid password
   are treated as a guest login

   Note that map_to_guest only has an effect in user or server
   level security.
*/

#define NEVER_MAP_TO_GUEST 0
#define MAP_TO_GUEST_ON_BAD_USER 1
#define MAP_TO_GUEST_ON_BAD_PASSWORD 2

#include "nsswitch/winbindd_nss.h"
#include "smb_acls.h"

#endif /* _SMB_H */
