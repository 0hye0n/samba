/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   Samba utility functions
   Copyright (C) Andrew Tridgell 1992-1998
   
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

#if (defined(HAVE_NETGROUP) && defined (WITH_AUTOMOUNT))
#ifdef WITH_NISPLUS_HOME
#ifdef BROKEN_NISPLUS_INCLUDE_FILES
/*
 * The following lines are needed due to buggy include files
 * in Solaris 2.6 which define GROUP in both /usr/include/sys/acl.h and
 * also in /usr/include/rpcsvc/nis.h. The definitions conflict. JRA.
 * Also GROUP_OBJ is defined as 0x4 in /usr/include/sys/acl.h and as
 * an enum in /usr/include/rpcsvc/nis.h.
 */

#if defined(GROUP)
#undef GROUP
#endif

#if defined(GROUP_OBJ)
#undef GROUP_OBJ
#endif

#endif /* BROKEN_NISPLUS_INCLUDE_FILES */

#include <rpcsvc/nis.h>

#else /* !WITH_NISPLUS_HOME */

#include "rpcsvc/ypclnt.h"

#endif /* WITH_NISPLUS_HOME */
#endif /* HAVE_NETGROUP && WITH_AUTOMOUNT */

#ifdef WITH_SSL
#include <ssl.h>
#undef Realloc  /* SSLeay defines this and samba has a function of this name */
extern SSL  *ssl;
extern int  sslFd;
#endif  /* WITH_SSL */

extern int DEBUGLEVEL;

int Protocol = PROTOCOL_COREPLUS;

/* a default finfo structure to ensure all fields are sensible */
file_info def_finfo = {-1,0,0,0,0,0,0,"",""};

/* this is used by the chaining code */
int chain_size = 0;

int trans_num = 0;

/*
   case handling on filenames 
*/
int case_default = CASE_LOWER;

/* the following control case operations - they are put here so the
   client can link easily */
BOOL case_sensitive;
BOOL case_preserve;
BOOL use_mangled_map = False;
BOOL short_case_preserve;
BOOL case_mangle;

static enum remote_arch_types ra_type = RA_UNKNOWN;
pstring user_socket_options=DEFAULT_SOCKET_OPTIONS;   

pstring global_myname = "";
fstring global_myworkgroup = "";
char **my_netbios_names;


/****************************************************************************
  find a suitable temporary directory. The result should be copied immediately
  as it may be overwritten by a subsequent call
  ****************************************************************************/
char *tmpdir(void)
{
  char *p;
  if ((p = getenv("TMPDIR"))) {
    return p;
  }
  return "/tmp";
}

/****************************************************************************
determine whether we are in the specified group
****************************************************************************/

BOOL in_group(gid_t group, gid_t current_gid, int ngroups, gid_t *groups)
{
	int i;

	if (group == current_gid) return(True);

	for (i=0;i<ngroups;i++)
		if (group == groups[i])
			return(True);

	return(False);
}


/****************************************************************************
like atoi but gets the value up to the separater character
****************************************************************************/
char *Atoic(char *p, int *n, char *c)
{
	if (!isdigit((int)*p))
	{
		DEBUG(5, ("Atoic: malformed number\n"));
		return NULL;
	}

	(*n) = atoi(p);

	while ((*p) && isdigit((int)*p))
	{
		p++;
	}

	if (strchr(c, *p) == NULL)
	{
		DEBUG(5, ("Atoic: no separator characters (%s) not found\n", c));
		return NULL;
	}

	return p;
}

/*************************************************************************
 reads a list of numbers
 *************************************************************************/
char *get_numlist(char *p, uint32 **num, int *count)
{
	int val;

	if (num == NULL || count == NULL)
	{
		return NULL;
	}

	(*count) = 0;
	(*num  ) = NULL;

	while ((p = Atoic(p, &val, ":,")) != NULL && (*p) != ':')
	{
		(*num) = Realloc((*num), ((*count)+1) * sizeof(uint32));
		if ((*num) == NULL)
		{
			return NULL;
		}
		(*num)[(*count)] = val;
		(*count)++;
		p++;
	}

	return p;
}


/*******************************************************************
  check if a file exists - call vfs_file_exist for samba files
********************************************************************/
BOOL file_exist(char *fname,SMB_STRUCT_STAT *sbuf)
{
  SMB_STRUCT_STAT st;
  if (!sbuf) sbuf = &st;
  
  if (sys_stat(fname,sbuf) != 0) 
    return(False);

  return(S_ISREG(sbuf->st_mode));
}

/*******************************************************************
  rename a unix file
********************************************************************/
int file_rename(char *from, char *to)
{
	int rcode = rename (from, to);

	if (errno == EXDEV) 
	{
		/* Rename across filesystems needed. */
		rcode = copy_reg (from, to);        
	}
	return rcode;
}

/*******************************************************************
check a files mod time
********************************************************************/
time_t file_modtime(char *fname)
{
  SMB_STRUCT_STAT st;
  
  if (sys_stat(fname,&st) != 0) 
    return(0);

  return(st.st_mtime);
}

/*******************************************************************
  check if a directory exists
********************************************************************/
BOOL directory_exist(char *dname,SMB_STRUCT_STAT *st)
{
  SMB_STRUCT_STAT st2;
  BOOL ret;

  if (!st) st = &st2;

  if (sys_stat(dname,st) != 0) 
    return(False);

  ret = S_ISDIR(st->st_mode);
  if(!ret)
    errno = ENOTDIR;
  return ret;
}

/*******************************************************************
returns the size in bytes of the named file
********************************************************************/
SMB_OFF_T get_file_size(char *file_name)
{
  SMB_STRUCT_STAT buf;
  buf.st_size = 0;
  if(sys_stat(file_name,&buf) != 0)
    return (SMB_OFF_T)-1;
  return(buf.st_size);
}

/*******************************************************************
return a string representing an attribute for a file
********************************************************************/
char *attrib_string(uint16 mode)
{
  static fstring attrstr;

  attrstr[0] = 0;

  if (mode & aVOLID) fstrcat(attrstr,"V");
  if (mode & aDIR) fstrcat(attrstr,"D");
  if (mode & aARCH) fstrcat(attrstr,"A");
  if (mode & aHIDDEN) fstrcat(attrstr,"H");
  if (mode & aSYSTEM) fstrcat(attrstr,"S");
  if (mode & aRONLY) fstrcat(attrstr,"R");	  

  return(attrstr);
}

/*******************************************************************
  show a smb message structure
********************************************************************/
void show_msg(char *buf)
{
	int i;
	int bcc=0;

	if (DEBUGLEVEL < 5) return;

	DEBUG(5,("size=%d\nsmb_com=0x%x\nsmb_rcls=%d\nsmb_reh=%d\nsmb_err=%d\nsmb_flg=%d\nsmb_flg2=%d\n",
			smb_len(buf),
			(int)CVAL(buf,smb_com),
			(int)CVAL(buf,smb_rcls),
			(int)CVAL(buf,smb_reh),
			(int)SVAL(buf,smb_err),
			(int)CVAL(buf,smb_flg),
			(int)SVAL(buf,smb_flg2)));
	DEBUG(5,("smb_tid=%d\nsmb_pid=%d\nsmb_uid=%d\nsmb_mid=%d\nsmt_wct=%d\n",
			(int)SVAL(buf,smb_tid),
			(int)SVAL(buf,smb_pid),
			(int)SVAL(buf,smb_uid),
			(int)SVAL(buf,smb_mid),
			(int)CVAL(buf,smb_wct)));

	for (i=0;i<(int)CVAL(buf,smb_wct);i++)
	{
		DEBUG(5,("smb_vwv[%d]=%d (0x%X)\n",i,
			SVAL(buf,smb_vwv+2*i),SVAL(buf,smb_vwv+2*i)));
	}

	bcc = (int)SVAL(buf,smb_vwv+2*(CVAL(buf,smb_wct)));

	DEBUG(5,("smb_bcc=%d\n",bcc));

	if (DEBUGLEVEL < 10) return;

	if (DEBUGLEVEL < 50)
	{
		bcc = MIN(bcc, 512);
	}

	dump_data(10, smb_buf(buf), bcc);
}

/*******************************************************************
  set the length and marker of an smb packet
********************************************************************/
void smb_setlen(char *buf,int len)
{
  _smb_setlen(buf,len);

  CVAL(buf,4) = 0xFF;
  CVAL(buf,5) = 'S';
  CVAL(buf,6) = 'M';
  CVAL(buf,7) = 'B';
}

/*******************************************************************
  setup the word count and byte count for a smb message
********************************************************************/
int set_message(char *buf,int num_words,int num_bytes,BOOL zero)
{
  if (zero)
    memset(buf + smb_size,'\0',num_words*2 + num_bytes);
  CVAL(buf,smb_wct) = num_words;
  SSVAL(buf,smb_vwv + num_words*SIZEOFWORD,num_bytes);  
  smb_setlen(buf,smb_size + num_words*2 + num_bytes - 4);
  return (smb_size + num_words*2 + num_bytes);
}

/*******************************************************************
reduce a file name, removing .. elements.
********************************************************************/
void dos_clean_name(char *s)
{
  char *p=NULL;

  DEBUG(3,("dos_clean_name [%s]\n",s));

  /* remove any double slashes */
  all_string_sub(s, "\\\\", "\\", 0);

  while ((p = strstr(s,"\\..\\")) != NULL)
    {
      pstring s1;

      *p = 0;
      pstrcpy(s1,p+3);

      if ((p=strrchr(s,'\\')) != NULL)
	*p = 0;
      else
	*s = 0;
      pstrcat(s,s1);
    }  

  trim_string(s,NULL,"\\..");

  all_string_sub(s, "\\.\\", "\\", 0);
}

/*******************************************************************
reduce a file name, removing .. elements. 
********************************************************************/
void unix_clean_name(char *s)
{
  char *p=NULL;

  DEBUG(3,("unix_clean_name [%s]\n",s));

  /* remove any double slashes */
  all_string_sub(s, "//","/", 0);

  /* Remove leading ./ characters */
  if(strncmp(s, "./", 2) == 0) {
    trim_string(s, "./", NULL);
    if(*s == 0)
      pstrcpy(s,"./");
  }

  while ((p = strstr(s,"/../")) != NULL)
    {
      pstring s1;

      *p = 0;
      pstrcpy(s1,p+3);

      if ((p=strrchr(s,'/')) != NULL)
	*p = 0;
      else
	*s = 0;
      pstrcat(s,s1);
    }  

  trim_string(s,NULL,"/..");
}

/****************************************************************************
  make a dir struct
****************************************************************************/
void make_dir_struct(char *buf,char *mask,char *fname,SMB_OFF_T size,int mode,time_t date)
{  
  char *p;
  pstring mask2;

  pstrcpy(mask2,mask);

  if ((mode & aDIR) != 0)
    size = 0;

  memset(buf+1,' ',11);
  if ((p = strchr(mask2,'.')) != NULL)
    {
      *p = 0;
      memcpy(buf+1,mask2,MIN(strlen(mask2),8));
      memcpy(buf+9,p+1,MIN(strlen(p+1),3));
      *p = '.';
    }
  else
    memcpy(buf+1,mask2,MIN(strlen(mask2),11));

  memset(buf+21,'\0',DIR_STRUCT_SIZE-21);
  CVAL(buf,21) = mode;
  put_dos_date(buf,22,date);
  SSVAL(buf,26,size & 0xFFFF);
  SSVAL(buf,28,(size >> 16)&0xFFFF);
  StrnCpy(buf+30,fname,12);
  if (!case_sensitive)
    strupper(buf+30);
  DEBUG(8,("put name [%s] into dir struct\n",buf+30));
}


/*******************************************************************
close the low 3 fd's and open dev/null in their place
********************************************************************/
void close_low_fds(void)
{
  int fd;
  int i;
  close(0); close(1); 
#ifndef __INSURE__
  close(2);
#endif
  /* try and use up these file descriptors, so silly
     library routines writing to stdout etc won't cause havoc */
  for (i=0;i<3;i++) {
    fd = sys_open("/dev/null",O_RDWR,0);
    if (fd < 0) fd = sys_open("/dev/null",O_WRONLY,0);
    if (fd < 0) {
      DEBUG(0,("Can't open /dev/null\n"));
      return;
    }
    if (fd != i) {
      DEBUG(0,("Didn't get file descriptor %d\n",i));
      return;
    }
  }
}

/****************************************************************************
Set a fd into blocking/nonblocking mode. Uses POSIX O_NONBLOCK if available,
else
if SYSV use O_NDELAY
if BSD use FNDELAY
****************************************************************************/
int set_blocking(int fd, BOOL set)
{
  int val;
#ifdef O_NONBLOCK
#define FLAG_TO_SET O_NONBLOCK
#else
#ifdef SYSV
#define FLAG_TO_SET O_NDELAY
#else /* BSD */
#define FLAG_TO_SET FNDELAY
#endif
#endif

  if((val = fcntl(fd, F_GETFL, 0)) == -1)
	return -1;
  if(set) /* Turn blocking on - ie. clear nonblock flag */
	val &= ~FLAG_TO_SET;
  else
    val |= FLAG_TO_SET;
  return fcntl( fd, F_SETFL, val);
#undef FLAG_TO_SET
}

/****************************************************************************
transfer some data between two fd's
****************************************************************************/
SMB_OFF_T transfer_file(int infd,int outfd,SMB_OFF_T n,char *header,int headlen,int align)
{
  static char *buf=NULL;  
  static int size=0;
  char *buf1,*abuf;
  SMB_OFF_T total = 0;

  DEBUG(4,("transfer_file n=%.0f  (head=%d) called\n",(double)n,headlen));

  if (size == 0) {
    size = lp_readsize();
    size = MAX(size,1024);
  }

  while (!buf && size>0) {
    buf = (char *)Realloc(buf,size+8);
    if (!buf) size /= 2;
  }

  if (!buf) {
    DEBUG(0,("Can't allocate transfer buffer!\n"));
    exit(1);
  }

  abuf = buf + (align%8);

  if (header)
    n += headlen;

  while (n > 0)
  {
    int s = (int)MIN(n,(SMB_OFF_T)size);
    int ret,ret2=0;

    ret = 0;

    if (header && (headlen >= MIN(s,1024))) {
      buf1 = header;
      s = headlen;
      ret = headlen;
      headlen = 0;
      header = NULL;
    } else {
      buf1 = abuf;
    }

    if (header && headlen > 0)
    {
      ret = MIN(headlen,size);
      memcpy(buf1,header,ret);
      headlen -= ret;
      header += ret;
      if (headlen <= 0) header = NULL;
    }

    if (s > ret)
      ret += read(infd,buf1+ret,s-ret);

    if (ret > 0)
    {
      ret2 = (outfd>=0?write_data(outfd,buf1,ret):ret);
      if (ret2 > 0) total += ret2;
      /* if we can't write then dump excess data */
      if (ret2 != ret)
        transfer_file(infd,-1,n-(ret+headlen),NULL,0,0);
    }
    if (ret <= 0 || ret2 != ret)
      return(total);
    n -= ret;
  }
  return(total);
}


/*******************************************************************
sleep for a specified number of milliseconds
********************************************************************/
void msleep(int t)
{
  int tdiff=0;
  struct timeval tval,t1,t2;  
  fd_set fds;

  GetTimeOfDay(&t1);
  GetTimeOfDay(&t2);
  
  while (tdiff < t) {
    tval.tv_sec = (t-tdiff)/1000;
    tval.tv_usec = 1000*((t-tdiff)%1000);
 
    FD_ZERO(&fds);
    errno = 0;
    sys_select_intr(0,&fds,&tval);

    GetTimeOfDay(&t2);
    tdiff = TvalDiff(&t1,&t2);
  }
}


/****************************************************************************
become a daemon, discarding the controlling terminal
****************************************************************************/
void become_daemon(void)
{
	if (sys_fork()) {
		_exit(0);
	}

  /* detach from the terminal */
#ifdef HAVE_SETSID
	setsid();
#elif defined(TIOCNOTTY)
	{
		int i = sys_open("/dev/tty", O_RDWR, 0);
		if (i != -1) {
			ioctl(i, (int) TIOCNOTTY, (char *)0);      
			close(i);
		}
	}
#endif /* HAVE_SETSID */

	/* Close fd's 0,1,2. Needed if started by rsh */
	close_low_fds();
}


/****************************************************************************
put up a yes/no prompt
****************************************************************************/
BOOL yesno(char *p)
{
  pstring ans;
  printf("%s",p);

  if (!fgets(ans,sizeof(ans)-1,stdin))
    return(False);

  if (*ans == 'y' || *ans == 'Y')
    return(True);

  return(False);
}

#ifdef HPUX
/****************************************************************************
this is a version of setbuffer() for those machines that only have setvbuf
****************************************************************************/
 void setbuffer(FILE *f,char *buf,int bufsize)
{
  setvbuf(f,buf,_IOFBF,bufsize);
}
#endif

/****************************************************************************
expand a pointer to be a particular size
****************************************************************************/
void *Realloc(void *p,size_t size)
{
  void *ret=NULL;

  if (size == 0) {
    if (p) free(p);
    DEBUG(5,("Realloc asked for 0 bytes\n"));
    return NULL;
  }

  if (!p)
    ret = (void *)malloc(size);
  else
    ret = (void *)realloc(p,size);

  if (!ret)
    DEBUG(0,("Memory allocation error: failed to expand to %d bytes\n",(int)size));

  return(ret);
}


/****************************************************************************
free memory, checks for NULL
****************************************************************************/
void safe_free(void *p)
{
	if (p != NULL)
	{
		free(p);
	}
}


/****************************************************************************
get my own name and IP
****************************************************************************/
BOOL get_myname(char *my_name)
{
	pstring hostname;

	*hostname = 0;

	/* get my host name */
	if (gethostname(hostname, sizeof(hostname)) == -1) {
		DEBUG(0,("gethostname failed\n"));
		return False;
	} 

	/* Ensure null termination. */
	hostname[sizeof(hostname)-1] = '\0';

	if (my_name) {
		/* split off any parts after an initial . */
		char *p = strchr(hostname,'.');
		if (p) *p = 0;
		
		fstrcpy(my_name,hostname);
	}
	
	return(True);
}

/****************************************************************************
interpret a protocol description string, with a default
****************************************************************************/
int interpret_protocol(char *str,int def)
{
  if (strequal(str,"NT1"))
    return(PROTOCOL_NT1);
  if (strequal(str,"LANMAN2"))
    return(PROTOCOL_LANMAN2);
  if (strequal(str,"LANMAN1"))
    return(PROTOCOL_LANMAN1);
  if (strequal(str,"CORE"))
    return(PROTOCOL_CORE);
  if (strequal(str,"COREPLUS"))
    return(PROTOCOL_COREPLUS);
  if (strequal(str,"CORE+"))
    return(PROTOCOL_COREPLUS);
  
  DEBUG(0,("Unrecognised protocol level %s\n",str));
  
  return(def);
}

/****************************************************************************
 Return true if a string could be a pure IP address.
****************************************************************************/

BOOL is_ipaddress(const char *str)
{
  BOOL pure_address = True;
  int i;
  
  for (i=0; pure_address && str[i]; i++)
    if (!(isdigit((int)str[i]) || str[i] == '.'))
      pure_address = False;

  /* Check that a pure number is not misinterpreted as an IP */
  pure_address = pure_address && (strchr(str, '.') != NULL);

  return pure_address;
}

/****************************************************************************
interpret an internet address or name into an IP address in 4 byte form
****************************************************************************/

uint32 interpret_addr(char *str)
{
  struct hostent *hp;
  uint32 res;

  if (strcmp(str,"0.0.0.0") == 0) return(0);
  if (strcmp(str,"255.255.255.255") == 0) return(0xFFFFFFFF);

  /* if it's in the form of an IP address then get the lib to interpret it */
  if (is_ipaddress(str)) {
    res = inet_addr(str);
  } else {
    /* otherwise assume it's a network name of some sort and use 
       Get_Hostbyname */
    if ((hp = Get_Hostbyname(str)) == 0) {
      DEBUG(3,("Get_Hostbyname: Unknown host. %s\n",str));
      return 0;
    }
    if(hp->h_addr == NULL) {
      DEBUG(3,("Get_Hostbyname: host address is invalid for host %s\n",str));
      return 0;
    }
    putip((char *)&res,(char *)hp->h_addr);
  }

  if (res == (uint32)-1) return(0);

  return(res);
}

/*******************************************************************
  a convenient addition to interpret_addr()
  ******************************************************************/
struct in_addr *interpret_addr2(char *str)
{
  static struct in_addr ret;
  uint32 a = interpret_addr(str);
  ret.s_addr = a;
  return(&ret);
}

/*******************************************************************
  check if an IP is the 0.0.0.0
  ******************************************************************/
BOOL zero_ip(struct in_addr ip)
{
  uint32 a;
  putip((char *)&a,(char *)&ip);
  return(a == 0);
}


#if (defined(HAVE_NETGROUP) && defined(WITH_AUTOMOUNT))
/******************************************************************
 Remove any mount options such as -rsize=2048,wsize=2048 etc.
 Based on a fix from <Thomas.Hepper@icem.de>.
*******************************************************************/

static void strip_mount_options( pstring *str)
{
  if (**str == '-')
  { 
    char *p = *str;
    while(*p && !isspace(*p))
      p++;
    while(*p && isspace(*p))
      p++;
    if(*p) {
      pstring tmp_str;

      pstrcpy(tmp_str, p);
      pstrcpy(*str, tmp_str);
    }
  }
}

/*******************************************************************
 Patch from jkf@soton.ac.uk
 Split Luke's automount_server into YP lookup and string splitter
 so can easily implement automount_path(). 
 As we may end up doing both, cache the last YP result. 
*******************************************************************/

#ifdef WITH_NISPLUS_HOME
static char *automount_lookup(char *user_name)
{
  static fstring last_key = "";
  static pstring last_value = "";
 
  char *nis_map = (char *)lp_nis_home_map_name();
 
  char nis_domain[NIS_MAXNAMELEN + 1];
  char buffer[NIS_MAXATTRVAL + 1];
  nis_result *result;
  nis_object *object;
  entry_obj  *entry;
 
  strncpy(nis_domain, (char *)nis_local_directory(), NIS_MAXNAMELEN);
  nis_domain[NIS_MAXNAMELEN] = '\0';
 
  DEBUG(5, ("NIS+ Domain: %s\n", nis_domain));
 
  if (strcmp(user_name, last_key))
  {
    slprintf(buffer, sizeof(buffer)-1, "[%s=%s]%s.%s", "key", user_name, nis_map, nis_domain);
    DEBUG(5, ("NIS+ querystring: %s\n", buffer));
 
    if (result = nis_list(buffer, RETURN_RESULT, NULL, NULL))
    {
       if (result->status != NIS_SUCCESS)
      {
        DEBUG(3, ("NIS+ query failed: %s\n", nis_sperrno(result->status)));
        fstrcpy(last_key, ""); pstrcpy(last_value, "");
      }
      else
      {
        object = result->objects.objects_val;
        if (object->zo_data.zo_type == ENTRY_OBJ)
        {
           entry = &object->zo_data.objdata_u.en_data;
           DEBUG(5, ("NIS+ entry type: %s\n", entry->en_type));
           DEBUG(3, ("NIS+ result: %s\n", entry->en_cols.en_cols_val[1].ec_value.ec_value_val));
 
           pstrcpy(last_value, entry->en_cols.en_cols_val[1].ec_value.ec_value_val);
           pstring_sub(last_value, "&", user_name);
           fstrcpy(last_key, user_name);
        }
      }
    }
    nis_freeresult(result);
  }

  strip_mount_options(&last_value);

  DEBUG(4, ("NIS+ Lookup: %s resulted in %s\n", user_name, last_value));
  return last_value;
}
#else /* WITH_NISPLUS_HOME */
static char *automount_lookup(char *user_name)
{
  static fstring last_key = "";
  static pstring last_value = "";

  int nis_error;        /* returned by yp all functions */
  char *nis_result;     /* yp_match inits this */
  int nis_result_len;  /* and set this */
  char *nis_domain;     /* yp_get_default_domain inits this */
  char *nis_map = (char *)lp_nis_home_map_name();

  if ((nis_error = yp_get_default_domain(&nis_domain)) != 0)
  {
    DEBUG(3, ("YP Error: %s\n", yperr_string(nis_error)));
    return last_value;
  }

  DEBUG(5, ("NIS Domain: %s\n", nis_domain));

  if (!strcmp(user_name, last_key))
  {
    nis_result = last_value;
    nis_result_len = strlen(last_value);
    nis_error = 0;
  }
  else
  {
    if ((nis_error = yp_match(nis_domain, nis_map,
                              user_name, strlen(user_name),
                              &nis_result, &nis_result_len)) != 0)
    {
      DEBUG(3, ("YP Error: \"%s\" while looking up \"%s\" in map \"%s\"\n", 
               yperr_string(nis_error), user_name, nis_map));
    }
    if (!nis_error && nis_result_len >= sizeof(pstring))
    {
      nis_result_len = sizeof(pstring)-1;
    }
    fstrcpy(last_key, user_name);
    strncpy(last_value, nis_result, nis_result_len);
    last_value[nis_result_len] = '\0';
  }

  strip_mount_options(&last_value);

  DEBUG(4, ("YP Lookup: %s resulted in %s\n", user_name, last_value));
  return last_value;
}
#endif /* WITH_NISPLUS_HOME */
#endif


/*******************************************************************
are two IPs on the same subnet?
********************************************************************/
BOOL same_net(struct in_addr ip1,struct in_addr ip2,struct in_addr mask)
{
  uint32 net1,net2,nmask;

  nmask = ntohl(mask.s_addr);
  net1  = ntohl(ip1.s_addr);
  net2  = ntohl(ip2.s_addr);
            
  return((net1 & nmask) == (net2 & nmask));
}


/****************************************************************************
a wrapper for gethostbyname() that tries with all lower and all upper case 
if the initial name fails
****************************************************************************/
struct hostent *Get_Hostbyname(const char *name)
{
  char *name2 = strdup(name);
  struct hostent *ret;

  if (!name2)
    {
      DEBUG(0,("Memory allocation error in Get_Hostbyname! panic\n"));
      exit(0);
    }

   
  /* 
   * This next test is redundent and causes some systems (with
   * broken isalnum() calls) problems.
   * JRA.
   */

#if 0
  if (!isalnum(*name2))
    {
      free(name2);
      return(NULL);
    }
#endif /* 0 */

  ret = sys_gethostbyname(name2);
  if (ret != NULL)
    {
      free(name2);
      return(ret);
    }

  /* try with all lowercase */
  strlower(name2);
  ret = sys_gethostbyname(name2);
  if (ret != NULL)
    {
      free(name2);
      return(ret);
    }

  /* try with all uppercase */
  strupper(name2);
  ret = sys_gethostbyname(name2);
  if (ret != NULL)
    {
      free(name2);
      return(ret);
    }
  
  /* nothing works :-( */
  free(name2);
  return(NULL);
}


/****************************************************************************
check if a process exists. Does this work on all unixes?
****************************************************************************/

BOOL process_exists(pid_t pid)
{
	return(kill(pid,0) == 0 || errno != ESRCH);
}


/*******************************************************************
 Convert a uid into a user name.
********************************************************************/

char *uidtoname(uid_t uid)
{
	static fstring name;
	struct passwd *pass;

	if (winbind_uidtoname(name, uid))
		return name;

	pass = sys_getpwuid(uid);
	if (pass) return(pass->pw_name);
	slprintf(name, sizeof(name) - 1, "%d",(int)uid);
	return(name);
}


/*******************************************************************
 Convert a gid into a group name.
********************************************************************/

char *gidtoname(gid_t gid)
{
	static fstring name;
	struct group *grp;

	if (winbind_gidtoname(name, gid))
		return name;

	grp = getgrgid(gid);
	if (grp) return(grp->gr_name);
	slprintf(name,sizeof(name) - 1, "%d",(int)gid);
	return(name);
}

/*******************************************************************
 Convert a user name into a uid. If winbindd is present uses this.
********************************************************************/

uid_t nametouid(char *name)
{
	struct passwd *pass;
	char *p;
	uid_t u;

	u = (uid_t)strtol(name, &p, 0);
	if (p != name) return u;

	if (winbind_nametouid(&u, name))
		return u;

	pass = sys_getpwnam(name);
	if (pass) return(pass->pw_uid);
	return (uid_t)-1;
}

/*******************************************************************
 Convert a name to a gid_t if possible. Return -1 if not a group. If winbindd
 is present does a shortcut lookup...
********************************************************************/

gid_t nametogid(char *name)
{
	struct group *grp;
	char *p;
	gid_t g;

	g = (gid_t)strtol(name, &p, 0);
	if (p != name) return g;

	if (winbind_nametogid(&g, name))
		return g;

	grp = getgrnam(name);
	if (grp) return(grp->gr_gid);
	return (gid_t)-1;
}

/*******************************************************************
something really nasty happened - panic!
********************************************************************/
void smb_panic(char *why)
{
	char *cmd = lp_panic_action();
	if (cmd && *cmd) {
		system(cmd);
	}
	DEBUG(0,("PANIC: %s\n", why));
	dbgflush();
	abort();
}


/*******************************************************************
a readdir wrapper which just returns the file name
********************************************************************/
char *readdirname(DIR *p)
{
	SMB_STRUCT_DIRENT *ptr;
	char *dname;

	if (!p) return(NULL);
  
	ptr = (SMB_STRUCT_DIRENT *)sys_readdir(p);
	if (!ptr) return(NULL);

	dname = ptr->d_name;

#ifdef NEXT2
	if (telldir(p) < 0) return(NULL);
#endif

#ifdef HAVE_BROKEN_READDIR
	/* using /usr/ucb/cc is BAD */
	dname = dname - 2;
#endif

	{
		static pstring buf;
		memcpy(buf, dname, NAMLEN(ptr)+1);
		dname = buf;
	}

	return(dname);
}

/*******************************************************************
 Utility function used to decide if the last component 
 of a path matches a (possibly wildcarded) entry in a namelist.
********************************************************************/

BOOL is_in_path(char *name, name_compare_entry *namelist)
{
  pstring last_component;
  char *p;

  DEBUG(8, ("is_in_path: %s\n", name));

  /* if we have no list it's obviously not in the path */
  if((namelist == NULL ) || ((namelist != NULL) && (namelist[0].name == NULL))) 
  {
    DEBUG(8,("is_in_path: no name list.\n"));
    return False;
  }

  /* Get the last component of the unix name. */
  p = strrchr(name, '/');
  strncpy(last_component, p ? ++p : name, sizeof(last_component)-1);
  last_component[sizeof(last_component)-1] = '\0'; 

  for(; namelist->name != NULL; namelist++)
  {
    if(namelist->is_wild)
    {
      if (mask_match(last_component, namelist->name, case_sensitive))
      {
         DEBUG(8,("is_in_path: mask match succeeded\n"));
         return True;
      }
    }
    else
    {
      if((case_sensitive && (strcmp(last_component, namelist->name) == 0))||
       (!case_sensitive && (StrCaseCmp(last_component, namelist->name) == 0)))
        {
         DEBUG(8,("is_in_path: match succeeded\n"));
         return True;
        }
    }
  }
  DEBUG(8,("is_in_path: match not found\n"));
 
  return False;
}

/*******************************************************************
 Strip a '/' separated list into an array of 
 name_compare_enties structures suitable for 
 passing to is_in_path(). We do this for
 speed so we can pre-parse all the names in the list 
 and don't do it for each call to is_in_path().
 namelist is modified here and is assumed to be 
 a copy owned by the caller.
 We also check if the entry contains a wildcard to
 remove a potentially expensive call to mask_match
 if possible.
********************************************************************/
 
void set_namearray(name_compare_entry **ppname_array, char *namelist)
{
  char *name_end;
  char *nameptr = namelist;
  int num_entries = 0;
  int i;

  (*ppname_array) = NULL;

  if((nameptr == NULL ) || ((nameptr != NULL) && (*nameptr == '\0'))) 
    return;

  /* We need to make two passes over the string. The
     first to count the number of elements, the second
     to split it.
   */
  while(*nameptr) 
    {
      if ( *nameptr == '/' ) 
        {
          /* cope with multiple (useless) /s) */
          nameptr++;
          continue;
        }
      /* find the next / */
      name_end = strchr(nameptr, '/');

      /* oops - the last check for a / didn't find one. */
      if (name_end == NULL)
        break;

      /* next segment please */
      nameptr = name_end + 1;
      num_entries++;
    }

  if(num_entries == 0)
    return;

  if(( (*ppname_array) = (name_compare_entry *)malloc( 
           (num_entries + 1) * sizeof(name_compare_entry))) == NULL)
        {
    DEBUG(0,("set_namearray: malloc fail\n"));
    return;
        }

  /* Now copy out the names */
  nameptr = namelist;
  i = 0;
  while(*nameptr)
             {
      if ( *nameptr == '/' ) 
      {
          /* cope with multiple (useless) /s) */
          nameptr++;
          continue;
      }
      /* find the next / */
      if ((name_end = strchr(nameptr, '/')) != NULL) 
      {
          *name_end = 0;
         }

      /* oops - the last check for a / didn't find one. */
      if(name_end == NULL) 
        break;

      (*ppname_array)[i].is_wild = ms_has_wild(nameptr);
      if(((*ppname_array)[i].name = strdup(nameptr)) == NULL)
      {
        DEBUG(0,("set_namearray: malloc fail (1)\n"));
        return;
      }

      /* next segment please */
      nameptr = name_end + 1;
      i++;
    }
  
  (*ppname_array)[i].name = NULL;

  return;
}

/****************************************************************************
routine to free a namearray.
****************************************************************************/

void free_namearray(name_compare_entry *name_array)
{
  if(name_array == 0)
    return;

  if(name_array->name != NULL)
    free(name_array->name);

  free((char *)name_array);
}

/****************************************************************************
 Simple routine to do POSIX file locking. Cruft in NFS and 64->32 bit mapping
 is dealt with in posix.c
****************************************************************************/

BOOL fcntl_lock(int fd, int op, SMB_OFF_T offset, SMB_OFF_T count, int type)
{
  SMB_STRUCT_FLOCK lock;
  int ret;

  DEBUG(8,("fcntl_lock %d %d %.0f %.0f %d\n",fd,op,(double)offset,(double)count,type));

  lock.l_type = type;
  lock.l_whence = SEEK_SET;
  lock.l_start = offset;
  lock.l_len = count;
  lock.l_pid = 0;

  errno = 0;

  ret = fcntl(fd,op,&lock);

  if (errno != 0)
    DEBUG(3,("fcntl_lock: fcntl lock gave errno %d (%s)\n",errno,strerror(errno)));

  /* a lock query */
  if (op == SMB_F_GETLK)
  {
    if ((ret != -1) &&
        (lock.l_type != F_UNLCK) && 
        (lock.l_pid != 0) && 
        (lock.l_pid != sys_getpid()))
    {
      DEBUG(3,("fcntl_lock: fd %d is locked by pid %d\n",fd,(int)lock.l_pid));
      return(True);
    }

    /* it must be not locked or locked by me */
    return(False);
  }

  /* a lock set or unset */
  if (ret == -1)
  {
    DEBUG(3,("fcntl_lock: lock failed at offset %.0f count %.0f op %d type %d (%s)\n",
          (double)offset,(double)count,op,type,strerror(errno)));
    return(False);
  }

  /* everything went OK */
  DEBUG(8,("fcntl_lock: Lock call successful\n"));

  return(True);
}

/*******************************************************************
is the name specified one of my netbios names
returns true is it is equal, false otherwise
********************************************************************/
BOOL is_myname(char *s)
{
  int n;
  BOOL ret = False;

  for (n=0; my_netbios_names[n]; n++) {
    if (strequal(my_netbios_names[n], s))
      ret=True;
  }
  DEBUG(8, ("is_myname(\"%s\") returns %d\n", s, ret));
  return(ret);
}

/*******************************************************************
set the horrid remote_arch string based on an enum.
********************************************************************/
void set_remote_arch(enum remote_arch_types type)
{
  extern fstring remote_arch;
  ra_type = type;
  switch( type )
  {
  case RA_WFWG:
    fstrcpy(remote_arch, "WfWg");
    return;
  case RA_OS2:
    fstrcpy(remote_arch, "OS2");
    return;
  case RA_WIN95:
    fstrcpy(remote_arch, "Win95");
    return;
  case RA_WINNT:
    fstrcpy(remote_arch, "WinNT");
    return;
  case RA_WIN2K:
    fstrcpy(remote_arch, "Win2K");
    return;
  case RA_SAMBA:
    fstrcpy(remote_arch,"Samba");
    return;
  default:
    ra_type = RA_UNKNOWN;
    fstrcpy(remote_arch, "UNKNOWN");
    break;
  }
}

/*******************************************************************
 Get the remote_arch type.
********************************************************************/
enum remote_arch_types get_remote_arch(void)
{
  return ra_type;
}


void out_ascii(FILE *f, unsigned char *buf,int len)
{
	int i;
	for (i=0;i<len;i++)
	{
		fprintf(f, "%c", isprint(buf[i])?buf[i]:'.');
	}
}

void out_data(FILE *f,char *buf1,int len, int per_line)
{
	unsigned char *buf = (unsigned char *)buf1;
	int i=0;
	if (len<=0)
	{
		return;
	}

	fprintf(f, "[%03X] ",i);
	for (i=0;i<len;)
	{
		fprintf(f, "%02X ",(int)buf[i]);
		i++;
		if (i%(per_line/2) == 0) fprintf(f, " ");
		if (i%per_line == 0)
		{      
			out_ascii(f,&buf[i-per_line  ],per_line/2); fprintf(f, " ");
			out_ascii(f,&buf[i-per_line/2],per_line/2); fprintf(f, "\n");
			if (i<len) fprintf(f, "[%03X] ",i);
		}
	}
	if ((i%per_line) != 0)
	{
		int n;

		n = per_line - (i%per_line);
		fprintf(f, " ");
		if (n>(per_line/2)) fprintf(f, " ");
		while (n--)
		{
			fprintf(f, "   ");
		}
		n = MIN(per_line/2,i%per_line);
		out_ascii(f,&buf[i-(i%per_line)],n); fprintf(f, " ");
		n = (i%per_line) - n;
		if (n>0) out_ascii(f,&buf[i-n],n); 
		fprintf(f, "\n");    
	}
}

void print_asc(int level, unsigned char *buf,int len)
{
	int i;
	for (i=0;i<len;i++)
		DEBUG(level,("%c", isprint(buf[i])?buf[i]:'.'));
}

void print_hex_bytes(int level, const unsigned char *buf, int len)
{
	int idx;

	for (idx = 0; idx < len; idx++) {
		DEBUG(level, ("%02x ", buf[idx]));
	}
}

void dump_data(int level,char *buf1,int len)
{
  unsigned char *buf = (unsigned char *)buf1;
  int i=0;
  if (len<=0) return;

  DEBUG(level,("[%03X] ",i));
  for (i=0;i<len;) {
    DEBUG(level,("%02X ",(int)buf[i]));
    i++;
    if (i%8 == 0) DEBUG(level,(" "));
    if (i%16 == 0) {      
      print_asc(level,&buf[i-16],8); DEBUG(level,(" "));
      print_asc(level,&buf[i-8],8); DEBUG(level,("\n"));
      if (i<len) DEBUG(level,("[%03X] ",i));
    }
  }
  if (i%16) {
    int n;

    n = 16 - (i%16);
    DEBUG(level,(" "));
    if (n>8) DEBUG(level,(" "));
    while (n--) DEBUG(level,("   "));

    n = MIN(8,i%16);
    print_asc(level,&buf[i-(i%16)],n); DEBUG(level,(" "));
    n = (i%16) - n;
    if (n>0) print_asc(level,&buf[i-n],n); 
    DEBUG(level,("\n"));    
  }
}

char *tab_depth(int depth)
{
	static pstring spaces;
	memset(spaces, ' ', depth * 4);
	spaces[depth * 4] = 0;
	return spaces;
}

/*****************************************************************************
 * Provide a checksum on a string
 *
 *  Input:  s - the null-terminated character string for which the checksum
 *              will be calculated.
 *
 *  Output: The checksum value calculated for s.
 *
 * ****************************************************************************
 */
int str_checksum(const char *s)
{
	int res = 0;
	int c;
	int i=0;
	
	while(*s) {
		c = *s;
		res ^= (c << (i % 15)) ^ (c >> (15-(i%15)));
		s++;
		i++;
	}
	return(res);
} /* str_checksum */



/*****************************************************************
zero a memory area then free it. Used to catch bugs faster
*****************************************************************/  
void zero_free(void *p, size_t size)
{
	memset(p, 0, size);
	free(p);
}


/*****************************************************************
set our open file limit to a requested max and return the limit
*****************************************************************/  
int set_maxfiles(int requested_max)
{
#if (defined(HAVE_GETRLIMIT) && defined(RLIMIT_NOFILE))
	struct rlimit rlp;
	int saved_current_limit;

	if(getrlimit(RLIMIT_NOFILE, &rlp)) {
		DEBUG(0,("set_maxfiles: getrlimit (1) for RLIMIT_NOFILE failed with error %s\n",
			strerror(errno) ));
		/* just guess... */
		return requested_max;
	}

	/* 
     * Set the fd limit to be real_max_open_files + MAX_OPEN_FUDGEFACTOR to
	 * account for the extra fd we need 
	 * as well as the log files and standard
	 * handles etc. Save the limit we want to set in case
	 * we are running on an OS that doesn't support this limit (AIX)
	 * which always returns RLIM_INFINITY for rlp.rlim_max.
	 */

	saved_current_limit = rlp.rlim_cur = MIN(requested_max,rlp.rlim_max);

	if(setrlimit(RLIMIT_NOFILE, &rlp)) {
		DEBUG(0,("set_maxfiles: setrlimit for RLIMIT_NOFILE for %d files failed with error %s\n", 
			(int)rlp.rlim_cur, strerror(errno) ));
		/* just guess... */
		return saved_current_limit;
	}

	if(getrlimit(RLIMIT_NOFILE, &rlp)) {
		DEBUG(0,("set_maxfiles: getrlimit (2) for RLIMIT_NOFILE failed with error %s\n",
			strerror(errno) ));
		/* just guess... */
		return saved_current_limit;
    }

#if defined(RLIM_INFINITY)
	if(rlp.rlim_cur == RLIM_INFINITY)
		return saved_current_limit;
#endif

    if((int)rlp.rlim_cur > saved_current_limit)
		return saved_current_limit;

	return rlp.rlim_cur;
#else /* !defined(HAVE_GETRLIMIT) || !defined(RLIMIT_NOFILE) */
	/*
	 * No way to know - just guess...
	 */
	return requested_max;
#endif
}

/*****************************************************************
 splits out the start of the key (HKLM or HKU) and the rest of the key
 *****************************************************************/  
BOOL reg_split_key(char *full_keyname, uint32 *reg_type, char *key_name)
{
	pstring tmp;

	if (!next_token(&full_keyname, tmp, "\\", sizeof(tmp)))
	{
		return False;
	}

	(*reg_type) = 0;

	DEBUG(10, ("reg_split_key: hive %s\n", tmp));

	if (strequal(tmp, "HKLM") || strequal(tmp, "HKEY_LOCAL_MACHINE"))
	{
		(*reg_type) = HKEY_LOCAL_MACHINE;
	}
	else if (strequal(tmp, "HKU") || strequal(tmp, "HKEY_USERS"))
	{
		(*reg_type) = HKEY_USERS;
	}
	else
	{
		DEBUG(10,("reg_split_key: unrecognised hive key %s\n", tmp));
		return False;
	}
	
	if (next_token(NULL, tmp, "\n\r", sizeof(tmp)))
	{
		fstrcpy(key_name, tmp);
	}
	else
	{
		key_name[0] = 0;
	}

	DEBUG(10, ("reg_split_key: name %s\n", key_name));

	return True;
}


/*****************************************************************
like mktemp() but make sure that no % characters are used
% characters are bad for us because of the macro subs
 *****************************************************************/  
char *smbd_mktemp(char *template)
{
	char *p = mktemp(template);
	char *p2;
	SMB_STRUCT_STAT st;

	if (!p) return NULL;

	while ((p2=strchr(p,'%'))) {
		p2[0] = 'A';
		while (sys_stat(p,&st) == 0 && p2[0] < 'Z') {
			/* damn, it exists */
			p2[0]++;
		}
		if (p2[0] == 'Z') {
			/* oh well ... better return something */
			p2[0] = '%';
			return p;
		}
	}

	return p;
}


/*****************************************************************
like strdup but for memory
 *****************************************************************/  
void *memdup(void *p, size_t size)
{
	void *p2;
	if (size == 0) return NULL;
	p2 = malloc(size);
	if (!p2) return NULL;
	memcpy(p2, p, size);
	return p2;
}

/*****************************************************************
get local hostname and cache result
 *****************************************************************/  
char *myhostname(void)
{
	static pstring ret;
	if (ret[0] == 0) {
		get_myname(ret);
	}
	return ret;
}


/*****************************************************************
a useful function for returning a path in the Samba lock directory
 *****************************************************************/  
char *lock_path(char *name)
{
	static pstring fname;

	pstrcpy(fname,lp_lockdir());
	trim_string(fname,"","/");
	
	if (!directory_exist(fname,NULL)) {
		mkdir(fname,0755);
	}
	
	pstrcat(fname,"/");
	pstrcat(fname,name);

	return fname;
}

/*******************************************************************
 Given a filename - get its directory name
 NB: Returned in static storage.  Caveats:
 o  Not safe in thread environment.
 o  Caller must not free.
 o  If caller wishes to preserve, they should copy.
********************************************************************/

char *parent_dirname(const char *path)
{
	static pstring dirpath;
	char *p;

	if (!path)
		return(NULL);

	pstrcpy(dirpath, path);
	p = strrchr(dirpath, '/');  /* Find final '/', if any */
	if (!p) {
		pstrcpy(dirpath, ".");    /* No final "/", so dir is "." */
	} else {
		if (p == dirpath)
			++p;    /* For root "/", leave "/" in place */
		*p = '\0';
	}
	return dirpath;
}


/*******************************************************************
determine if a pattern contains any Microsoft wildcard characters
 *******************************************************************/
BOOL ms_has_wild(char *s)
{
	char c;
	while ((c = *s++)) {
		switch (c) {
		case '*':
		case '?':
		case '<':
		case '>':
		case '"':
			return True;
		}
	}
	return False;
}


/*******************************************************************
 a wrapper that handles case sensitivity and the special handling
   of the ".." name
 *******************************************************************/
BOOL mask_match(char *string, char *pattern, BOOL is_case_sensitive)
{
	fstring p2, s2;
	if (strcmp(string,"..") == 0) string = ".";
	if (strcmp(pattern,".") == 0) return False;
	
	if (is_case_sensitive) {
		return ms_fnmatch(pattern, string) == 0;
	}

	fstrcpy(p2, pattern);
	fstrcpy(s2, string);
	strlower(p2); 
	strlower(s2);
	return ms_fnmatch(p2, s2) == 0;
}



#ifdef __INSURE__

/*******************************************************************
This routine is a trick to immediately catch errors when debugging
with insure. A xterm with a gdb is popped up when insure catches
a error. It is Linux specific.
********************************************************************/
int _Insure_trap_error(int a1, int a2, int a3, int a4, int a5, int a6)
{
	static int (*fn)();
	int ret;
	char pidstr[10];
	pstring cmd = "/usr/X11R6/bin/xterm -display :0 -T Panic -n Panic -e /bin/sh -c 'cat /tmp/ierrs.*.%d ; gdb /proc/%d/exe %d'";

	slprintf(pidstr, sizeof(pidstr), "%d", sys_getpid());
	pstring_sub(cmd, "%d", pidstr);

	if (!fn) {
		static void *h;
		h = dlopen("/usr/local/parasoft/insure++lite/lib.linux2/libinsure.so", RTLD_LAZY);
		fn = dlsym(h, "_Insure_trap_error");
	}

	ret = fn(a1, a2, a3, a4, a5, a6);

	system(cmd);

	return ret;
}
#endif
