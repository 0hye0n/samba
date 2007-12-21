/*
 *  Unix SMB/CIFS implementation.
 *  libnet Join Support
 *  Copyright (C) Gerald (Jerry) Carter 2006
 *  Copyright (C) Guenther Deschner 2007
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "includes.h"
#include "libnet/libnet_join.h"
#include "libnet/libnet_proto.h"

static NTSTATUS do_DomainJoin(TALLOC_CTX *mem_ctx,
			      struct libnet_JoinCtx *r)
{
	struct cli_state *cli = NULL;
	struct rpc_pipe_client *pipe_hnd = NULL;
	const char *password = NULL;
	POLICY_HND sam_pol, domain_pol, user_pol, lsa_pol;
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;
	char *acct_name;
	const char *const_acct_name;
	uint32 user_rid;
	uint32 num_rids, *name_types, *user_rids;
	uint32 flags = 0x3e8;
	uint32 acb_info = ACB_WSTRUST;
	uint32 fields_present;
	uchar pwbuf[532];
	SAM_USERINFO_CTR ctr;
	SAM_USER_INFO_25 p25;
	const int infolevel = 25;
	struct MD5Context md5ctx;
	uchar md5buffer[16];
	DATA_BLOB digested_session_key;
	uchar md4_trust_password[16];

	password = talloc_strdup(mem_ctx,
		generate_random_str(DEFAULT_TRUST_ACCOUNT_PASSWORD_LENGTH));
	NT_STATUS_HAVE_NO_MEMORY(password);

	status = cli_full_connection(&cli, NULL, r->in.server_name,
				     NULL, 0,
				     "IPC$", "IPC",
				     r->in.admin_account,
				     NULL, //r->in.domain_name,
				     r->in.password,
				     0, Undefined, NULL);

	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	pipe_hnd = cli_rpc_pipe_open_noauth(cli, PI_LSARPC, &status);
	if (!pipe_hnd) {
		goto done;
	}

	status = rpccli_lsa_open_policy(pipe_hnd, mem_ctx, True,
					SEC_RIGHTS_MAXIMUM_ALLOWED, &lsa_pol);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	status = rpccli_lsa_query_info_policy2(pipe_hnd, mem_ctx, &lsa_pol,
					       12,
					       &r->out.netbios_domain_name,
					       &r->out.dns_domain_name,
					       NULL,
					       NULL,
					       &r->out.domain_sid);

	if (!NT_STATUS_IS_OK(status)) {
		status = rpccli_lsa_query_info_policy(pipe_hnd, mem_ctx, &lsa_pol,
						      5,
						      &r->out.netbios_domain_name,
						      &r->out.domain_sid);
		if (!NT_STATUS_IS_OK(status)) {
			goto done;
		}
	}

	rpccli_lsa_Close(pipe_hnd, mem_ctx, &lsa_pol);
	cli_rpc_pipe_close(pipe_hnd);

	pipe_hnd = cli_rpc_pipe_open_noauth(cli, PI_SAMR, &status);
	if (!pipe_hnd) {
		goto done;
	}

	status = rpccli_samr_connect(pipe_hnd, mem_ctx,
				     SEC_RIGHTS_MAXIMUM_ALLOWED, &sam_pol);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	status = rpccli_samr_open_domain(pipe_hnd, mem_ctx, &sam_pol,
					 SEC_RIGHTS_MAXIMUM_ALLOWED,
					 r->out.domain_sid,
					 &domain_pol);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	acct_name = talloc_asprintf(mem_ctx, "%s$", global_myname());
	strlower_m(acct_name);
	const_acct_name = acct_name;

	status = rpccli_samr_create_dom_user(pipe_hnd, mem_ctx, &domain_pol,
					     acct_name, ACB_WSTRUST,
					     0xe005000b, &user_pol, &user_rid);
	if (NT_STATUS_EQUAL(status, NT_STATUS_USER_EXISTS)) {
		if (!(r->in.join_flags & WKSSVC_JOIN_FLAGS_DOMAIN_JOIN_IF_JOINED)) {
			goto done;
		}
	}

	if (NT_STATUS_IS_OK(status)) {
		rpccli_samr_close(pipe_hnd, mem_ctx, &user_pol);
	}

	status = rpccli_samr_lookup_names(pipe_hnd, mem_ctx,
					  &domain_pol, flags, 1,
					  &const_acct_name,
					  &num_rids, &user_rids, &name_types);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	if (name_types[0] != SID_NAME_USER) {
		status = NT_STATUS_INVALID_WORKSTATION;
		goto done;
	}

	user_rid = user_rids[0];

	status = rpccli_samr_open_user(pipe_hnd, mem_ctx, &domain_pol,
				       SEC_RIGHTS_MAXIMUM_ALLOWED, user_rid,
				       &user_pol);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	E_md4hash(r->in.password, md4_trust_password);
	encode_pw_buffer(pwbuf, r->in.password, STR_UNICODE);

	generate_random_buffer((uint8*)md5buffer, sizeof(md5buffer));
	digested_session_key = data_blob_talloc(mem_ctx, 0, 16);

	MD5Init(&md5ctx);
	MD5Update(&md5ctx, md5buffer, sizeof(md5buffer));
	MD5Update(&md5ctx, cli->user_session_key.data, cli->user_session_key.length);
	MD5Final(digested_session_key.data, &md5ctx);

	SamOEMhashBlob(pwbuf, sizeof(pwbuf), &digested_session_key);
	memcpy(&pwbuf[516], md5buffer, sizeof(md5buffer));

	acb_info |= ACB_PWNOEXP;
#if 0
	if ( dom_type == ND_TYPE_AD ) {
#if !defined(ENCTYPE_ARCFOUR_HMAC)
		acb_info |= ACB_USE_DES_KEY_ONLY;
#endif
		;;
	}
#endif
	ZERO_STRUCT(ctr);
	ZERO_STRUCT(p25);

	fields_present = ACCT_NT_PWD_SET | ACCT_LM_PWD_SET | ACCT_FLAGS;
	init_sam_user_info25P(&p25, fields_present, acb_info, (char *)pwbuf);

	ctr.switch_value = infolevel;
	ctr.info.id25    = &p25;

	status = rpccli_samr_set_userinfo2(pipe_hnd, mem_ctx, &user_pol,
					   infolevel, &cli->user_session_key,
					   &ctr);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	rpccli_samr_close(pipe_hnd, mem_ctx, &user_pol);
	cli_rpc_pipe_close(pipe_hnd);

	if (!secrets_store_domain_sid(r->out.netbios_domain_name,
				      r->out.domain_sid))
	{
		status = NT_STATUS_INTERNAL_DB_ERROR;
		goto done;
	}

	if (!secrets_store_machine_password(password,
					    r->out.netbios_domain_name,
					    SEC_CHAN_WKSTA))
	{
		status = NT_STATUS_INTERNAL_DB_ERROR;
		goto done;
	}

	status = NT_STATUS_OK;
 done:
	if (cli) {
		cli_shutdown(cli);
	}

	return status;
}

static NTSTATUS do_DomainUnjoin(TALLOC_CTX *mem_ctx,
				struct libnet_UnjoinCtx *r)
{
	struct cli_state *cli = NULL;
	struct rpc_pipe_client *pipe_hnd = NULL;
	POLICY_HND sam_pol, domain_pol, user_pol;
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;
	char *acct_name;
	uint32 flags = 0x3e8;
	const char *const_acct_name;
	uint32 user_rid;
	uint32 num_rids, *name_types, *user_rids;
	SAM_USERINFO_CTR ctr, *qctr = NULL;
	SAM_USER_INFO_16 p16;

	status = cli_full_connection(&cli, NULL, r->in.server_name,
				     NULL, 0,
				     "IPC$", "IPC",
				     r->in.admin_account,
				     NULL, //r->in.domain_name,
				     r->in.password,
				     0, Undefined, NULL);

	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	pipe_hnd = cli_rpc_pipe_open_noauth(cli, PI_SAMR, &status);
	if (!pipe_hnd) {
		goto done;
	}

	status = rpccli_samr_connect(pipe_hnd, mem_ctx,
				     SEC_RIGHTS_MAXIMUM_ALLOWED, &sam_pol);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	status = rpccli_samr_open_domain(pipe_hnd, mem_ctx, &sam_pol,
					 SEC_RIGHTS_MAXIMUM_ALLOWED,
					 r->in.domain_sid,
					 &domain_pol);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	acct_name = talloc_asprintf(mem_ctx, "%s$", global_myname());
	strlower_m(acct_name);
	const_acct_name = acct_name;

	status = rpccli_samr_lookup_names(pipe_hnd, mem_ctx,
					  &domain_pol, flags, 1,
					  &const_acct_name,
					  &num_rids, &user_rids, &name_types);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	if (name_types[0] != SID_NAME_USER) {
		status = NT_STATUS_INVALID_WORKSTATION;
		goto done;
	}

	user_rid = user_rids[0];

	status = rpccli_samr_open_user(pipe_hnd, mem_ctx, &domain_pol,
				       SEC_RIGHTS_MAXIMUM_ALLOWED,
				       user_rid, &user_pol);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	status = rpccli_samr_query_userinfo(pipe_hnd, mem_ctx,
					    &user_pol, 16, &qctr);
	if (!NT_STATUS_IS_OK(status)) {
		rpccli_samr_close(pipe_hnd, mem_ctx, &user_pol);
		goto done;
	}

	ZERO_STRUCT(ctr);
	ctr.switch_value = 16;
	ctr.info.id16 = &p16;

	p16.acb_info = qctr->info.id16->acb_info | ACB_DISABLED;

	status = rpccli_samr_set_userinfo2(pipe_hnd, mem_ctx, &user_pol, 16,
					   &cli->user_session_key, &ctr);

	rpccli_samr_close(pipe_hnd, mem_ctx, &user_pol);

	if (!secrets_delete_machine_password_ex(lp_workgroup())) {
		status = NT_STATUS_INTERNAL_DB_ERROR;
		goto done;
	}

	if (!secrets_delete_domain_sid(lp_workgroup())) {
		status = NT_STATUS_INTERNAL_DB_ERROR;
		goto done;
	}

done:
	rpccli_samr_close(pipe_hnd, mem_ctx, &domain_pol);
	rpccli_samr_close(pipe_hnd, mem_ctx, &sam_pol);

	cli_rpc_pipe_close(pipe_hnd);

	if (cli) {
		cli_shutdown(cli);
	}

	return status;
}

static WERROR do_modify_val_config(struct registry_key *key,
				   const char *val_name,
				   const char *val_data)
{
	struct registry_value val;

	ZERO_STRUCT(val);

	val.type = REG_SZ;
	val.v.sz.str = CONST_DISCARD(char *, val_data);
	val.v.sz.len = strlen(val_data) + 1;

	return reg_setvalue(key, val_name, &val);
}

static WERROR do_join_modify_vals_config(TALLOC_CTX *mem_ctx,
					 struct libnet_JoinCtx *r,
					 struct registry_key *key)
{
	WERROR werr;
	bool is_ad = false;

	if (!(r->in.join_flags & WKSSVC_JOIN_FLAGS_JOIN_TYPE)) {

		werr = do_modify_val_config(key, "security", "user");
		W_ERROR_NOT_OK_RETURN(werr);

		werr = do_modify_val_config(key, "workgroup",
					    r->in.domain_name);
		return werr;
	}

	if (r->out.dns_domain_name) {
		is_ad = true;
	}

	werr = do_modify_val_config(key, "security", "domain");
	W_ERROR_NOT_OK_RETURN(werr);

	werr = do_modify_val_config(key, "workgroup",
				    r->out.netbios_domain_name);
	W_ERROR_NOT_OK_RETURN(werr);

	if (is_ad) {
		werr = do_modify_val_config(key, "security", "ads");
		W_ERROR_NOT_OK_RETURN(werr);

		werr = do_modify_val_config(key, "realm",
					    r->out.dns_domain_name);
		W_ERROR_NOT_OK_RETURN(werr);
	}

	return werr;
}

static WERROR do_unjoin_modify_vals_config(TALLOC_CTX *mem_ctx,
					   struct libnet_UnjoinCtx *r,
					   struct registry_key *key)
{
	WERROR werr;

	if (r->in.unjoin_flags & WKSSVC_JOIN_FLAGS_JOIN_TYPE) {

		werr = do_modify_val_config(key, "security", "user");
		W_ERROR_NOT_OK_RETURN(werr);
	}

	reg_deletevalue(key, "realm");

	return werr;
}


static WERROR do_JoinConfig(TALLOC_CTX *mem_ctx,
			    struct libnet_JoinCtx *r)
{
	WERROR werr;
	struct registry_key *key = NULL;

	if (!W_ERROR_IS_OK(r->out.result)) {
		return r->out.result;
	}

	if (!r->in.modify_config) {
		return WERR_OK;
	}

	if (!registry_init_regdb()) {
		return WERR_REG_IO_FAILURE;
	}

	if (!libnet_smbconf_key_exists(mem_ctx, GLOBAL_NAME)) {
		werr = libnet_reg_createkey_internal(mem_ctx,
						     GLOBAL_NAME, &key);
	} else {
		werr = libnet_smbconf_open_path(mem_ctx,
						GLOBAL_NAME,
						REG_KEY_WRITE, &key);
	}
	if (!W_ERROR_IS_OK(werr)) {
		return werr;
	}

	werr = do_join_modify_vals_config(mem_ctx, r, key);
	if (!W_ERROR_IS_OK(werr)) {
		return werr;
	}

	r->out.modified_config = true;
	r->out.result = werr;

	return werr;
}

static WERROR do_UnjoinConfig(TALLOC_CTX *mem_ctx,
			      struct libnet_UnjoinCtx *r)
{
	WERROR werr;
	struct registry_key *key = NULL;

	if (!W_ERROR_IS_OK(r->out.result)) {
		return r->out.result;
	}

	if (!r->in.modify_config) {
		return WERR_OK;
	}

	if (!registry_init_regdb()) {
		return WERR_REG_IO_FAILURE;
	}

	if (!libnet_smbconf_key_exists(mem_ctx, GLOBAL_NAME)) {
		werr = libnet_reg_createkey_internal(mem_ctx,
						     GLOBAL_NAME, &key);
	} else {
		werr = libnet_smbconf_open_path(mem_ctx,
						GLOBAL_NAME,
						REG_KEY_WRITE, &key);
	}
	if (!W_ERROR_IS_OK(werr)) {
		return werr;
	}

	werr = do_unjoin_modify_vals_config(mem_ctx, r, key);
	if (!W_ERROR_IS_OK(werr)) {
		return werr;
	}

	r->out.modified_config = true;
	r->out.result = werr;

	return werr;
}

WERROR libnet_init_JoinCtx(TALLOC_CTX *mem_ctx,
			   struct libnet_JoinCtx **r)
{
	struct libnet_JoinCtx *ctx;

	ctx = talloc_zero(mem_ctx, struct libnet_JoinCtx);
	if (!ctx) {
		return WERR_NOMEM;
	}

	*r = ctx;

	return WERR_OK;
}

WERROR libnet_init_UnjoinCtx(TALLOC_CTX *mem_ctx,
			     struct libnet_UnjoinCtx **r)
{
	struct libnet_UnjoinCtx *ctx;

	ctx = talloc_zero(mem_ctx, struct libnet_UnjoinCtx);
	if (!ctx) {
		return WERR_NOMEM;
	}

	*r = ctx;

	return WERR_OK;
}

WERROR libnet_Join(TALLOC_CTX *mem_ctx,
		   struct libnet_JoinCtx *r)
{
	WERROR werr;
	NTSTATUS status;

	if (!r->in.domain_name) {
		return WERR_INVALID_PARAM;
	}

	if (r->in.modify_config && !lp_include_registry_globals()) {
		return WERR_NOT_SUPPORTED;
	}

	if (r->in.join_flags & WKSSVC_JOIN_FLAGS_JOIN_TYPE) {

		status = do_DomainJoin(mem_ctx, r);
		if (!NT_STATUS_IS_OK(status)) {
			if (NT_STATUS_EQUAL(status, NT_STATUS_USER_EXISTS)) {
				return WERR_SETUP_ALREADY_JOINED;
			}
			return ntstatus_to_werror(status);
		}
	}

	werr = do_JoinConfig(mem_ctx, r);
	if (!W_ERROR_IS_OK(werr)) {
		return werr;
	}

	return werr;
}

WERROR libnet_Unjoin(TALLOC_CTX *mem_ctx,
		     struct libnet_UnjoinCtx *r)
{
	WERROR werr;
	NTSTATUS status;

	if (r->in.modify_config && !lp_include_registry_globals()) {
		return WERR_NOT_SUPPORTED;
	}

	if (r->in.unjoin_flags & WKSSVC_JOIN_FLAGS_JOIN_TYPE) {

		status = do_DomainUnjoin(mem_ctx, r);
		if (!NT_STATUS_IS_OK(status)) {
			if (NT_STATUS_EQUAL(status, NT_STATUS_NO_SUCH_USER)) {
				return WERR_SETUP_NOT_JOINED;
			}
			return ntstatus_to_werror(status);
		}
	}

	werr = do_UnjoinConfig(mem_ctx, r);
	if (!W_ERROR_IS_OK(werr)) {
		return werr;
	}

	return werr;
}
