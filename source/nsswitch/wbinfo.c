/* 
   Unix SMB/Netbios implementation.
   Version 2.0

   Winbind status program.

   Copyright (C) Tim Potter 2000
   
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
#include "winbind_nss_config.h"
#include "winbindd.h"
#include "debug.h"

/* Prototypes from common.h - only needed #if TNG */

enum nss_status winbindd_request(int req_type, 
				 struct winbindd_request *request,
				 struct winbindd_response *response);

static BOOL wbinfo_get_usergroups(char *user)
{
	struct winbindd_request request;
	struct winbindd_response response;
	int result, i;
	
	ZERO_STRUCT(response);

	/* Send request */

	fstrcpy(request.data.username, user);

	result = winbindd_request(WINBINDD_GETGROUPS, &request, &response);

	if (result != NSS_STATUS_SUCCESS) {
		return False;
	}

	for (i = 0; i < response.data.num_entries; i++) {
		printf("%d\n", ((gid_t *)response.extra_data)[i]);
	}

	return True;
}

/* List trusted domains */

static BOOL wbinfo_list_domains(void)
{
	struct winbindd_response response;
	fstring name;

	ZERO_STRUCT(response);

	/* Send request */

	if (winbindd_request(WINBINDD_LIST_TRUSTDOM, NULL, &response) ==
	    WINBINDD_ERROR) {
		return False;
	}

	/* Display response */

	if (response.extra_data) {
		while(next_token((char **)&response.extra_data, name, ",", 
				 sizeof(fstring))) {
			printf("%s\n", name);
		}
	}

	return True;
}

/* Check trust account password */

static BOOL wbinfo_check_secret(void)
{
	return False;
}

/* Convert uid to sid */

static BOOL wbinfo_uid_to_sid(uid_t uid)
{
	struct winbindd_request request;
	struct winbindd_response response;

	ZERO_STRUCT(request);
	ZERO_STRUCT(response);

	/* Send request */

	request.data.uid = uid;
	if (winbindd_request(WINBINDD_UID_TO_SID, &request, &response) ==
	    WINBINDD_ERROR) {
		return False;
	}

	/* Display response */

	printf("%s\n", response.data.sid.sid);

	return True;
}

/* Convert gid to sid */

static BOOL wbinfo_gid_to_sid(gid_t gid)
{
	struct winbindd_request request;
	struct winbindd_response response;

	ZERO_STRUCT(request);
	ZERO_STRUCT(response);

	/* Send request */

	request.data.gid = gid;
	if (winbindd_request(WINBINDD_GID_TO_SID, &request, &response) ==
	    WINBINDD_ERROR) {
		return False;
	}

	/* Display response */

	printf("%s\n", response.data.sid.sid);

	return True;
}

/* Convert sid to uid */

static BOOL wbinfo_sid_to_uid(char *sid)
{
	struct winbindd_request request;
	struct winbindd_response response;

	ZERO_STRUCT(request);
	ZERO_STRUCT(response);

	/* Send request */

	fstrcpy(request.data.sid, sid);
	if (winbindd_request(WINBINDD_SID_TO_UID, &request, &response) ==
	    WINBINDD_ERROR) {
		return False;
	}

	/* Display response */

	printf("%d\n", response.data.uid);

	return True;
}

static BOOL wbinfo_sid_to_gid(char *sid)
{
	struct winbindd_request request;
	struct winbindd_response response;

	ZERO_STRUCT(request);
	ZERO_STRUCT(response);

	/* Send request */

	fstrcpy(request.data.sid, sid);
	if (winbindd_request(WINBINDD_SID_TO_GID, &request, &response) ==
	    WINBINDD_ERROR) {
		return False;
	}

	/* Display response */

	printf("%d\n", response.data.gid);

	return True;
}

/* Convert sid to string */

static BOOL wbinfo_lookupsid(char *sid)
{
	struct winbindd_request request;
	struct winbindd_response response;

	ZERO_STRUCT(request);
	ZERO_STRUCT(response);

	/* Send off request */

	fstrcpy(request.data.sid, sid);
	if (winbindd_request(WINBINDD_LOOKUPSID, &request, &response) ==
	    WINBINDD_ERROR) {
		return False;
	}

	/* Display response */

	printf("%s %d\n", response.data.name.name, response.data.name.type);

	return True;
}

/* Convert string to sid */

static BOOL wbinfo_lookupname(char *name)
{
	struct winbindd_request request;
	struct winbindd_response response;

	/* Send off request */

	ZERO_STRUCT(request);
	ZERO_STRUCT(response);

	fstrcpy(request.data.name, name);
	if (winbindd_request(WINBINDD_LOOKUPNAME, &request, &response) ==
	    WINBINDD_ERROR) {
		return False;
	}

	/* Display response */

	printf("%s %d\n", response.data.sid.sid, response.data.sid.type);

	return True;
}

/* Print domain users */

static BOOL print_domain_users(void)
{
	struct winbindd_response response;
	fstring name;

	/* Send request to winbind daemon */

	ZERO_STRUCT(response);

	if (winbindd_request(WINBINDD_LIST_USERS, NULL, &response) ==
	    WINBINDD_ERROR) {
		return False;
	}

	/* Look through extra data */

	if (!response.extra_data) {
		return False;
	}

	while(next_token((char **)&response.extra_data, name, ",", 
			 sizeof(fstring))) {
		printf("%s\n", name);
	}
	
	return True;
}

/* Print domain groups */

static BOOL print_domain_groups(void)
{
	struct winbindd_response response;
	fstring name;

	ZERO_STRUCT(response);

	if (winbindd_request(WINBINDD_LIST_GROUPS, NULL, &response) ==
	    WINBINDD_ERROR) {
		return False;
	}

	/* Look through extra data */

	if (!response.extra_data) {
		return False;
	}

	while(next_token((char **)&response.extra_data, name, ",", 
			 sizeof(fstring))) {
		printf("%s\n", name);
	}
	
	return True;
}

/* Print program usage */

static void usage(void)
{
	printf("Usage: wbinfo -ug | -n name | -sSY sid | -UG uid/gid | -tm\n");
	printf("\t-u\tlists all domain users\n");
	printf("\t-g\tlists all domain groups\n");
	printf("\t-n name\tconverts name to sid\n");
	printf("\t-s sid\tconverts sid to name\n");
	printf("\t-U uid\tconverts uid to sid\n");
	printf("\t-G gid\tconverts gid to sid\n");
	printf("\t-S sid\tconverts sid to uid\n");
	printf("\t-Y sid\tconverts sid to gid\n");
	printf("\t-t\tcheck shared secret\n");
	printf("\t-m\tlist trusted domains\n");
	printf("\t-r user\tget user groups\n");
}

/* Main program */

int main(int argc, char **argv)
{
	extern pstring global_myname;
	int opt;

	/* Samba client initialisation */

	if (!*global_myname) {
		char *p;

		fstrcpy(global_myname, myhostname());
		p = strchr(global_myname, '.');
		if (p) {
			*p = 0;
		}
	}

	TimeInit();
	charset_initialise();

	if (!lp_load(CONFIGFILE, True, False, False)) {
		DEBUG(0, ("error opening config file\n"));
		exit(1);
	}
	
	codepage_initialise(lp_client_code_page());
	load_interfaces();

	/* Parse command line options */

	if (argc == 1) {
		usage();
		return 1;
	}

	while ((opt = getopt(argc, argv, "ugs:n:U:G:S:Y:tmr:")) != EOF) {
		switch (opt) {
		case 'u':
			if (!print_domain_users()) {
				printf("Error looking up domain users\n");
				return 1;
			}
			break;
		case 'g':
			if (!print_domain_groups()) {
				printf("Error looking up domain groups\n");
				return 1;
			}
			break;
		case 's':
			if (!wbinfo_lookupsid(optarg)) {
				printf("Could not lookup sid %s\n", optarg);
				return 1;
			}
			break;
		case 'n':
			if (!wbinfo_lookupname(optarg)) {
				printf("Could not lookup name %s\n", optarg);
				return 1;
			}
			break;
		case 'U':
			if (!wbinfo_uid_to_sid(atoi(optarg))) {
				printf("Could not convert uid %s to sid\n",
				       optarg);
				return 1;
			}
			break;
		case 'G':
			if (!wbinfo_gid_to_sid(atoi(optarg))) {
				printf("Could not convert gid %s to sid\n",
				       optarg);
				return 1;
			}
			break;
		case 'S':
			if (!wbinfo_sid_to_uid(optarg)) {
				printf("Could not convert sid %s to uid\n",
				       optarg);
				return 1;
			}
			break;
		case 'Y':
			if (!wbinfo_sid_to_gid(optarg)) {
				printf("Could not convert sid %s to gid\n",
				       optarg);
				return 1;
			}
			break;
		case 't':
			if (!wbinfo_check_secret()) {
				printf("Could not check secret\n");
				return 1;
			}
			break;
		case 'm':
			if (!wbinfo_list_domains()) {
				printf("Could not list trusted domains\n");
				return 1;
			}
			break;
		case 'r':
			if (!wbinfo_get_usergroups(optarg)) {
				printf("Could not get groups for user %s\n", 
				       optarg);
				return 1;
			}
			break;

			/* Invalid option */

		default:
			usage();
			return 1;
		}
	}
	
	/* Clean exit */

	return 0;
}
