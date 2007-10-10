/* 
   Unix SMB/CIFS implementation.

   PAC Glue between Samba and the KDC
   
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
#include "kdc/kdc.h"
#include "kdc/pac-glue.h" /* Ensure we don't get this prototype wrong, as that could be painful */

 krb5_error_code samba_get_pac(krb5_context context, 
			       struct krb5_kdc_configuration *config,
			       krb5_principal client, 
			       krb5_keyblock *krbtgt_keyblock, 
			       krb5_keyblock *server_keyblock, 
			       time_t tgs_authtime,
			       krb5_data *pac)
{
	krb5_error_code ret;
	NTSTATUS nt_status;
	struct auth_serversupplied_info *server_info;
	DATA_BLOB tmp_blob;
	char *principal_string;
	TALLOC_CTX *mem_ctx = talloc_named(config, 0, "samba_get_pac context");
	if (!mem_ctx) {
		return ENOMEM;
	}

	ret = krb5_unparse_name(context, client, &principal_string);

	if (ret != 0) {
		krb5_set_error_string(context, "get pac: could not unparse principal");
		krb5_warnx(context, "get pac: could not unparse principal");
		talloc_free(mem_ctx);
		return ret;
	}

	nt_status = sam_get_server_info_principal(mem_ctx, principal_string,
						  &server_info);
	free(principal_string);
	if (!NT_STATUS_IS_OK(nt_status)) {
		DEBUG(0, ("Getting user info for PAC failed: %s\n",
			  nt_errstr(nt_status)));
		return EINVAL;
	}

	ret = kerberos_create_pac(mem_ctx, server_info, 
				  context, 
				  krbtgt_keyblock,
				  server_keyblock,
				  client,
				  tgs_authtime,
				  &tmp_blob);

	if (ret) {
		DEBUG(1, ("PAC encoding failed: %s\n", 
			  smb_get_krb5_error_message(context, ret, mem_ctx)));
		talloc_free(mem_ctx);
		return ret;
	}

	ret = krb5_data_copy(pac, tmp_blob.data, tmp_blob.length);
	talloc_free(mem_ctx);
	return ret;
}
