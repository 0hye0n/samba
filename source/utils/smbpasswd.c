/*
 * Unix SMB/Netbios implementation. Version 1.9. smbpasswd module.
 * Copyright (C) Jeremy Allison               1995-1999
 * Copyright (C) Luke Kenneth Casson Leighton 1996-1999
 * 
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 675
 * Mass Ave, Cambridge, MA 02139, USA.
 */

#include "includes.h"

extern pstring myhostname;
extern pstring global_myname;
extern int DEBUGLEVEL;

/*
 * Next two lines needed for SunOS and don't
 * hurt anything else...
 */
extern char *optarg;
extern int optind;

/*********************************************************
a strdup with exit
**********************************************************/
static char *xstrdup(char *s)
{
	s = strdup(s);
	if (!s) {
		fprintf(stderr,"out of memory\n");
		exit(1);
	}
	return s;
}


/*********************************************************
 Print command usage on stderr and die.
**********************************************************/
static void usage(void)
{
	if (getuid() == 0) {
		printf("smbpasswd [options] [username] [password]\n");
	} else {
		printf("smbpasswd [options] [password]\n");
	}
	printf("options:\n");
	printf("  -s                   use stdin for password prompt\n");
	printf("  -D LEVEL             debug level\n");
	printf("  -U USER              remote username\n");
	printf("  -r MACHINE           remote machine\n");

	if (getuid() == 0) {
		printf("  -R ORDER             name resolve order\n");
		printf("  -j DOMAIN            join domain name\n");
		printf("  -a                   add user\n");
		printf("  -d                   disable user\n");
		printf("  -e                   enable user\n");
		printf("  -n                   set no password\n");
		printf("  -m                   workstation trust account\n");
		printf("  -b                   backup domain controller account\n");
		printf("  -i                   inter-domain trust account\n");
		printf("  -p                   user cannot change password\n");
		printf("  -x                   user can change password\n");
	}
	
	exit(1);
}

/*********************************************************
Join a domain.
**********************************************************/
static int join_domain(char *domain, char *remote)
{
	pstring remote_machine;
	fstring trust_passwd;
	unsigned char orig_trust_passwd_hash[16];
	BOOL ret;
	uint16 sec_chan;

	DEBUG(5,("join_domain: domain %s remote %s\n", domain, remote));

	pstrcpy(remote_machine, remote ? remote : "");
	fstrcpy(trust_passwd, global_myname);
	strlower(trust_passwd);
	E_md4hash( (uchar *)trust_passwd, orig_trust_passwd_hash);

	switch (lp_server_role())
	{
		case ROLE_DOMAIN_PDC:
		{
			DEBUG(0, ("Cannot join domain - we are PDC!\n"));
			return 1;
		}
		case ROLE_DOMAIN_BDC:
		{
			DEBUG(0, ("Joining Domain as BDC\n"));
			sec_chan = SEC_CHAN_BDC;
			break;
		}
		default:
		{
			DEBUG(0, ("Joining Domain as Workstation\n"));
			sec_chan = SEC_CHAN_WKSTA;
		}
	}

	/*
	 * Create the machine account password file.
	 */
	if(!trust_password_lock( domain, global_myname, True))
	{
		fprintf(stderr, "unable to open the machine account password file for \
machine %s in domain %s.\n", global_myname, domain); 
		return 1;
	}

	/*
	 * Write the old machine account password.
	 */
	
	if(!set_trust_account_password( orig_trust_passwd_hash))
	{              
		fprintf(stderr, "unable to write the machine account password for \
machine %s in domain %s.\n", global_myname, domain);
		trust_password_unlock();
		return 1;
	}
	
	/*
	 * If we are given a remote machine assume this is the PDC.
	 */
	
	if(remote == NULL)
	{
		pstrcpy(remote_machine, lp_passwordserver());
	}

	if(!*remote_machine) {
		fprintf(stderr, "No password server list given in smb.conf - \
unable to join domain.\n");
		trust_password_unlock();
		return 1;
	}

	ret = change_trust_account_password(domain, remote_machine, sec_chan);
	trust_password_unlock();
	
	if(!ret) {
		fprintf(stderr,"Unable to join domain %s.\n",domain);
	} else {
		printf("Joined domain %s.\n",domain);
	}
	
#if 0
	trust_password_delete( domain, global_myname);
#endif

	return (int)ret;
}


static void set_line_buffering(FILE *f)
{
	setvbuf(f, NULL, _IOLBF, 0);
}

/*************************************************************
 Utility function to prompt for passwords from stdin. Each
 password entered must end with a newline.
*************************************************************/
static char *stdin_new_passwd(void)
{
	static fstring new_passwd;
	size_t len;

	ZERO_ARRAY(new_passwd);

	/*
	 * if no error is reported from fgets() and string at least contains
	 * the newline that ends the password, then replace the newline with
	 * a null terminator.
	 */
	if ( fgets(new_passwd, sizeof(new_passwd), stdin) != NULL) {
		if ((len = strlen(new_passwd)) > 0) {
			if(new_passwd[len-1] == '\n')
				new_passwd[len - 1] = 0; 
		}
	}
	return(new_passwd);
}


/*************************************************************
 Utility function to get passwords via tty or stdin
 Used if the '-s' option is set to silently get passwords
 to enable scripting.
*************************************************************/
static char *get_pass( char *prompt, BOOL stdin_get)
{
	char *p;
	if (stdin_get) {
		p = stdin_new_passwd();
	} else {
		p = getpass(prompt);
	}
	return xstrdup(p);
}

/*************************************************************
 Utility function to prompt for new password.
*************************************************************/
static char *prompt_for_new_password(BOOL stdin_get)
{
	char *p;
	fstring new_passwd;

	ZERO_ARRAY(new_passwd);
 
	p = get_pass("New SMB password:", stdin_get);

	fstrcpy(new_passwd, p);

	p = get_pass("Retype new SMB password:", stdin_get);

	if (strcmp(p, new_passwd)) {
		fprintf(stderr, "Mismatch - password unchanged.\n");
		return NULL;
	}

	return xstrdup(p);
}


/*************************************************************
change a password either locally or remotely
*************************************************************/
static BOOL password_change(const char *remote_machine, char *user_name, 
				char *old_passwd, char *new_passwd, 
				BOOL add_user, 
				uint16 acb_info, uint16 acb_mask)
{
	BOOL ret;
	pstring err_str;
	pstring msg_str;

	if (remote_machine != NULL)
	{
		if (add_user ||
		    IS_BITS_SET_SOME(acb_info, ACB_PWNOTREQ | ACB_WSTRUST | ACB_DOMTRUST | ACB_SVRTRUST) ||
		    (IS_BITS_SET_SOME(acb_mask, ACB_DISABLED) && 
		     IS_BITS_CLR_ALL(acb_info, ACB_DISABLED)))
		{
			/* these things can't be done remotely yet */
			return False;
		}
		ret = remote_password_change(remote_machine, user_name, 
		                            old_passwd, new_passwd,
		                            err_str, sizeof(err_str));
		if (*err_str != 0)
		{
			fprintf(stderr, err_str);
		}
		return ret;
	}
	
	ret = local_password_change(user_name, add_user, acb_info, acb_mask,
				     new_passwd, 
				     err_str, sizeof(err_str),
	                             msg_str, sizeof(msg_str));

	if (*msg_str != 0)
	{
		printf(msg_str);
	}
	if (*err_str != 0)
	{
		fprintf(stderr, err_str);
	}

	return ret;
}


/*************************************************************
handle password changing for root
*************************************************************/
static int process_root(int argc, char *argv[])
{
	struct passwd  *pwd;
	int ch;
	uint16 acb_info = 0;
	uint16 acb_mask = 0;
	BOOL joining_domain = False;
	BOOL wks_trust_account = False;
	BOOL srv_trust_account = False;
	BOOL dom_trust_account = False;
	BOOL add_user = False;
	BOOL disable_user = False;
	BOOL enable_user = False;
	BOOL set_no_password = False;
	BOOL stdin_passwd_get = False;
	BOOL lock_password = False;
	BOOL unlock_password = False;
	char *user_name = NULL;
	char *new_domain = NULL;
	char *new_passwd = NULL;
	char *old_passwd = NULL;
	char *remote_machine = NULL;

	while ((ch = getopt(argc, argv, "abdehimnpxj:r:sR:D:U:")) != EOF)
	{
		switch(ch)
		{
			case 'a':
			{
				add_user = True;
				break;
			}
			case 'd':
			{
				disable_user = True;
				new_passwd = "XXXXXX";
				break;
			}
			case 'e':
			{
				enable_user = True;
				break;
			}
			case 'D':
			{
				DEBUGLEVEL = atoi(optarg);
				break;
			}
			case 'n':
			{
				set_no_password = True;
				new_passwd = "NO PASSWORD";
			}
			case 'r':
			{
				remote_machine = optarg;
				break;
			}
			case 's':
			{
				set_line_buffering(stdin);
				set_line_buffering(stdout);
				set_line_buffering(stderr);
				stdin_passwd_get = True;
				break;
			}
			case 'R':
			{
				lp_set_name_resolve_order(optarg);
				break;
			}
			case 'i':
			{
				dom_trust_account = True;
				break;
			}
			case 'b':
			{
				srv_trust_account = True;
				break;
			}
			case 'm':
			{
				wks_trust_account = True;
				break;
			}
			case 'j':
			{
				new_domain = optarg;
				strupper(new_domain);
				joining_domain = True;
				break;
			}
			case 'U':
			{
				user_name = optarg;
				break;
			}
			case 'p':
			{
				lock_password = True;
				break;
			}
			case 'x':
			{
				unlock_password = True;
				break;
			}
			default:
			{
				usage();
			}
		}
	}
	
	argc -= optind;
	argv += optind;

	/*
	 * Ensure add_user and either remote machine or join domain are
	 * not both set.
	 */	
	if (add_user && ((remote_machine != NULL) || joining_domain))
	{
		usage();
	}
	
	if (joining_domain)
	{
		if (argc != 0) usage();
		return join_domain(new_domain, remote_machine);
	}

	/*
	 * Deal with root - can add a user, but only locally.
	 */

	switch(argc) {
	case 0:
		break;
	case 1:
		user_name = argv[0];
		break;
	case 2:
		user_name = argv[0];
		new_passwd = argv[1];
		break;
	default:
		usage();
	}

	if (!user_name && (pwd = getpwuid(0))) {
		user_name = xstrdup(pwd->pw_name);
	} 

	if (!user_name) {
		fprintf(stderr,"You must specify a username\n");
		exit(1);
	}

	if (wks_trust_account || srv_trust_account || dom_trust_account)
	{
		/* add the $ automatically */
		static fstring buf;

		/*
		 * Remove any trailing '$' before we
		 * generate the initial machine password.
		 */

		if (user_name[strlen(user_name)-1] == '$') {
			user_name[strlen(user_name)-1] = 0;
		}

		if (add_user) {
			new_passwd = xstrdup(user_name);
			strlower(new_passwd);
		}

		/*
		 * Now ensure the username ends in '$' for
		 * the machine add.
		 */

		slprintf(buf, sizeof(buf)-1, "%s$", user_name);
		user_name = buf;
	}

	if (!remote_machine && !Get_Pwnam(user_name, True)) {
		fprintf(stderr, "User \"%s\" was not found in system password file.\n", 
			user_name);
		exit(1);
	}

	if (remote_machine != NULL) {
		old_passwd = get_pass("Old SMB password:",stdin_passwd_get);
	}
	
	if (!new_passwd)
	{

		/*
		 * If we are trying to enable a user, first we need to find out
		 * if they are using a modern version of the smbpasswd file that
		 * disables a user by just writing a flag into the file. If so
		 * then we can re-enable a user without prompting for a new
		 * password. If not (ie. they have a no stored password in the
		 * smbpasswd file) then we need to prompt for a new password.
		 */

		if (enable_user)
		{
			struct smb_passwd *smb_pass = getsmbpwnam(user_name);
			if((smb_pass != NULL) && (smb_pass->smb_passwd != NULL))
			{
				new_passwd = "XXXX"; /* Don't care. */
			}
		}

		if(!new_passwd)
		{
			new_passwd = prompt_for_new_password(stdin_passwd_get);
		}
	}
	
	if (enable_user)
	{
		acb_mask |= ACB_DISABLED;
		acb_info &= ~ACB_DISABLED;
	}

	if (disable_user)
	{
		acb_mask |= ACB_DISABLED;
		acb_info |= ACB_DISABLED;
	}

	if (set_no_password)
	{
		acb_mask |= ACB_PWNOTREQ;
		acb_info |= ACB_PWNOTREQ;
	}

	if (lock_password)
	{
		acb_mask |= ACB_PWLOCK;
		acb_info |= ACB_PWLOCK;
	}

	if (unlock_password)
	{
		acb_mask |= ACB_PWLOCK;
		acb_info &= ~ACB_PWLOCK;
	}
	
	if (wks_trust_account)
	{
		acb_mask |= ACB_WSTRUST;
		acb_info |= ACB_WSTRUST;
	}
	else if (srv_trust_account)
	{
		acb_mask |= ACB_SVRTRUST;
		acb_info |= ACB_SVRTRUST;
	}
	else if (dom_trust_account)
	{
		acb_mask |= ACB_DOMTRUST;
		acb_info |= ACB_DOMTRUST;
	}
	else
	{
		acb_mask |= ACB_NORMAL;
		acb_info |= ACB_NORMAL;
	}

	if (!password_change(remote_machine, user_name, old_passwd, new_passwd,
			     add_user, acb_info, acb_mask))
	{
		fprintf(stderr,"Failed to change password entry for %s\n", user_name);
		return 1;
	} 

	if (disable_user)
	{
		printf("User %s disabled.\n", user_name);
	}
	if (enable_user)
	{
		printf("User %s enabled.\n", user_name);
	}
	if (set_no_password)
	{
		printf("User %s - set to no password.\n", user_name);
	}
	if (!disable_user && !enable_user && !set_no_password)
	{
		printf("Password changed for user %s\n", user_name);
	}
	return 0;
}


/*************************************************************
handle password changing for non-root
*************************************************************/
static int process_nonroot(int argc, char *argv[])
{
	struct passwd  *pwd = NULL;
	int ch;
	BOOL stdin_passwd_get = False;
	char *old_passwd = NULL;
	char *remote_machine = NULL;
	char *user_name = NULL;
	char *new_passwd = NULL;
	
	while ((ch = getopt(argc, argv, "hD:r:sU:")) != EOF)
	{
		switch(ch)
		{
		case 'D':
			DEBUGLEVEL = atoi(optarg);
			break;
		case 'r':
			remote_machine = optarg;
			break;
		case 's':
			set_line_buffering(stdin);
			set_line_buffering(stdout);
			set_line_buffering(stderr);
			stdin_passwd_get = True;
			break;
		case 'U':
			user_name = optarg;
			break;
		default:
			usage();
		}
	}
	
	argc -= optind;
	argv += optind;

	if(argc > 1) {
		usage();
	}
	
	if (argc == 1) {
		new_passwd = argv[0];
	}
	
	if (!user_name) {
		pwd = getpwuid(getuid());
		if (pwd) {
			user_name = xstrdup(pwd->pw_name);
		} else {
			fprintf(stderr,"you don't exist - go away\n");
			exit(1);
		}
	}
	
	/*
	 * A non-root user is always setting a password
	 * via a remote machine (even if that machine is
	 * localhost).
	 */	
	if (remote_machine == NULL) {
		remote_machine = "127.0.0.1";
	}

	if (remote_machine != NULL) {
		old_passwd = get_pass("Old SMB password:",stdin_passwd_get);
	}
	
	if (!new_passwd) {
		new_passwd = prompt_for_new_password(stdin_passwd_get);
	}
	
	if (!new_passwd) {
		printf("unable to get new password\n");
		exit(0);
	}

	if (!password_change(remote_machine, user_name,
	                     old_passwd, new_passwd,
			     False, 0x0, 0x0))
	{
		fprintf(stderr,"Failed to change password for %s\n", user_name);
		return 1;
	}

	printf("Password changed for user %s\n", user_name);
	return 0;
}



/*********************************************************
 Start here.
**********************************************************/
int main(int argc, char **argv)
{	
	static pstring servicesf = CONFIGFILE;

	TimeInit();
	
	setup_logging("smbpasswd", True);
	
	charset_initialise();
	
	if (!lp_load(servicesf,True,False,False)) {
		fprintf(stderr, "Can't load %s - run testparm to debug it\n", 
			servicesf);
		exit(1);
	}

	if(!get_myname(myhostname,NULL)) {
		fprintf(stderr, "unable to get my hostname.\n");
		exit(1);
	}

	/*
	 * Set the machine NETBIOS name if not already
	 * set from the config file. 
	 */ 
    
	if (!*global_myname) {   
		char *p;
		fstrcpy(global_myname, myhostname);
		p = strchr(global_myname, '.' );
		if (p) *p = 0;
	}           
	strupper(global_myname);

	codepage_initialise(lp_client_code_page());

	load_interfaces();

	if(!pwdb_initialise(False))
	{
		fprintf(stderr, "Can't setup password database vectors.\n");
		exit(1);
	}

	/* Check the effective uid - make sure we are not setuid */
	if ((geteuid() == (uid_t)0) && (getuid() != (uid_t)0)) {
		fprintf(stderr, "smbpasswd must *NOT* be setuid root.\n");
		exit(1);
	}

	if (getuid() == 0) {
		return process_root(argc, argv);
	} 

	return process_nonroot(argc, argv);
}
