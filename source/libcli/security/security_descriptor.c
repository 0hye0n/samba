/* 
   Unix SMB/CIFS implementation.

   security descriptror utility functions

   Copyright (C) Andrew Tridgell 		2004
      
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
#include "libcli/security/security.h"

/*
  return a blank security descriptor (no owners, dacl or sacl)
*/
struct security_descriptor *security_descriptor_initialise(TALLOC_CTX *mem_ctx)
{
	struct security_descriptor *sd;

	sd = talloc(mem_ctx, struct security_descriptor);
	if (!sd) {
		return NULL;
	}

	sd->revision = SD_REVISION;
	/* we mark as self relative, even though it isn't while it remains
	   a pointer in memory because this simplifies the ndr code later.
	   All SDs that we store/emit are in fact SELF_RELATIVE
	*/
	sd->type = SEC_DESC_SELF_RELATIVE;

	sd->owner_sid = NULL;
	sd->group_sid = NULL;
	sd->sacl = NULL;
	sd->dacl = NULL;

	return sd;
}

static struct security_acl *security_acl_dup(TALLOC_CTX *mem_ctx,
					     const struct security_acl *oacl)
{
	struct security_acl *nacl;
	int i;

	nacl = talloc (mem_ctx, struct security_acl);
	if (nacl == NULL) {
		return NULL;
	}

	nacl->aces = (struct security_ace *)talloc_memdup (nacl, oacl->aces, sizeof(struct security_ace) * oacl->num_aces);
	if ((nacl->aces == NULL) && (oacl->num_aces > 0)) {
		goto failed;
	}

	/* remapping array in trustee dom_sid from old acl to new acl */

	for (i = 0; i < oacl->num_aces; i++) {
		nacl->aces[i].trustee.sub_auths = 
			(uint32_t *)talloc_memdup(nacl->aces, nacl->aces[i].trustee.sub_auths,
				      sizeof(uint32_t) * nacl->aces[i].trustee.num_auths);

		if ((nacl->aces[i].trustee.sub_auths == NULL) && (nacl->aces[i].trustee.num_auths > 0)) {
			goto failed;
		}
	}

	nacl->revision = oacl->revision;
	nacl->size = oacl->size;
	nacl->num_aces = oacl->num_aces;
	
	return nacl;

 failed:
	talloc_free (nacl);
	return NULL;
	
}

/* 
   talloc and copy a security descriptor
 */
struct security_descriptor *security_descriptor_copy(TALLOC_CTX *mem_ctx, 
						     const struct security_descriptor *osd)
{
	struct security_descriptor *nsd;

	nsd = talloc_zero(mem_ctx, struct security_descriptor);
	if (!nsd) {
		return NULL;
	}

	if (osd->owner_sid) {
		nsd->owner_sid = dom_sid_dup(nsd, osd->owner_sid);
		if (nsd->owner_sid == NULL) {
			goto failed;
		}
	}
	
	if (osd->group_sid) {
		nsd->group_sid = dom_sid_dup(nsd, osd->group_sid);
		if (nsd->group_sid == NULL) {
			goto failed;
		}
	}

	if (osd->sacl) {
		nsd->sacl = security_acl_dup(nsd, osd->sacl);
		if (nsd->sacl == NULL) {
			goto failed;
		}
	}

	if (osd->dacl) {
		nsd->dacl = security_acl_dup(nsd, osd->dacl);
		if (nsd->dacl == NULL) {
			goto failed;
		}
	}

	return nsd;

 failed:
	talloc_free(nsd);

	return NULL;
}

/*
  add an ACE to the DACL of a security_descriptor
*/
NTSTATUS security_descriptor_dacl_add(struct security_descriptor *sd, 
				      const struct security_ace *ace)
{
	if (sd->dacl == NULL) {
		sd->dacl = talloc(sd, struct security_acl);
		if (sd->dacl == NULL) {
			return NT_STATUS_NO_MEMORY;
		}
		sd->dacl->revision = SECURITY_ACL_REVISION_NT4;
		sd->dacl->size     = 0;
		sd->dacl->num_aces = 0;
		sd->dacl->aces     = NULL;
	}

	sd->dacl->aces = talloc_realloc(sd->dacl, sd->dacl->aces, 
					  struct security_ace, sd->dacl->num_aces+1);
	if (sd->dacl->aces == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	sd->dacl->aces[sd->dacl->num_aces] = *ace;
	sd->dacl->aces[sd->dacl->num_aces].trustee.sub_auths = 
		(uint32_t *)talloc_memdup(sd->dacl->aces, 
			      sd->dacl->aces[sd->dacl->num_aces].trustee.sub_auths,
			      sizeof(uint32_t) * 
			      sd->dacl->aces[sd->dacl->num_aces].trustee.num_auths);
	if (sd->dacl->aces[sd->dacl->num_aces].trustee.sub_auths == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	switch (sd->dacl->aces[sd->dacl->num_aces].type) {
	case SEC_ACE_TYPE_ACCESS_ALLOWED_OBJECT:
	case SEC_ACE_TYPE_ACCESS_DENIED_OBJECT:
	case SEC_ACE_TYPE_SYSTEM_AUDIT_OBJECT:
	case SEC_ACE_TYPE_SYSTEM_ALARM_OBJECT:
		sd->dacl->revision = SECURITY_ACL_REVISION_ADS;
		break;
	default:
		break;
	}

	sd->dacl->num_aces++;

	sd->type |= SEC_DESC_DACL_PRESENT;

	return NT_STATUS_OK;
}


/*
  delete the ACE corresponding to the given trustee in the DACL of a security_descriptor
*/
NTSTATUS security_descriptor_dacl_del(struct security_descriptor *sd, 
				      struct dom_sid *trustee)
{
	int i;
	bool found = false;

	if (sd->dacl == NULL) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	/* there can be multiple ace's for one trustee */
	for (i=0;i<sd->dacl->num_aces;i++) {
		if (dom_sid_equal(trustee, &sd->dacl->aces[i].trustee)) {
			memmove(&sd->dacl->aces[i], &sd->dacl->aces[i+1],
				sizeof(sd->dacl->aces[i]) * (sd->dacl->num_aces - (i+1)));
			sd->dacl->num_aces--;
			if (sd->dacl->num_aces == 0) {
				sd->dacl->aces = NULL;
			}
			found = true;
		}
	}

	if (!found) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	sd->dacl->revision = SECURITY_ACL_REVISION_NT4;

	for (i=0;i<sd->dacl->num_aces;i++) {
		switch (sd->dacl->aces[i].type) {
		case SEC_ACE_TYPE_ACCESS_ALLOWED_OBJECT:
		case SEC_ACE_TYPE_ACCESS_DENIED_OBJECT:
		case SEC_ACE_TYPE_SYSTEM_AUDIT_OBJECT:
		case SEC_ACE_TYPE_SYSTEM_ALARM_OBJECT:
			sd->dacl->revision = SECURITY_ACL_REVISION_ADS;
			return NT_STATUS_OK;
		default:
			break; /* only for the switch statement */
		}
	}

	return NT_STATUS_OK;
}


/*
  compare two security ace structures
*/
bool security_ace_equal(const struct security_ace *ace1, 
			const struct security_ace *ace2)
{
	if (ace1 == ace2) return true;
	if (!ace1 || !ace2) return false;
	if (ace1->type != ace2->type) return false;
	if (ace1->flags != ace2->flags) return false;
	if (ace1->access_mask != ace2->access_mask) return false;
	if (!dom_sid_equal(&ace1->trustee, &ace2->trustee)) return false;

	return true;	
}


/*
  compare two security acl structures
*/
bool security_acl_equal(const struct security_acl *acl1, 
			const struct security_acl *acl2)
{
	int i;

	if (acl1 == acl2) return true;
	if (!acl1 || !acl2) return false;
	if (acl1->revision != acl2->revision) return false;
	if (acl1->num_aces != acl2->num_aces) return false;

	for (i=0;i<acl1->num_aces;i++) {
		if (!security_ace_equal(&acl1->aces[i], &acl2->aces[i])) return false;
	}
	return true;	
}

/*
  compare two security descriptors.
*/
bool security_descriptor_equal(const struct security_descriptor *sd1, 
			       const struct security_descriptor *sd2)
{
	if (sd1 == sd2) return true;
	if (!sd1 || !sd2) return false;
	if (sd1->revision != sd2->revision) return false;
	if (sd1->type != sd2->type) return false;

	if (!dom_sid_equal(sd1->owner_sid, sd2->owner_sid)) return false;
	if (!dom_sid_equal(sd1->group_sid, sd2->group_sid)) return false;
	if (!security_acl_equal(sd1->sacl, sd2->sacl))      return false;
	if (!security_acl_equal(sd1->dacl, sd2->dacl))      return false;

	return true;	
}

/*
  compare two security descriptors, but allow certain (missing) parts
  to be masked out of the comparison
*/
bool security_descriptor_mask_equal(const struct security_descriptor *sd1, 
				    const struct security_descriptor *sd2, 
				    uint32_t mask)
{
	if (sd1 == sd2) return true;
	if (!sd1 || !sd2) return false;
	if (sd1->revision != sd2->revision) return false;
	if ((sd1->type & mask) != (sd2->type & mask)) return false;

	if (!dom_sid_equal(sd1->owner_sid, sd2->owner_sid)) return false;
	if (!dom_sid_equal(sd1->group_sid, sd2->group_sid)) return false;
	if ((mask & SEC_DESC_DACL_PRESENT) && !security_acl_equal(sd1->dacl, sd2->dacl))      return false;
	if ((mask & SEC_DESC_SACL_PRESENT) && !security_acl_equal(sd1->sacl, sd2->sacl))      return false;

	return true;	
}


/*
  create a security descriptor using string SIDs. This is used by the
  torture code to allow the easy creation of complex ACLs
  This is a varargs function. The list of DACL ACEs ends with a NULL sid.

  Each ACE contains a set of 4 parameters:
  SID, ACCESS_TYPE, MASK, FLAGS

  a typical call would be:

    sd = security_descriptor_create(mem_ctx,
                                    sd_type_flags,
                                    mysid,
				    mygroup,
				    SID_NT_AUTHENTICATED_USERS, 
				    SEC_ACE_TYPE_ACCESS_ALLOWED,
				    SEC_FILE_ALL,
				    SEC_ACE_FLAG_OBJECT_INHERIT,
				    NULL);
  that would create a sd with one DACL ACE
*/

struct security_descriptor *security_descriptor_append(struct security_descriptor *sd,
						       ...)
{
	va_list ap;
	const char *sidstr;

	va_start(ap, sd);
	while ((sidstr = va_arg(ap, const char *))) {
		struct dom_sid *sid;
		struct security_ace *ace = talloc(sd, struct security_ace);
		NTSTATUS status;

		if (ace == NULL) {
			talloc_free(sd);
			va_end(ap);
			return NULL;
		}
		ace->type = va_arg(ap, unsigned int);
		ace->access_mask = va_arg(ap, unsigned int);
		ace->flags = va_arg(ap, unsigned int);
		sid = dom_sid_parse_talloc(ace, sidstr);
		if (sid == NULL) {
			va_end(ap);
			talloc_free(sd);
			return NULL;
		}
		ace->trustee = *sid;
		status = security_descriptor_dacl_add(sd, ace);
		/* TODO: check: would talloc_free(ace) here be correct? */
		if (!NT_STATUS_IS_OK(status)) {
			va_end(ap);
			talloc_free(sd);
			return NULL;
		}
	}
	va_end(ap);

	return sd;

}

struct security_descriptor *security_descriptor_create(TALLOC_CTX *mem_ctx,
						       uint16_t sd_type,
						       const char *owner_sid,
						       const char *group_sid,
						       ...)
{
	va_list ap;
	struct security_descriptor *sd;

	sd = security_descriptor_initialise(mem_ctx);
	if (sd == NULL) return NULL;

	sd->type |= sd_type;

	if (owner_sid) {
		sd->owner_sid = dom_sid_parse_talloc(sd, owner_sid);
		if (sd->owner_sid == NULL) {
			talloc_free(sd);
			return NULL;
		}
	}
	if (group_sid) {
		sd->group_sid = dom_sid_parse_talloc(sd, group_sid);
		if (sd->group_sid == NULL) {
			talloc_free(sd);
			return NULL;
		}
	}

	va_start(ap, group_sid);
	sd = security_descriptor_append(sd, ap);
	va_end(ap);

	return sd;
}
