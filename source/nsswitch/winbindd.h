/* 
   Unix SMB/Netbios implementation.
   Version 2.0

   Winbind daemon for ntdom nss module

   Copyright (C) Tim Potter 2000
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.
   
   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA  02111-1307, USA.   
*/

#ifndef _WINBINDD_H
#define _WINBINDD_H

#include "includes.h"
#include "nterr.h"

#include "winbindd_nss.h"

/* Naughty global stuff */

extern int DEBUGLEVEL;

/* Client state structure */

struct winbindd_cli_state {
    struct winbindd_cli_state *prev, *next;   /* Linked list pointers */
    int sock;                                 /* Open socket from client */
    pid_t pid;                                /* pid of client */
    int read_buf_len, write_buf_len;          /* Indexes in request/response */
    BOOL finished;                            /* Can delete from list */
    BOOL write_extra_data;                    /* Write extra_data field */
    struct winbindd_request request;          /* Request from client */
    struct winbindd_response response;        /* Respose to client */
    struct getent_state *getpwent_state;      /* State for getpwent() */
    struct getent_state *getgrent_state;      /* State for getgrent() */
};

/* State between get{pw,gr}ent() calls */

struct getent_state {
	struct getent_state *prev, *next;
	void *sam_entries;
	uint32 sam_entry_index, num_sam_entries;
	uint32 dispinfo_ndx;
	uint32 grp_query_start_ndx;
	BOOL got_all_sam_entries, got_sam_entries;
	struct winbindd_domain *domain;
};

/* Storage for cached getpwent() user entries */

struct getpwent_user {
	fstring name;                        /* Account name */
	fstring gecos;                       /* User information */
	uint32 user_rid, group_rid;          /* NT user and group rids */
};

/* Server state structure */

struct winbindd_state {
	/* Netbios name of PDC */
	fstring controller;
	
	/* User and group id pool */
	uid_t uid_low, uid_high;               /* Range of uids to allocate */
	gid_t gid_low, gid_high;               /* Range of gids to allocate */
	
	/* Cached handle to lsa pipe */
	POLICY_HND lsa_handle;
	BOOL lsa_handle_open;
	BOOL pwdb_initialised;
};

extern struct winbindd_state server_state;  /* Server information */

/* Structures to hold per domain information */

struct winbindd_domain {

	/* Domain information */

	fstring name;                          /* Domain name */
	fstring controller;                    /* NetBIOS name of DC */
	
	DOM_SID sid;                           /* SID for this domain */
	BOOL got_domain_info;                  /* Got controller and sid */
	
	/* Cached handles to samr pipe */
	
	POLICY_HND sam_handle, sam_dom_handle;
	BOOL sam_handle_open, sam_dom_handle_open;
	time_t last_check;
	
	struct winbindd_domain *prev, *next;   /* Linked list info */
};

extern struct winbindd_domain *domain_list;  /* List of domains we know */

#include "winbindd_proto.h"

#include "rpc_parse.h"
#include "rpc_client.h"

#define WINBINDD_ESTABLISH_LOOP 30
#define DOM_SEQUENCE_NONE ((uint32)-1)
#define WINBINDD_MAX_CACHE_SIZE (50*1024*1024) /* 50 Mb max cache size */

/* SETENV */
#if HAVE_SETENV
#define SETENV(name, value, overwrite) setenv(name,value,overwrite)
#elif HAVE_PUTENV
#define SETENV(name, value, overwrite)					 \
{									 \
	fstring envvar;							 \
	slprintf(envvar, sizeof(fstring), "%s=%s", name, value);	 \
	putenv(envvar);							 \
}
#else
#define SETENV(name, value, overwrite) ;
#endif

#endif /* _WINBINDD_H */
