/* 
   Samba Unix/Linux SMB client library 
   Distributed SMB/CIFS Server Management Utility 
   Copyright (C) 2001 Steve French  (sfrench@us.ibm.com)
   Copyright (C) 2001 Jim McDonough (jmcd@us.ibm.com)
   Copyright (C) 2001 Andrew Tridgell (tridge@samba.org)
   Copyright (C) 2001 Andrew Bartlett (abartlet@samba.org)

   Originally written by Steve and Jim. Largely rewritten by tridge in
   November 2001.

   Reworked again by abartlet in December 2001

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */
 
/*****************************************************/
/*                                                   */
/*   Distributed SMB/CIFS Server Management Utility  */
/*                                                   */
/*   The intent was to make the syntax similar       */
/*   to the NET utility (first developed in DOS      */
/*   with additional interesting & useful functions  */
/*   added in later SMB server network operating     */
/*   systems).                                       */
/*                                                   */
/*****************************************************/

#include "includes.h"
#include "../utils/net.h"

/***********************************************************************/
/* Beginning of internationalization section.  Translatable constants  */
/* should be kept in this area and referenced in the rest of the code. */
/*                                                                     */
/* No functions, outside of Samba or LSB (Linux Standards Base) should */
/* be used (if possible).                                              */
/***********************************************************************/

#define YES_STRING              "Yes"
#define NO_STRING               "No"

/************************************************************************************/
/*                       end of internationalization section                        */
/************************************************************************************/

/* Yes, these buggers are globals.... */
char *opt_requester_name = NULL;
char *opt_host = NULL; 
char *opt_password = NULL;
char *opt_user_name = NULL;
BOOL opt_user_specified = False;
char *opt_workgroup = NULL;
int opt_long_list_entries = 0;
int opt_reboot = 0;
int opt_force = 0;
int opt_port = 0;
int opt_maxusers = -1;
char *opt_comment = "";
int opt_flags = -1;
int opt_jobid = 0;
int opt_timeout = 0;
char *opt_target_workgroup = NULL;

BOOL opt_have_ip = False;
struct in_addr opt_dest_ip;

extern pstring global_myname;
extern BOOL AllowDebugChange;

/*
  run a function from a function table. If not found then
  call the specified usage function 
*/
int net_run_function(int argc, const char **argv, struct functable *table, 
		     int (*usage_fn)(int argc, const char **argv))
{
	int i;
	
	if (argc < 1) {
		d_printf("\nUsage: \n");
		return usage_fn(argc, argv);
	}
	for (i=0; table[i].funcname; i++) {
		if (StrCaseCmp(argv[0], table[i].funcname) == 0)
			return table[i].fn(argc-1, argv+1);
	}
	d_printf("No command: %s\n", argv[0]);
	return usage_fn(argc, argv);
}


/****************************************************************************
connect to \\server\ipc$  
****************************************************************************/
NTSTATUS connect_to_ipc(struct cli_state **c, struct in_addr *server_ip,
					const char *server_name)
{
	NTSTATUS nt_status;

	if (!opt_password) {
		char *pass = getpass("Password:");
		if (pass) {
			opt_password = strdup(pass);
		}
	}
	
	nt_status = cli_full_connection(c, opt_requester_name, server_name, 
					server_ip, opt_port,
					"IPC$", "IPC",  
					opt_user_name, opt_workgroup,
					opt_password);
	
	if (NT_STATUS_IS_OK(nt_status)) {
		return nt_status;
	} else {
		DEBUG(1,("Cannot connect to server.  Error was %s\n", 
			 nt_errstr(nt_status)));

		/* Display a nicer message depending on the result */

		if (NT_STATUS_V(nt_status) == 
		    NT_STATUS_V(NT_STATUS_LOGON_FAILURE))
			d_printf("The username or password was not correct.\n");

		return nt_status;
	}
}

/****************************************************************************
connect to \\server\ipc$ anonymously
****************************************************************************/
NTSTATUS connect_to_ipc_anonymous(struct cli_state **c,
			struct in_addr *server_ip, const char *server_name)
{
	NTSTATUS nt_status;

	nt_status = cli_full_connection(c, opt_requester_name, server_name, 
					server_ip, opt_port,
					"IPC$", "IPC",  
					"", "",
					"");
	
	if (NT_STATUS_IS_OK(nt_status)) {
		return nt_status;
	} else {
		DEBUG(1,("Cannot connect to server (anonymously).  Error was %s\n", nt_errstr(nt_status)));
		return nt_status;
	}
}

BOOL net_find_server(unsigned flags, struct in_addr *server_ip, char **server_name)
{

	if (opt_host) {
		*server_name = strdup(opt_host);
	}		

	if (opt_have_ip) {
		*server_ip = opt_dest_ip;
		if (!*server_name) {
			*server_name = strdup(inet_ntoa(opt_dest_ip));
		}
	} else if (*server_name) {
		/* resolve the IP address */
		if (!resolve_name(*server_name, server_ip, 0x20))  {
			DEBUG(1,("Unable to resolve server name\n"));
			return False;
		}
	} else if (flags & NET_FLAGS_PDC) {
		struct in_addr *ip_list;
		int addr_count;
		if (get_dc_list(True /* PDC only*/, opt_target_workgroup, &ip_list, &addr_count)) {
			fstring dc_name;
			if (addr_count < 1) {
				return False;
			}
			
			*server_ip = *ip_list;
			
			if (is_zero_ip(*server_ip))
				return False;
			
			if (!lookup_dc_name(global_myname, opt_target_workgroup, server_ip, dc_name))
				return False;
				
			*server_name = strdup(dc_name);
		}
		
	} else if (flags & NET_FLAGS_DMB) {
		struct in_addr msbrow_ip;
		/*  if (!resolve_name(MSBROWSE, &msbrow_ip, 1)) */
		if (!resolve_name(opt_target_workgroup, &msbrow_ip, 0x1B))  {
			DEBUG(1,("Unable to resolve domain browser via name lookup\n"));
			return False;
		} else {
			*server_ip = msbrow_ip;
		}
		*server_name = strdup(inet_ntoa(opt_dest_ip));
	} else if (flags & NET_FLAGS_MASTER) {
		struct in_addr brow_ips;
		if (!resolve_name(opt_target_workgroup, &brow_ips, 0x1D))  {
				/* go looking for workgroups */
			DEBUG(1,("Unable to resolve master browser via name lookup\n"));
			return False;
		} else {
			*server_ip = brow_ips;
		}
		*server_name = strdup(inet_ntoa(opt_dest_ip));
	} else if (!(flags & NET_FLAGS_LOCALHOST_DEFAULT_INSANE)) {
		extern struct in_addr loopback_ip;
		*server_ip = loopback_ip;
		*server_name = strdup("127.0.0.1");
	}

	if (!server_name || !*server_name) {
		DEBUG(1,("no server to connect to\n"));
		return False;
	}

	return True;
}


BOOL net_find_dc(struct in_addr *server_ip, fstring server_name, char *domain_name)
{
	struct in_addr *ip_list;
	int addr_count;

	if (get_dc_list(True /* PDC only*/, domain_name, &ip_list, &addr_count)) {
		fstring dc_name;
		if (addr_count < 1) {
			return False;
		}
			
		*server_ip = *ip_list;
		
		if (is_zero_ip(*server_ip))
			return False;
		
		if (!lookup_dc_name(global_myname, domain_name, server_ip, dc_name))
			return False;
			
		safe_strcpy(server_name, dc_name, FSTRING_LEN);
		return True;
	} else
		return False;
}


struct cli_state *net_make_ipc_connection(unsigned flags)
{
	char *server_name = NULL;
	struct in_addr server_ip;
	struct cli_state *cli = NULL;
	NTSTATUS nt_status;

	if (!net_find_server(flags, &server_ip, &server_name)) {
		d_printf("\nUnable to find a suitable server\n");
		return NULL;
	}

	if (flags & NET_FLAGS_ANONYMOUS) {
		nt_status = connect_to_ipc_anonymous(&cli, &server_ip, server_name);
	} else {
		nt_status = connect_to_ipc(&cli, &server_ip, server_name);
	}
	SAFE_FREE(server_name);
	return cli;
}

static int net_user(int argc, const char **argv)
{
	if (net_ads_check() == 0)
		return net_ads_user(argc, argv);

	/* if server is not specified, default to PDC? */
	if (net_rpc_check(NET_FLAGS_PDC))
		return net_rpc_user(argc, argv);

	return net_rap_user(argc, argv);
}

static int net_group(int argc, const char **argv)
{
	if (net_ads_check() == 0)
		return net_ads_group(argc, argv);

	if (argc == 0 && net_rpc_check(NET_FLAGS_PDC))
		return net_rpc_group(argc, argv);

	return net_rap_group(argc, argv);
}

static int net_join(int argc, const char **argv)
{
	if (net_ads_check() == 0) {
		if (net_ads_join(argc, argv) == 0)
			return 0;
		else
			d_printf("ADS join did not work, trying RPC...\n");
	}
	return net_rpc_join(argc, argv);
}

static int net_share(int argc, const char **argv)
{
	if (net_rpc_check(0))
		return net_rpc_share(argc, argv);
	return net_rap_share(argc, argv);
}

static int net_file(int argc, const char **argv)
{
	if (net_rpc_check(0))
		return net_rpc_file(argc, argv);
	return net_rap_file(argc, argv);
}

/* main function table */
static struct functable net_func[] = {
	{"RPC", net_rpc},
	{"RAP", net_rap},
	{"ADS", net_ads},

	/* eventually these should auto-choose the transport ... */
	{"FILE", net_file},
	{"SHARE", net_share},
	{"SESSION", net_rap_session},
	{"SERVER", net_rap_server},
	{"DOMAIN", net_rap_domain},
	{"PRINTQ", net_rap_printq},
	{"USER", net_user},
	{"GROUP", net_group},
	{"VALIDATE", net_rap_validate},
	{"GROUPMEMBER", net_rap_groupmember},
	{"ADMIN", net_rap_admin},
	{"SERVICE", net_rap_service},	
	{"PASSWORD", net_rap_password},
	{"TIME", net_time},
	{"LOOKUP", net_lookup},
	{"JOIN", net_join},

	{"HELP", net_help},
	{NULL, NULL}
};


/****************************************************************************
  main program
****************************************************************************/
 int main(int argc, const char **argv)
{
	int opt,i;
	char *p;
	int rc = 0;
	int argc_new = 0;
	const char ** argv_new;
	poptContext pc;
	static char *servicesf = dyn_CONFIGFILE;
	static char *debuglevel = NULL;

	struct poptOption long_options[] = {
		{"help",	'h', POPT_ARG_NONE,   0, 'h'},
		{"workgroup",	'w', POPT_ARG_STRING, &opt_target_workgroup},
		{"myworkgroup",	'W', POPT_ARG_STRING, &opt_workgroup},
		{"user",	'U', POPT_ARG_STRING, &opt_user_name, 'U'},
		{"ipaddress",	'I', POPT_ARG_STRING, 0,'I'},
		{"port",	'p', POPT_ARG_INT,    &opt_port},
		{"myname",	'n', POPT_ARG_STRING, &opt_requester_name},
		{"conf",	's', POPT_ARG_STRING, &servicesf},
		{"debug",	'd', POPT_ARG_STRING,    &debuglevel},
		{"debuglevel",	'd', POPT_ARG_STRING,    &debuglevel},
		{"server",	'S', POPT_ARG_STRING, &opt_host},
		{"comment",	'C', POPT_ARG_STRING, &opt_comment},
		{"maxusers",	'M', POPT_ARG_INT,    &opt_maxusers},
		{"flags",	'F', POPT_ARG_INT,    &opt_flags},
		{"jobid",	'j', POPT_ARG_INT,    &opt_jobid},
		{"long",	'l', POPT_ARG_NONE,   &opt_long_list_entries},
		{"reboot",	'r', POPT_ARG_NONE,   &opt_reboot},
		{"force",	'f', POPT_ARG_NONE,   &opt_force},
		{"timeout",	't', POPT_ARG_INT,    &opt_timeout},
		{ 0, 0, 0, 0}
	};

	zero_ip(&opt_dest_ip);

	dbf = x_stderr;
	
	pc = poptGetContext(NULL, argc, (const char **) argv, long_options, 
			    POPT_CONTEXT_KEEP_FIRST);
	
	while((opt = poptGetNextOpt(pc)) != -1) {
		switch (opt) {
		case 'h':
			net_help(argc, argv);
			exit(0);
			break;
		case 'I':
			opt_dest_ip = *interpret_addr2(poptGetOptArg(pc));
			if (is_zero_ip(opt_dest_ip))
				d_printf("\nInvalid ip address specified\n");
			else
				opt_have_ip = True;
			break;
		case 'U':
			opt_user_specified = True;
			opt_user_name = strdup(opt_user_name);
			p = strchr(opt_user_name,'%');
			if (p) {
				*p = 0;
				opt_password = p+1;
			}
			break;
		default:
			d_printf("\nInvalid option %c (%d)\n", (char)opt, opt);
			net_help(argc, argv);
		}
	}

	if (debuglevel) {
		debug_parse_levels(debuglevel);
		AllowDebugChange = False;
	}

	lp_load(servicesf,True,False,False);       

	argv_new = (const char **)poptGetArgs(pc);

	argc_new = argc;
	for (i=0; i<argc; i++) {
		if (argv_new[i] == NULL) {
			argc_new = i;
			break;
		}
	}
  	 
	if (!opt_requester_name) {
		static fstring myname;
		get_myname(myname);
		opt_requester_name = myname;
	}

	if (!opt_user_name && getenv("LOGNAME")) {
		opt_user_name = getenv("LOGNAME");
	}

	if (!opt_workgroup) {
		opt_workgroup = lp_workgroup();
	}
	
	if (!opt_target_workgroup) {
		opt_target_workgroup = lp_workgroup();
	}
	
	if (!*global_myname) {
		char *p2;

		fstrcpy(global_myname, myhostname());
		p2 = strchr_m(global_myname, '.');
		if (p2) 
                        *p2 = 0;
	}
	
	strupper(global_myname);

	load_interfaces();

	rc = net_run_function(argc_new-1, argv_new+1, net_func, net_help);
	
	DEBUG(2,("return code = %d\n", rc));
	return rc;
}
