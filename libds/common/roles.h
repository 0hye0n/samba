/*
   Unix SMB/CIFS implementation.

   domain roles

   Copyright (C) Andrew Tridgell 2011

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

#ifndef _LIBDS_ROLES_H_
#define _LIBDS_ROLES_H_

/* server roles. If you add new roles, please keep ensure that the
 * existing role values match samr_Role from samr.idl
 */
enum server_role {
	ROLE_STANDALONE    = 0,
	ROLE_DOMAIN_MEMBER = 1,
	ROLE_DOMAIN_BDC    = 2,
	ROLE_DOMAIN_PDC    = 3,

	/* To determine the role automatically, this is not a valid role */
	ROLE_AUTO          = 100
};

/* keep compatibility with the s4 'ROLE_DOMAIN_CONTROLLER' by mapping
 * it to ROLE_DOMAIN_BDC. The PDC/BDC split is really historical from
 * NT4 domains which were not multi-master, but even in AD there is
 * only one machine that has the PDC FSMO role in a domain.
*/
#define ROLE_DOMAIN_CONTROLLER ROLE_DOMAIN_BDC

/* security levels for 'security =' option */
enum security_types {SEC_AUTO, SEC_SHARE,SEC_USER,SEC_SERVER,SEC_DOMAIN,SEC_ADS};

#endif /* _LIBDS_ROLES_H_ */
