/*
 *  Unix SMB/CIFS implementation.
 *  libnet smbconf registry Support
 *  Copyright (C) Michael Adam 2007
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

/**********************************************************************
 *
 * Helper functions (mostly registry related)
 * TODO: These should be eventually static.

 **********************************************************************/

/*
 * Open a subkey of KEY_SMBCONF (i.e a service)
 */
WERROR libnet_smbconf_reg_open_path(TALLOC_CTX *ctx,
				    const char *subkeyname,
				    uint32 desired_access,
				    struct registry_key **key)
{
	WERROR werr = WERR_OK;
	char *path = NULL;
	NT_USER_TOKEN *token;

	if (!(token = registry_create_admin_token(ctx))) {
		DEBUG(1, ("Error creating admin token\n"));
		goto done;
	}

	if (subkeyname == NULL) {
		path = talloc_strdup(ctx, KEY_SMBCONF);
	} else {
		path = talloc_asprintf(ctx, "%s\\%s", KEY_SMBCONF, subkeyname);
	}

	werr = reg_open_path(ctx, path, desired_access,
			     token, key);

	if (!W_ERROR_IS_OK(werr)) {
		DEBUG(1, ("Error opening registry path '%s': %s\n",
			  path, dos_errstr(werr)));
	}

done:
	TALLOC_FREE(path);
	return werr;
}

/*
 * check if a subkey of KEY_SMBCONF of a given name exists
 */
bool libnet_smbconf_key_exists(const char *subkeyname)
{
	bool ret = false;
	WERROR werr = WERR_OK;
	TALLOC_CTX *mem_ctx = talloc_stackframe();
	struct registry_key *key = NULL;

	werr = libnet_smbconf_reg_open_path(mem_ctx, subkeyname, REG_KEY_READ,
					    &key);
	if (W_ERROR_IS_OK(werr)) {
		ret = true;
	}

	TALLOC_FREE(mem_ctx);
	return ret;
}

static bool libnet_smbconf_value_exists(struct registry_key *key,
					const char *param)
{
	bool ret = false;
	WERROR werr = WERR_OK;
	TALLOC_CTX *ctx = talloc_stackframe();
	struct registry_value *value = NULL;

	werr = reg_queryvalue(ctx, key, param, &value);
	if (W_ERROR_IS_OK(werr)) {
		ret = true;
	}

	TALLOC_FREE(ctx);
	return ret;
}

/*
 * open the base key KEY_SMBCONF
 */
WERROR libnet_smbconf_open_basepath(TALLOC_CTX *ctx, uint32 desired_access,
			     	    struct registry_key **key)
{
	return libnet_smbconf_reg_open_path(ctx, NULL, desired_access, key);
}

/*
 * create a subkey of KEY_SMBCONF
 */
WERROR libnet_smbconf_reg_createkey_internal(TALLOC_CTX *ctx,
					     const char * subkeyname,
					     struct registry_key **newkey)
{
	WERROR werr = WERR_OK;
	struct registry_key *create_parent = NULL;
	TALLOC_CTX *create_ctx;
	enum winreg_CreateAction action = REG_ACTION_NONE;

	/* create a new talloc ctx for creation. it will hold
	 * the intermediate parent key (SMBCONF) for creation
	 * and will be destroyed when leaving this function... */
	if (!(create_ctx = talloc_new(ctx))) {
		werr = WERR_NOMEM;
		goto done;
	}

	werr = libnet_smbconf_open_basepath(create_ctx, REG_KEY_WRITE, &create_parent);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = reg_createkey(ctx, create_parent, subkeyname,
			     REG_KEY_WRITE, newkey, &action);
	if (W_ERROR_IS_OK(werr) && (action != REG_CREATED_NEW_KEY)) {
		d_fprintf(stderr, "Key '%s' already exists.\n", subkeyname);
		werr = WERR_ALREADY_EXISTS;
	}
	if (!W_ERROR_IS_OK(werr)) {
		d_fprintf(stderr, "Error creating key %s: %s\n",
			 subkeyname, dos_errstr(werr));
	}

done:
	TALLOC_FREE(create_ctx);
	return werr;
}

/*
 * add a value to a key.
 */
WERROR libnet_smbconf_reg_setvalue_internal(struct registry_key *key,
						   const char *valname,
						   const char *valstr)
{
	struct registry_value val;
	WERROR werr = WERR_OK;
	char *subkeyname;
	const char *canon_valname;
	const char *canon_valstr;

	if (!lp_canonicalize_parameter_with_value(valname, valstr,
						  &canon_valname,
						  &canon_valstr))
	{
		if (canon_valname == NULL) {
			d_fprintf(stderr, "invalid parameter '%s' given\n",
				  valname);
		} else {
			d_fprintf(stderr, "invalid value '%s' given for "
				  "parameter '%s'\n", valstr, valname);
		}
		werr = WERR_INVALID_PARAM;
		goto done;
	}

	ZERO_STRUCT(val);

	val.type = REG_SZ;
	val.v.sz.str = CONST_DISCARD(char *, canon_valstr);
	val.v.sz.len = strlen(canon_valstr) + 1;

	if (registry_smbconf_valname_forbidden(canon_valname)) {
		d_fprintf(stderr, "Parameter '%s' not allowed in registry.\n",
			  canon_valname);
		werr = WERR_INVALID_PARAM;
		goto done;
	}

	subkeyname = strrchr_m(key->key->name, '\\');
	if ((subkeyname == NULL) || (*(subkeyname +1) == '\0')) {
		d_fprintf(stderr, "Invalid registry key '%s' given as "
			  "smbconf section.\n", key->key->name);
		werr = WERR_INVALID_PARAM;
		goto done;
	}
	subkeyname++;
	if (!strequal(subkeyname, GLOBAL_NAME) &&
	    lp_parameter_is_global(valname))
	{
		d_fprintf(stderr, "Global paramter '%s' not allowed in "
			  "service definition ('%s').\n", canon_valname,
			  subkeyname);
		werr = WERR_INVALID_PARAM;
		goto done;
	}

	werr = reg_setvalue(key, canon_valname, &val);
	if (!W_ERROR_IS_OK(werr)) {
		d_fprintf(stderr,
			  "Error adding value '%s' to "
			  "key '%s': %s\n",
			  canon_valname, key->key->name, dos_errstr(werr));
	}

done:
	return werr;
}

/**********************************************************************
 *
 * The actual net conf api functions, that are exported.
 *
 **********************************************************************/

/**
 * Drop the whole configuration (restarting empty).
 */
WERROR libnet_smbconf_drop(void)
{
	char *path, *p;
	WERROR werr = WERR_OK;
	NT_USER_TOKEN *token;
	struct registry_key *parent_key = NULL;
	struct registry_key *new_key = NULL;
	TALLOC_CTX* mem_ctx = talloc_stackframe();
	enum winreg_CreateAction action;

	if (!(token = registry_create_admin_token(mem_ctx))) {
		/* what is the appropriate error code here? */
		werr = WERR_CAN_NOT_COMPLETE;
		goto done;
	}

	path = talloc_strdup(mem_ctx, KEY_SMBCONF);
	if (path == NULL) {
		werr = WERR_NOMEM;
		goto done;
	}
	p = strrchr(path, '\\');
	*p = '\0';
	werr = reg_open_path(mem_ctx, path, REG_KEY_WRITE, token, &parent_key);

	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = reg_deletekey_recursive(mem_ctx, parent_key, p+1);

	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = reg_createkey(mem_ctx, parent_key, p+1, REG_KEY_WRITE,
			     &new_key, &action);

done:
	TALLOC_FREE(mem_ctx);
	return werr;
}

/**
 * delete a service from configuration
 */
WERROR libnet_smbconf_delshare(const char *servicename)
{
	WERROR werr = WERR_OK;
	struct registry_key *key = NULL;
	TALLOC_CTX *ctx = talloc_stackframe();

	werr = libnet_smbconf_open_basepath(ctx, REG_KEY_WRITE, &key);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = reg_deletekey_recursive(key, key, servicename);

done:
	TALLOC_FREE(ctx);
	return werr;
}

WERROR libnet_smbconf_setparm(const char *service,
			      const char *param,
			      const char *valstr)
{
	WERROR werr;
	struct registry_key *key = NULL;
	TALLOC_CTX *mem_ctx = talloc_stackframe();

	if (!libnet_smbconf_key_exists(service)) {
		werr = libnet_smbconf_reg_createkey_internal(mem_ctx, service,
							     &key);
	} else {
		werr = libnet_smbconf_reg_open_path(mem_ctx, service,
						    REG_KEY_WRITE, &key);
	}
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnet_smbconf_reg_setvalue_internal(key, param, valstr);

done:
	TALLOC_FREE(mem_ctx);
	return werr;
}

WERROR libnet_smbconf_getparm(TALLOC_CTX *mem_ctx,
			      const char *service,
			      const char *param,
			      struct registry_value **value)
{
	WERROR werr;
	struct registry_key *key = NULL;

	if (!libnet_smbconf_key_exists(service)) {
		werr = WERR_NO_SUCH_SERVICE;
		goto done;
	}

	werr = libnet_smbconf_reg_open_path(mem_ctx, service, REG_KEY_READ,
					    &key);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	if (!libnet_smbconf_value_exists(key, param)) {
		werr = WERR_INVALID_PARAM;
		goto done;
	}

	werr = reg_queryvalue(mem_ctx, key, param, value);

done:
	TALLOC_FREE(key);
	return werr;
}

WERROR libnet_smbconf_delparm(const char *service,
			      const char *param)
{
	struct registry_key *key = NULL;
	WERROR werr = WERR_OK;
	TALLOC_CTX *mem_ctx = talloc_stackframe();

	if (!libnet_smbconf_key_exists(service)) {
		return WERR_NO_SUCH_SERVICE;
	}

	werr = libnet_smbconf_reg_open_path(mem_ctx, service, REG_KEY_ALL, &key);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	if (!libnet_smbconf_value_exists(key, param)) {
		werr = WERR_INVALID_PARAM;
		goto done;
	}

	werr = reg_deletevalue(key, param);

done:
	TALLOC_FREE(mem_ctx);
	return werr;
}


/**********************************************************************
 *
 * Convenience functions that are also exported.
 *
 **********************************************************************/

WERROR libnet_smbconf_set_global_param(const char *param,
				       const char *val)
{
	return libnet_smbconf_setparm(GLOBAL_NAME, param, val);
}

