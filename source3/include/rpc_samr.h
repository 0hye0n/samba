/* 
   Unix SMB/CIFS implementation.
   SMB parameters and setup
   Copyright (C) Andrew Tridgell              1992-2000
   Copyright (C) Luke Kenneth Casson Leighton 1996-2000
   Copyright (C) Paul Ashton                  1997-2000
   Copyright (C) Jean François Micouleau      1998-2001
   Copyright (C) Jim McDonough <jmcd@us.ibm.com> 2002
   
   
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

#ifndef _RPC_SAMR_H /* _RPC_SAMR_H */
#define _RPC_SAMR_H 

/*******************************************************************
 the following information comes from a QuickView on samsrv.dll,
 and gives an idea of exactly what is needed:
 
x SamrAddMemberToAlias
x SamrAddMemberToGroup
SamrAddMultipleMembersToAlias
x SamrChangePasswordUser
x SamrCloseHandle
x SamrConnect
x SamrCreateAliasInDomain
x SamrCreateGroupInDomain
x SamrCreateUserInDomain
? SamrDeleteAlias
SamrDeleteGroup
x SamrDeleteUser
x SamrEnumerateAliasesInDomain
SamrEnumerateDomainsInSamServer
x SamrEnumerateGroupsInDomain
x SamrEnumerateUsersInDomain
SamrGetUserDomainPasswordInformation
SamrLookupDomainInSamServer
? SamrLookupIdsInDomain
x SamrLookupNamesInDomain
x SamrOpenAlias
x SamrOpenDomain
x SamrOpenGroup
x SamrOpenUser
x SamrQueryDisplayInformation
x SamrQueryInformationAlias
SamrQueryInformationDomain
? SamrQueryInformationUser
x SamrQuerySecurityObject
SamrRemoveMemberFromAlias
SamrRemoveMemberFromForiegnDomain
SamrRemoveMemberFromGroup
SamrRemoveMultipleMembersFromAlias
x SamrSetInformationAlias
SamrSetInformationDomain
x SamrSetInformationGroup
x SamrSetInformationUser
SamrSetMemberAttributesOfGroup
SamrSetSecurityObject
SamrShutdownSamServer
SamrTestPrivateFunctionsDomain
SamrTestPrivateFunctionsUser

********************************************************************/

#define PASS_MUST_CHANGE_AT_NEXT_LOGON	0x01
#define PASS_DONT_CHANGE_AT_NEXT_LOGON	0x00

#define MAX_SAM_ENTRIES_W2K 0x400
#define MAX_SAM_ENTRIES_W95 50
/* The following should be the greater of the preceeding two. */
#define MAX_SAM_ENTRIES MAX_SAM_ENTRIES_W2K

/* these are from the old rpc_samr.h - they are needed while the merge
   is still going on */
#define MAX_SAM_SIDS 15

#endif /* _RPC_SAMR_H */
