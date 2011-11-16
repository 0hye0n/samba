/*
   Unix SMB/CIFS implementation.
   Parameter loading functions
   Copyright (C) Karl Auer 1993-1998

   Largely re-written by Andrew Tridgell, September 1994

   Copyright (C) Simo Sorce 2001
   Copyright (C) Alexander Bokovoy 2002
   Copyright (C) Stefan (metze) Metzmacher 2002
   Copyright (C) Jim McDonough <jmcd@us.ibm.com> 2003
   Copyright (C) Michael Adam 2008
   Copyright (C) Andrew Bartlett 2010

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "includes.h"
#include "lib/param/loadparm.h"
#include "libds/common/roles.h"

/*******************************************************************
 Set the server type we will announce as via nmbd.
********************************************************************/

static const struct srv_role_tab {
	uint32_t role;
	const char *role_str;
} srv_role_tab [] = {
	{ ROLE_STANDALONE, "ROLE_STANDALONE" },
	{ ROLE_DOMAIN_MEMBER, "ROLE_DOMAIN_MEMBER" },
	{ ROLE_DOMAIN_BDC, "ROLE_DOMAIN_BDC" },
	{ ROLE_DOMAIN_PDC, "ROLE_DOMAIN_PDC" },
	{ 0, NULL }
};

const char* server_role_str(uint32_t role)
{
	int i = 0;
	for (i=0; srv_role_tab[i].role_str; i++) {
		if (role == srv_role_tab[i].role) {
			return srv_role_tab[i].role_str;
		}
	}
	return NULL;
}

/**
 * Set the server role based on security, domain logons and domain master
 */
int lp_find_server_role(int server_role, int security, bool domain_logons, bool domain_master)
{
	int role;

	if (server_role != ROLE_AUTO) {
		return server_role;
	}

	/* If server_role is set to ROLE_AUTO, figure out the correct role */
	role = ROLE_STANDALONE;

	switch (security) {
		case SEC_SHARE:
			if (domain_logons) {
				DEBUG(0, ("Server's Role (logon server) conflicts with share-level security\n"));
			}
			break;
		case SEC_SERVER:
			if (domain_logons) {
				DEBUG(0, ("Server's Role (logon server) conflicts with server-level security\n"));
			}
			/* this used to be considered ROLE_DOMAIN_MEMBER but that's just wrong */
			role = ROLE_STANDALONE;
			break;
		case SEC_DOMAIN:
			if (domain_logons) {
				DEBUG(1, ("Server's Role (logon server) NOT ADVISED with domain-level security\n"));
				role = ROLE_DOMAIN_BDC;
				break;
			}
			role = ROLE_DOMAIN_MEMBER;
			break;
		case SEC_ADS:
			if (domain_logons) {
				role = ROLE_DOMAIN_CONTROLLER;
				break;
			}
			role = ROLE_DOMAIN_MEMBER;
			break;
		case SEC_AUTO:
		case SEC_USER:
			if (domain_logons) {

				if (domain_master) {
					role = ROLE_DOMAIN_PDC;
				} else {
					role = ROLE_DOMAIN_BDC;
				}
			}
			break;
		default:
			DEBUG(0, ("Server's Role undefined due to unknown security mode\n"));
			break;
	}

	return role;
}

/**
 * Set the server role based on security, domain logons and domain master
 */
int lp_find_security(int server_role, int security)
{
	if (security != SEC_AUTO) {
		return security;
	}

	switch (server_role) {
	case ROLE_AUTO:
	case ROLE_STANDALONE:
		return SEC_USER;
	case ROLE_DOMAIN_MEMBER:
#if (defined(HAVE_ADS) || _SAMBA_BUILD_ >= 4)
		return SEC_ADS;
#else
		return SEC_DOMAIN;
#endif
	case ROLE_DOMAIN_PDC:
	case ROLE_DOMAIN_BDC:
	default:
		return SEC_USER;
	}
}


/**
 * Check if server role and security parameters are contradictory
 */
bool lp_is_security_and_server_role_valid(int server_role, int security)
{
	bool valid = false;

	if (server_role == ROLE_AUTO || security == SEC_AUTO) {
		return false;
	}

	switch (server_role) {
	case ROLE_STANDALONE:
		if (security == SEC_SHARE || security == SEC_SERVER || security == SEC_USER) {
			valid = true;
		}
		break;

	case ROLE_DOMAIN_MEMBER:
		if (security == SEC_ADS || security == SEC_DOMAIN) {
			valid = true;
		}
		break;

	case ROLE_DOMAIN_PDC:
	case ROLE_DOMAIN_BDC:
		if (security == SEC_USER) {
			valid = true;
		}
		break;

	default:
		break;
	}

	return valid;
}
