/* 
   Unix SMB/CIFS implementation.

   DRSUapi tests

   Copyright (C) Andrew Tridgell 2003
   Copyright (C) Stefan (metze) Metzmacher 2004
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2005

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
#include "librpc/gen_ndr/ndr_drsuapi.h"
#include "torture/rpc/drsuapi.h"
#include "ldb/include/ldb.h"

static BOOL test_DsCrackNamesMatrix(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
				    struct DsPrivate *priv, const char *dn,
				    const char *user_principal_name, const char *service_principal_name)
{
	

	NTSTATUS status;
	BOOL ret = True;
	struct drsuapi_DsCrackNames r;
	enum drsuapi_DsNameFormat formats[] = {
		DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
		DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT,
		DRSUAPI_DS_NAME_FORMAT_DISPLAY,
		DRSUAPI_DS_NAME_FORMAT_GUID,
		DRSUAPI_DS_NAME_FORMAT_CANONICAL,
		DRSUAPI_DS_NAME_FORMAT_USER_PRINCIPAL,
		DRSUAPI_DS_NAME_FORMAT_CANONICAL_EX,
		DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL,
		DRSUAPI_DS_NAME_FORMAT_SID_OR_SID_HISTORY,
		DRSUAPI_DS_NAME_FORMAT_DNS_DOMAIN
	};
	struct drsuapi_DsNameString names[ARRAY_SIZE(formats)];
	int i, j;

	const char *n_matrix[ARRAY_SIZE(formats)][ARRAY_SIZE(formats)];
	const char *n_from[ARRAY_SIZE(formats)];

	ZERO_STRUCT(r);
	r.in.bind_handle		= &priv->bind_handle;
	r.in.level			= 1;
	r.in.req.req1.unknown1		= 0x000004e4;
	r.in.req.req1.unknown2		= 0x00000407;
	r.in.req.req1.count		= 1;
	r.in.req.req1.names		= names;
	r.in.req.req1.format_flags	= DRSUAPI_DS_NAME_FLAG_NO_FLAGS;

	n_matrix[0][0] = dn;

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		r.in.req.req1.format_offered	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779;
		r.in.req.req1.format_desired	= formats[i];
		names[0].str = dn;
		printf("testing DsCrackNames (matrix prep) with name '%s' from format: %d desired format:%d ",
		       names[0].str, r.in.req.req1.format_offered, r.in.req.req1.format_desired);
		
		status = dcerpc_drsuapi_DsCrackNames(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			const char *errstr = nt_errstr(status);
			if (NT_STATUS_EQUAL(status, NT_STATUS_NET_WRITE_FAULT)) {
				errstr = dcerpc_errstr(mem_ctx, p->last_fault_code);
			}
			printf("dcerpc_drsuapi_DsCrackNames failed - %s\n", errstr);
			ret = False;
		} else if (!W_ERROR_IS_OK(r.out.result)) {
			printf("DsCrackNames failed - %s\n", win_errstr(r.out.result));
			ret = False;
		}
			
		if (!ret) {
			return ret;
		}
		switch (formats[i]) {
		case DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL:	
			if (r.out.ctr.ctr1->array[0].status != DRSUAPI_DS_NAME_STATUS_NOT_UNIQUE) {
				printf(__location__ ": Unexpected error (%d): This name lookup should fail\n", 
				       r.out.ctr.ctr1->array[0].status);
				return False;
			}
			printf ("(expected) error\n");
			break;
		case DRSUAPI_DS_NAME_FORMAT_USER_PRINCIPAL:
			if (r.out.ctr.ctr1->array[0].status != DRSUAPI_DS_NAME_STATUS_NO_MAPPING) {
				printf(__location__ ": Unexpected error (%d): This name lookup should fail\n", 
				       r.out.ctr.ctr1->array[0].status);
				return False;
			}
			printf ("(expected) error\n");
			break;
		case DRSUAPI_DS_NAME_FORMAT_DNS_DOMAIN:	
		case DRSUAPI_DS_NAME_FORMAT_SID_OR_SID_HISTORY:	
			if (r.out.ctr.ctr1->array[0].status != DRSUAPI_DS_NAME_STATUS_RESOLVE_ERROR) {
				printf(__location__ ": Unexpected error (%d): This name lookup should fail\n", 
				       r.out.ctr.ctr1->array[0].status);
				return False;
			}
			printf ("(expected) error\n");
			break;
		default:
			if (r.out.ctr.ctr1->array[0].status != DRSUAPI_DS_NAME_STATUS_OK) {
				printf("Error: %d\n", r.out.ctr.ctr1->array[0].status);
				return False;
			}
		}

		switch (formats[i]) {
		case DRSUAPI_DS_NAME_FORMAT_USER_PRINCIPAL:
			n_from[i] = user_principal_name;
			break;
		case DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL:	
			n_from[i] = service_principal_name;
			break;
		case DRSUAPI_DS_NAME_FORMAT_SID_OR_SID_HISTORY:	
		case DRSUAPI_DS_NAME_FORMAT_DNS_DOMAIN:	
			n_from[i] = NULL;
			break;
		default:
			n_from[i] = r.out.ctr.ctr1->array[0].result_name;
			printf("%s\n", n_from[i]);
		}
	}

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		for (j = 0; j < ARRAY_SIZE(formats); j++) {
			r.in.req.req1.format_offered	= formats[i];
			r.in.req.req1.format_desired	= formats[j];
			if (!n_from[i]) {
				n_matrix[i][j] = NULL;
				continue;
			}
			names[0].str = n_from[i];
			status = dcerpc_drsuapi_DsCrackNames(p, mem_ctx, &r);
			if (!NT_STATUS_IS_OK(status)) {
				const char *errstr = nt_errstr(status);
				if (NT_STATUS_EQUAL(status, NT_STATUS_NET_WRITE_FAULT)) {
					errstr = dcerpc_errstr(mem_ctx, p->last_fault_code);
				}
				printf("testing DsCrackNames (matrix) with name '%s' from format: %d desired format:%d failed - %s",
				       names[0].str, r.in.req.req1.format_offered, r.in.req.req1.format_desired, errstr);
				ret = False;
			} else if (!W_ERROR_IS_OK(r.out.result)) {
				printf("testing DsCrackNames (matrix) with name '%s' from format: %d desired format:%d failed - %s",
				       names[0].str, r.in.req.req1.format_offered, r.in.req.req1.format_desired, 
				       win_errstr(r.out.result));
				ret = False;
			}
			
			if (!ret) {
				return ret;
			}
			if (r.out.ctr.ctr1->array[0].status == DRSUAPI_DS_NAME_STATUS_OK) {
				n_matrix[i][j] = r.out.ctr.ctr1->array[0].result_name;
			} else {
				n_matrix[i][j] = NULL;
			}
		}
	}

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		for (j = 0; j < ARRAY_SIZE(formats); j++) {
			if (n_matrix[i][j] == n_from[j]) {
				
			/* We don't have a from name for these yet (and we can't map to them to find it out) */
			} else if (n_matrix[i][j] == NULL && n_from[i] == NULL) {
				
			/* we can't map to these two */
			} else if (n_matrix[i][j] == NULL && formats[j] == DRSUAPI_DS_NAME_FORMAT_USER_PRINCIPAL) {
			} else if (n_matrix[i][j] == NULL && formats[j] == DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL) {
			} else if (n_matrix[i][j] == NULL && n_from[j] != NULL) {
				printf("dcerpc_drsuapi_DsCrackNames mismatch - from %d to %d: %s should be %s\n", formats[i], formats[j], n_matrix[i][j], n_from[j]);
				ret = False;
			} else if (n_matrix[i][j] != NULL && n_from[j] == NULL) {
				printf("dcerpc_drsuapi_DsCrackNames mismatch - from %d to %d: %s should be %s\n", formats[i], formats[j], n_matrix[i][j], n_from[j]);
				ret = False;
			} else if (strcmp(n_matrix[i][j], n_from[j]) != 0) {
				printf("dcerpc_drsuapi_DsCrackNames mismatch - from %d to %d: %s should be %s\n", formats[i], formats[j], n_matrix[i][j], n_from[j]);
				ret = False;
			}
		}
	}
	return ret;
}

BOOL test_DsCrackNames(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			      struct DsPrivate *priv, const char *test_dc)
{
	NTSTATUS status;
	struct drsuapi_DsCrackNames r;
	struct drsuapi_DsNameString names[1];
	BOOL ret = True;
	const char *dns_domain;
	const char *nt4_domain;
	const char *FQDN_1779_name;
	struct ldb_dn *FQDN_1779_dn;
	struct ldb_dn *realm_dn;
	const char *realm_dn_str;
	const char *realm_canonical;
	const char *realm_canonical_ex;
	const char *user_principal_name;
	const char *service_principal_name;
	const char *canonical_name;
	const char *canonical_ex_name;

	ZERO_STRUCT(r);
	r.in.bind_handle		= &priv->bind_handle;
	r.in.level			= 1;
	r.in.req.req1.unknown1		= 0x000004e4;
	r.in.req.req1.unknown2		= 0x00000407;
	r.in.req.req1.count		= 1;
	r.in.req.req1.names		= names;
	r.in.req.req1.format_flags	= DRSUAPI_DS_NAME_FLAG_NO_FLAGS;

	r.in.req.req1.format_offered	= DRSUAPI_DS_NAME_FORMAT_CANONICAL;
	r.in.req.req1.format_desired	= DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT;
	names[0].str = talloc_asprintf(mem_ctx, "%s/", lp_realm());

	printf("testing DsCrackNames with name '%s' desired format:%d\n",
			names[0].str, r.in.req.req1.format_desired);

	status = dcerpc_drsuapi_DsCrackNames(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		const char *errstr = nt_errstr(status);
		if (NT_STATUS_EQUAL(status, NT_STATUS_NET_WRITE_FAULT)) {
			errstr = dcerpc_errstr(mem_ctx, p->last_fault_code);
		}
		printf("dcerpc_drsuapi_DsCrackNames failed - %s\n", errstr);
		ret = False;
	} else if (!W_ERROR_IS_OK(r.out.result)) {
		printf("DsCrackNames failed - %s\n", win_errstr(r.out.result));
		ret = False;
	} else if (r.out.ctr.ctr1->array[0].status != DRSUAPI_DS_NAME_STATUS_OK) {
		printf("DsCrackNames failed on name - %d\n", r.out.ctr.ctr1->array[0].status);
		ret = False;
	}

	if (!ret) {
		return ret;
	}

	dns_domain = r.out.ctr.ctr1->array[0].dns_domain_name;
	nt4_domain = r.out.ctr.ctr1->array[0].result_name;

	r.in.req.req1.format_desired	= DRSUAPI_DS_NAME_FORMAT_GUID;

	printf("testing DsCrackNames with name '%s' desired format:%d\n",
			names[0].str, r.in.req.req1.format_desired);

	status = dcerpc_drsuapi_DsCrackNames(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		const char *errstr = nt_errstr(status);
		if (NT_STATUS_EQUAL(status, NT_STATUS_NET_WRITE_FAULT)) {
			errstr = dcerpc_errstr(mem_ctx, p->last_fault_code);
		}
		printf("dcerpc_drsuapi_DsCrackNames failed - %s\n", errstr);
		ret = False;
	} else if (!W_ERROR_IS_OK(r.out.result)) {
		printf("DsCrackNames failed - %s\n", win_errstr(r.out.result));
		ret = False;
	} else if (r.out.ctr.ctr1->array[0].status != DRSUAPI_DS_NAME_STATUS_OK) {
		printf("DsCrackNames failed on name - %d\n", r.out.ctr.ctr1->array[0].status);
		ret = False;
	}

	if (!ret) {
		return ret;
	}

	priv->domain_dns_name = r.out.ctr.ctr1->array[0].dns_domain_name;
	priv->domain_guid_str = r.out.ctr.ctr1->array[0].result_name;
	GUID_from_string(priv->domain_guid_str, &priv->domain_guid);

	r.in.req.req1.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779;

	printf("testing DsCrackNames with name '%s' desired format:%d\n",
			names[0].str, r.in.req.req1.format_desired);

	status = dcerpc_drsuapi_DsCrackNames(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		const char *errstr = nt_errstr(status);
		if (NT_STATUS_EQUAL(status, NT_STATUS_NET_WRITE_FAULT)) {
			errstr = dcerpc_errstr(mem_ctx, p->last_fault_code);
		}
		printf("dcerpc_drsuapi_DsCrackNames failed - %s\n", errstr);
		ret = False;
	} else if (!W_ERROR_IS_OK(r.out.result)) {
		printf("DsCrackNames failed - %s\n", win_errstr(r.out.result));
		ret = False;
	} else if (r.out.ctr.ctr1->array[0].status != DRSUAPI_DS_NAME_STATUS_OK) {
		printf("DsCrackNames failed on name - %d\n", r.out.ctr.ctr1->array[0].status);
		ret = False;
	}

	if (!ret) {
		return ret;
	}
	
	realm_dn_str = r.out.ctr.ctr1->array[0].result_name;
	realm_dn =  ldb_dn_explode(mem_ctx, realm_dn_str);
	realm_canonical = ldb_dn_canonical_string(mem_ctx, realm_dn);

	if (strcmp(realm_canonical, 
		   talloc_asprintf(mem_ctx, "%s/", lp_realm()))!= 0) {
		printf("local Round trip on canonical name failed: %s != %s!\n",
		       realm_canonical, 
		       talloc_asprintf(mem_ctx, "%s/", lp_realm()));
		    return False;
	};

	realm_canonical_ex = ldb_dn_canonical_ex_string(mem_ctx, realm_dn);

	if (strcmp(realm_canonical_ex, 
		   talloc_asprintf(mem_ctx, "%s\n", lp_realm()))!= 0) {
		printf("local Round trip on canonical ex name failed: %s != %s!\n",
		       realm_canonical, 
		       talloc_asprintf(mem_ctx, "%s\n", lp_realm()));
		    return False;
	};

	r.in.req.req1.format_offered	= DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT;
	r.in.req.req1.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779;
	names[0].str = nt4_domain;

	printf("testing DsCrackNames with name '%s' desired format:%d\n",
			names[0].str, r.in.req.req1.format_desired);

	status = dcerpc_drsuapi_DsCrackNames(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		const char *errstr = nt_errstr(status);
		if (NT_STATUS_EQUAL(status, NT_STATUS_NET_WRITE_FAULT)) {
			errstr = dcerpc_errstr(mem_ctx, p->last_fault_code);
		}
		printf("dcerpc_drsuapi_DsCrackNames failed - %s\n", errstr);
		ret = False;
	} else if (!W_ERROR_IS_OK(r.out.result)) {
		printf("DsCrackNames failed - %s\n", win_errstr(r.out.result));
		ret = False;
	} else if (r.out.ctr.ctr1->array[0].status != DRSUAPI_DS_NAME_STATUS_OK) {
		printf("DsCrackNames failed on name - %d\n", r.out.ctr.ctr1->array[0].status);
		ret = False;
	}

	if (!ret) {
		return ret;
	}

	priv->domain_obj_dn = r.out.ctr.ctr1->array[0].result_name;

	r.in.req.req1.format_offered	= DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT;
	r.in.req.req1.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779;
	names[0].str = talloc_asprintf(mem_ctx, "%s%s$", nt4_domain, test_dc);

	printf("testing DsCrackNames with name '%s' desired format:%d\n",
			names[0].str, r.in.req.req1.format_desired);

	status = dcerpc_drsuapi_DsCrackNames(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		const char *errstr = nt_errstr(status);
		if (NT_STATUS_EQUAL(status, NT_STATUS_NET_WRITE_FAULT)) {
			errstr = dcerpc_errstr(mem_ctx, p->last_fault_code);
		}
		printf("dcerpc_drsuapi_DsCrackNames failed - %s\n", errstr);
		ret = False;
	} else if (!W_ERROR_IS_OK(r.out.result)) {
		printf("DsCrackNames failed - %s\n", win_errstr(r.out.result));
		ret = False;
	} else if (r.out.ctr.ctr1->array[0].status != DRSUAPI_DS_NAME_STATUS_OK) {
		printf("DsCrackNames failed on name - %d\n", r.out.ctr.ctr1->array[0].status);
		ret = False;
	}

	if (!ret) {
		return ret;
	}

	FQDN_1779_name = r.out.ctr.ctr1->array[0].result_name;

	FQDN_1779_dn = ldb_dn_explode(mem_ctx, FQDN_1779_name);

	canonical_name = ldb_dn_canonical_string(mem_ctx, FQDN_1779_dn);
	canonical_ex_name = ldb_dn_canonical_ex_string(mem_ctx, FQDN_1779_dn);

	user_principal_name = talloc_asprintf(mem_ctx, "%s$@%s", test_dc, dns_domain);
	service_principal_name = talloc_asprintf(mem_ctx, "HOST/%s", test_dc);
	{
		
		struct {
			enum drsuapi_DsNameFormat format_offered;
			enum drsuapi_DsNameFormat format_desired;
			const char *comment;
			const char *str;
			const char *expected_str;
			enum drsuapi_DsNameStatus status;
			enum drsuapi_DsNameFlags flags;
		} crack[] = {
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_USER_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = user_principal_name,
				.expected_str = FQDN_1779_name,
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = service_principal_name,
				.expected_str = FQDN_1779_name,
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = talloc_asprintf(mem_ctx, "cifs/%s.%s", test_dc, dns_domain),
				.comment = "ServicePrincipal Name",
				.expected_str = FQDN_1779_name,
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_CANONICAL,
				.str = FQDN_1779_name,
				.expected_str = canonical_name,
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_CANONICAL_EX,
				.str = FQDN_1779_name,
				.expected_str = canonical_ex_name,
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_CANONICAL,
				.str = FQDN_1779_name,
				.comment = "DN to cannoical syntactial only",
				.status = DRSUAPI_DS_NAME_STATUS_OK,
				.expected_str = canonical_name,
				.flags = DRSUAPI_DS_NAME_FLAG_SYNTACTICAL_ONLY
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_CANONICAL_EX,
				.str = FQDN_1779_name,
				.comment = "DN to cannoical EX syntactial only",
				.status = DRSUAPI_DS_NAME_STATUS_OK,
				.expected_str = canonical_ex_name,
				.flags = DRSUAPI_DS_NAME_FLAG_SYNTACTICAL_ONLY
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_DISPLAY,
				.str = FQDN_1779_name,
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_GUID,
				.str = FQDN_1779_name,
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_GUID,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT,
				.str = priv->domain_guid_str,
				.comment = "Domain GUID to NT4 ACCOUNT",
				.expected_str = nt4_domain,
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_GUID,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_CANONICAL,
				.str = priv->domain_guid_str,
				.comment = "Domain GUID to Canonical",
				.expected_str = talloc_asprintf(mem_ctx, "%s/", dns_domain),
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_GUID,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_CANONICAL_EX,
				.str = priv->domain_guid_str,
				.comment = "Domain GUID to Canonical EX",
				.expected_str = talloc_asprintf(mem_ctx, "%s\n", dns_domain),
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_DISPLAY,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = "CN=Microsoft Corporation,L=Redmond,S=Washington,C=US",
				.comment = "display name for Microsoft Support Account",
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{		
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_GUID,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = GUID_string2(mem_ctx, &priv->dcinfo.site_guid),
				.comment = "Site GUID",
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT,
				.str = GUID_string2(mem_ctx, &priv->dcinfo.computer_guid),
				.comment = "Computer GUID",
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_GUID,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = GUID_string2(mem_ctx, &priv->dcinfo.server_guid),
				.comment = "Server GUID",
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_GUID,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = GUID_string2(mem_ctx, &priv->dcinfo.ntds_guid),
				.comment = "NTDS GUID",
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SID_OR_SID_HISTORY,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = SID_BUILTIN,
				.comment = "BUILTIN domain SID",
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_DISPLAY,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = test_dc,
				.comment = "DISPAY NAME search for DC short name",
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = talloc_asprintf(mem_ctx, "krbtgt/%s", dns_domain),
				.comment = "Looking for KRBTGT as a serivce principal",
				.status = DRSUAPI_DS_NAME_STATUS_DOMAIN_ONLY
			},
			{ 
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = talloc_asprintf(mem_ctx, "krbtgt"),
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			},
			{ 
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = talloc_asprintf(mem_ctx, "kadmin/changepw"),
				.status = DRSUAPI_DS_NAME_STATUS_OK,
				.expected_str = talloc_asprintf(mem_ctx, "CN=krbtgt,CN=Users,%s", realm_dn_str)
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = talloc_asprintf(mem_ctx, "cifs/%s.%s@%s", 
						       test_dc, dns_domain,
						       dns_domain),
				.status = DRSUAPI_DS_NAME_STATUS_DOMAIN_ONLY
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_GUID,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = "NOT A GUID",
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SID_OR_SID_HISTORY,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = "NOT A SID",
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = "NOT AN NT4 NAME",
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_GUID,
				.comment = "Unparsable DN",
				.str = "NOT A DN",
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_USER_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.comment = "Unparsable user principal",
				.str = "NOT A PRINCIPAL",
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.comment = "Unparsable service principal",
				.str = "NOT A SERVICE PRINCIPAL",
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_GUID,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.comment = "BIND GUID (ie, not in the directory)",
				.str = GUID_string2(mem_ctx, &priv->bind_guid),
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_USER_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.comment = "Unqualified Machine account as user principal",
				.str = talloc_asprintf(mem_ctx, "%s$", test_dc),
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.comment = "Machine account as service principal",
				.str = talloc_asprintf(mem_ctx, "%s$", test_dc),
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.comment = "Realm as an NT4 domain lookup",
				.str = talloc_asprintf(mem_ctx, "%s\\", lp_realm()),
				.status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND
			}, 
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SID_OR_SID_HISTORY,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT,
				.comment = "BUITIN SID -> NT4 account",
				.str = SID_BUILTIN,
				.status = DRSUAPI_DS_NAME_STATUS_NO_MAPPING
			}, 
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SID_OR_SID_HISTORY,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = SID_BUILTIN_ADMINISTRATORS,
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_SID_OR_SID_HISTORY,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT,
				.str = SID_BUILTIN_ADMINISTRATORS,
				.status = DRSUAPI_DS_NAME_STATUS_OK
			},
			{
				.format_offered	= DRSUAPI_DS_NAME_FORMAT_USER_PRINCIPAL,
				.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779,
				.str = "foo@bar",
				.status = DRSUAPI_DS_NAME_STATUS_DOMAIN_ONLY
			},
		};
		int i;
		
		for (i=0; i < ARRAY_SIZE(crack); i++) {
			r.in.req.req1.format_flags   = crack[i].flags;
			r.in.req.req1.format_offered = crack[i].format_offered; 
			r.in.req.req1.format_desired = crack[i].format_desired;
			names[0].str = crack[i].str;
			
			if (crack[i].comment) {
				printf("testing DsCrackNames '%s' with name '%s' desired format:%d\n",
				       crack[i].comment, names[0].str, r.in.req.req1.format_desired);
			} else {
				printf("testing DsCrackNames with name '%s' desired format:%d\n",
				       names[0].str, r.in.req.req1.format_desired);
			}
			status = dcerpc_drsuapi_DsCrackNames(p, mem_ctx, &r);
			if (!NT_STATUS_IS_OK(status)) {
				const char *errstr = nt_errstr(status);
				if (NT_STATUS_EQUAL(status, NT_STATUS_NET_WRITE_FAULT)) {
					errstr = dcerpc_errstr(mem_ctx, p->last_fault_code);
				}
				printf("dcerpc_drsuapi_DsCrackNames failed - %s\n", errstr);
				ret = False;
			} else if (!W_ERROR_IS_OK(r.out.result)) {
				printf("DsCrackNames failed - %s\n", win_errstr(r.out.result));
				ret = False;
			} else if (r.out.ctr.ctr1->array[0].status != crack[i].status) {
				printf("DsCrackNames unexpected status %d, wanted %d on name: %s\n", 
				       r.out.ctr.ctr1->array[0].status,
				       crack[i].status,
				       crack[i].str);
				ret = False;
			} else if (crack[i].expected_str
				   && (strcmp(r.out.ctr.ctr1->array[0].result_name, 
					      crack[i].expected_str) != 0)) {
				printf("DsCrackNames failed - got %s, expected %s\n", 
				       r.out.ctr.ctr1->array[0].result_name, 
				       crack[i].expected_str);
				ret = False;
			}
		}
	}

	if (!test_DsCrackNamesMatrix(p, mem_ctx, priv, FQDN_1779_name, 
				     user_principal_name, service_principal_name)) {
		ret = False;
	}

	return ret;
}

BOOL torture_rpc_drsuapi_cracknames(void)
{
        NTSTATUS status;
        struct dcerpc_pipe *p;
	TALLOC_CTX *mem_ctx;
	BOOL ret = True;
	struct DsPrivate priv;

	mem_ctx = talloc_init("torture_rpc_drsuapi");

	status = torture_rpc_connection(mem_ctx, 
					&p, 
					DCERPC_DRSUAPI_NAME,
					DCERPC_DRSUAPI_UUID,
					DCERPC_DRSUAPI_VERSION);
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(mem_ctx);
		return False;
	}

	printf("Connected to DRAUAPI pipe\n");

	ZERO_STRUCT(priv);

	ret &= test_DsBind(p, mem_ctx, &priv);

	ret &= test_DsCrackNames(p, mem_ctx, &priv, lp_parm_string(-1, "torture", "host"));

	ret &= test_DsUnbind(p, mem_ctx, &priv);

	talloc_free(mem_ctx);

	return ret;
}
