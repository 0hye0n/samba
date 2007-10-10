/* 
   Unix SMB/CIFS implementation.
   Transparent registry backend handling
   Copyright (C) Jelmer Vernooij			2003-2004.

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
#include "registry.h"

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_REGISTRY

static const struct {
	uint32 id;
	const char *name;
} reg_value_types[] = {
	{ REG_SZ, "REG_SZ" },
	{ REG_DWORD, "REG_DWORD" },
	{ REG_BINARY, "REG_BINARY" },
	{ REG_EXPAND_SZ, "REG_EXPAND_SZ" },
	{ REG_NONE, "REG_NONE" },
	{ 0, NULL }
};

/* Return string description of registry value type */
const char *str_regtype(int type)
{
	int i;
	for (i = 0; reg_value_types[i].name; i++) {
		if (reg_value_types[i].id == type) 
			return reg_value_types[i].name;
	}

	return "Unknown";
}

char *reg_val_data_string(TALLOC_CTX *mem_ctx, struct registry_value *v)
{ 
  char *asciip;
  char *ret = NULL;
  int i;

  if(v->data_len == 0) return talloc_strdup(mem_ctx, "");

  switch (v->data_type) {
  case REG_EXPAND_SZ:
  case REG_SZ:
      convert_string_talloc(mem_ctx, CH_UTF16, CH_UNIX, v->data_blk, v->data_len, (void **)&ret);
	  return ret;

  case REG_BINARY:
	  ret = talloc_array(mem_ctx, 3, v->data_len+1, "REG_BINARY");
	  asciip = ret;
	  for (i=0; i<v->data_len; i++) { 
		  int str_rem = v->data_len * 3 - (asciip - ret);
		  asciip += snprintf(asciip, str_rem, "%02x", *(uint8_t *)(((char *)v->data_blk)+i));
		  if (i < v->data_len && str_rem > 0)
			  *asciip = ' '; asciip++;	
	  }
	  *asciip = '\0';
	  return ret;

  case REG_DWORD:
	  if (*(int *)v->data_blk == 0)
		  return talloc_strdup(mem_ctx, "0");

	  return talloc_asprintf(mem_ctx, "0x%x", *(int *)v->data_blk);

  case REG_MULTI_SZ:
	/* FIXME */
    break;

  default:
    break;
  } 

  return ret;
}

char *reg_val_description(TALLOC_CTX *mem_ctx, struct registry_value *val) 
{
	return talloc_asprintf(mem_ctx, "%s = %s : %s", val->name?val->name:"<No Name>", str_regtype(val->data_type), reg_val_data_string(mem_ctx, val));
}

BOOL reg_string_to_val(TALLOC_CTX *mem_ctx, const char *type_str, const char *data_str, struct registry_value **value)
{
	int i;
	*value = talloc_p(mem_ctx, struct registry_value);
	(*value)->data_type = -1;

	/* Find the correct type */
	for (i = 0; reg_value_types[i].name; i++) {
		if (!strcmp(reg_value_types[i].name, type_str)) {
			(*value)->data_type = reg_value_types[i].id;
			break;
		}
	}

	if ((*value)->data_type == -1) 
		return False;

	/* Convert data appropriately */

	switch ((*value)->data_type) 
	{
		case REG_SZ:
		case REG_EXPAND_SZ:
      		(*value)->data_len = convert_string_talloc(mem_ctx, CH_UNIX, CH_UTF16, data_str, strlen(data_str), &(*value)->data_blk);
			break;
		case REG_DWORD:
			(*value)->data_len = sizeof(uint32);
			(*value)->data_blk = talloc_p(mem_ctx, uint32);
			*((uint32 *)(*value)->data_blk) = strtol(data_str, NULL, 0);
			break;

		case REG_NONE:
			(*value)->data_len = 0;
			(*value)->data_blk = NULL;
			break;
	
		default:
		case REG_BINARY: /* FIXME */
			return False;
	}
	return True;
}

WERROR reg_key_get_subkey_val(TALLOC_CTX *mem_ctx, struct registry_key *key, const char *subname, const char *valname, struct registry_value **val)
{
	struct registry_key *k;
	WERROR error = reg_key_get_subkey_by_name(mem_ctx, key, subname, &k);
	if(!W_ERROR_IS_OK(error)) return error;
	
	return reg_key_get_value_by_name(mem_ctx, k, valname, val);
}

/***********************************************************************
 Utility function for splitting the base path of a registry path off
 by setting base and new_path to the apprapriate offsets withing the
 path.
 
 WARNING!!  Does modify the original string!
 ***********************************************************************/

BOOL reg_split_path( char *path, char **base, char **new_path )
{
	char *p;
	
	*new_path = *base = NULL;
	
	if ( !path)
		return False;
	
	*base = path;
	
	p = strchr( path, '\\' );
	
	if ( p ) {
		*p = '\0';
		*new_path = p+1;
	}
	
	return True;
}

/**
 * Replace all \'s with /'s
 */
char *reg_path_win2unix(char *path) 
{
	int i;
	for(i = 0; path[i]; i++) {
		if(path[i] == '\\') path[i] = '/';
	}
	return path;
}
/**
 * Replace all /'s with \'s
 */
char *reg_path_unix2win(char *path) 
{
	int i;
	for(i = 0; path[i]; i++) {
		if(path[i] == '/') path[i] = '\\';
	}
	return path;
}

/* Open a key by name (including the predefined key name!) */
WERROR reg_open_key_abs(TALLOC_CTX *mem_ctx, struct registry_context *handle, const char *name, struct registry_key **result)
{
	struct registry_key *predef;
	WERROR error;
	int predeflength;
	char *predefname;

	if(strchr(name, '\\')) predeflength = strchr(name, '\\')-name;
	else predeflength = strlen(name);

	predefname = strndup(name, predeflength);
	error = reg_get_predefined_key_by_name(handle, predefname, &predef);
	SAFE_FREE(predefname);

	if(!W_ERROR_IS_OK(error)) {
		return error;
	}

	if (strchr(name, '\\')) {
		return reg_open_key(mem_ctx, predef, strchr(name, '\\')+1, result);
	} else {
		*result = predef;
		return WERR_OK;
	}
}

static WERROR get_abs_parent(TALLOC_CTX *mem_ctx, struct registry_context *ctx, const char *path, struct registry_key **parent, const char **name)
{
	char *parent_name;
	WERROR error;
	
	if (strchr(path, '\\') == NULL) {
		return WERR_FOOBAR;
	}
	
	parent_name = talloc_strndup(mem_ctx, path, strrchr(path, '\\')-1-path);

	error = reg_open_key_abs(mem_ctx, ctx, parent_name, parent);
	if (!W_ERROR_IS_OK(error)) {
		return error;
	}
	
	*name = talloc_strdup(mem_ctx, strchr(path, '\\')+1);

	return WERR_OK;
}

WERROR reg_key_del_abs(struct registry_context *ctx, const char *path)
{
	struct registry_key *parent;
	const char *n;
	TALLOC_CTX *mem_ctx = talloc_init("reg_key_del_abs");
	WERROR error;
	
	if (!strchr(path, '\\')) {
		return WERR_FOOBAR;
	}
	
	error = get_abs_parent(mem_ctx, ctx, path, &parent, &n);
	if (W_ERROR_IS_OK(error)) {
		error = reg_key_del(parent, n);
	}

	talloc_destroy(mem_ctx);

	return error;
}

WERROR reg_key_add_abs(TALLOC_CTX *mem_ctx, struct registry_context *ctx, const char *path, uint32 access_mask, struct security_descriptor *sec_desc, struct registry_key **result)
{
	struct registry_key *parent;
	const char *n;
	WERROR error;
	
	if (!strchr(path, '\\')) {
		return WERR_FOOBAR;
	}
	
	error = get_abs_parent(mem_ctx, ctx, path, &parent, &n);
	if (W_ERROR_IS_OK(error)) {
		error = reg_key_add_name(mem_ctx, parent, n, access_mask, sec_desc, result);
	}

	return error;
}
