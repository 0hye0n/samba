/* 
   Unix SMB/CIFS implementation.

   manipulate nbt name structures

   Copyright (C) Andrew Tridgell 2005
   
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

/*
  see rfc1002 for the detailed format of compressed names
*/

#include "includes.h"
#include "system/iconv.h"
#include "librpc/gen_ndr/ndr_nbt.h"
#include "librpc/gen_ndr/ndr_misc.h"

/* don't allow an unlimited number of name components */
#define MAX_COMPONENTS 10

/*
  print a nbt string
*/
void ndr_print_nbt_string(struct ndr_print *ndr, const char *name, const char *s)
{
	ndr_print_string(ndr, name, s);
}

/*
  pull one component of a nbt_string
*/
static NTSTATUS ndr_pull_component(struct ndr_pull *ndr, uint8_t **component,
				   uint32_t *offset, uint32_t *max_offset)
{
	uint8_t len;
	uint_t loops = 0;
	while (loops < 5) {
		if (*offset >= ndr->data_size) {
			return NT_STATUS_BAD_NETWORK_NAME;
		}
		len = ndr->data[*offset];
		if (len == 0) {
			*offset += 1;
			*max_offset = MAX(*max_offset, *offset);
			*component = NULL;
			return NT_STATUS_OK;
		}
		if ((len & 0xC0) == 0xC0) {
			/* its a label pointer */
			if (1 + *offset >= ndr->data_size) {
				return NT_STATUS_BAD_NETWORK_NAME;
			}
			*max_offset = MAX(*max_offset, *offset + 2);
			*offset = ((len&0x3F)<<8) | ndr->data[1 + *offset];
			*max_offset = MAX(*max_offset, *offset);
			loops++;
			continue;
		}
		if ((len & 0xC0) != 0) {
			/* its a reserved length field */
			return NT_STATUS_BAD_NETWORK_NAME;
		}
		if (*offset + len + 2 > ndr->data_size) {
			return NT_STATUS_BAD_NETWORK_NAME;
		}
		*component = (uint8_t*)talloc_strndup(ndr, &ndr->data[1 + *offset], len);
		NT_STATUS_HAVE_NO_MEMORY(*component);
		*offset += len + 1;
		*max_offset = MAX(*max_offset, *offset);
		return NT_STATUS_OK;
	}

	/* too many pointers */
	return NT_STATUS_BAD_NETWORK_NAME;
}

/*
  pull a nbt_string from the wire
*/
NTSTATUS ndr_pull_nbt_string(struct ndr_pull *ndr, int ndr_flags, const char **s)
{
	NTSTATUS status;
	uint32_t offset = ndr->offset;
	uint32_t max_offset = offset;
	unsigned num_components;
	char *name;

	if (!(ndr_flags & NDR_SCALARS)) {
		return NT_STATUS_OK;
	}

	name = NULL;

	/* break up name into a list of components */
	for (num_components=0;num_components<MAX_COMPONENTS;num_components++) {
		uint8_t *component;
		status = ndr_pull_component(ndr, &component, &offset, &max_offset);
		NT_STATUS_NOT_OK_RETURN(status);
		if (component == NULL) break;
		if (name) {
			name = talloc_asprintf_append(name, ".%s", component);
			NT_STATUS_HAVE_NO_MEMORY(name);
		} else {
			name = component;
		}
	}
	if (num_components == MAX_COMPONENTS) {
		return NT_STATUS_BAD_NETWORK_NAME;
	}
	if (num_components == 0) {
		name = talloc_strdup(ndr, "");
		NT_STATUS_HAVE_NO_MEMORY(name);
	}

	(*s) = name;
	ndr->offset = max_offset;

	return NT_STATUS_OK;
}

/*
  push a nbt string to the wire
*/
NTSTATUS ndr_push_nbt_string(struct ndr_push *ndr, int ndr_flags, const char *s)
{
	if (!(ndr_flags & NDR_SCALARS)) {
		return NT_STATUS_OK;
	}

	while (s && *s) {
		NTSTATUS status;
		char *compname;
		size_t complen;
		uint32_t offset;

		/* see if we have pushed the remaing string allready,
		 * if so we use a label pointer to this string
		 */
		status = ndr_token_retrieve_cmp_fn(&ndr->nbt_string_list, s, &offset, (comparison_fn_t)strcmp, False);
		if (NT_STATUS_IS_OK(status)) {
			uint8_t b[2];
			
			if (offset > 0x3FFF) {
				return ndr_push_error(ndr, NDR_ERR_STRING,
						      "offset for nbt string label pointer %u[%08X] > 0x00003FFF",
						      offset, offset);
			}

			b[0] = 0xC0 | (offset>>8);
			b[1] = (offset & 0xFF);

			return ndr_push_bytes(ndr, b, 2);
		}

		complen = strcspn(s, ".");

		/* we need to make sure the length fits into 6 bytes */
		if (complen >= 0x3F) {
			return ndr_push_error(ndr, NDR_ERR_STRING,
					      "component length %u[%08X] > 0x00003F",
					      (unsigned)complen, (unsigned)complen);
		}

		compname = talloc_asprintf(ndr, "%c%*.*s",
						(unsigned char)complen,
						(unsigned char)complen,
						(unsigned char)complen, s);
		NT_STATUS_HAVE_NO_MEMORY(compname);

		/* remember the current componemt + the rest of the string
		 * so it can be reused later
		 */
		NDR_CHECK(ndr_token_store(ndr, &ndr->nbt_string_list, s, ndr->offset));

		/* push just this component into the blob */
		NDR_CHECK(ndr_push_bytes(ndr, (const uint8_t *)compname, complen+1));
		talloc_free(compname);

		s += complen;
		if (*s == '.') s++;
	}

	/* if we reach the end of the string and have pushed the last component
	 * without using a label pointer, we need to terminate the string
	 */
	return ndr_push_bytes(ndr, (const uint8_t *)"", 1);
}


/*
  decompress a 'compressed' name component
 */
static NTSTATUS decompress_name(char *name, enum nbt_name_type *type)
{
	int i;
	for (i=0;name[2*i];i++) {
		uint8_t c1 = name[2*i];
		uint8_t c2 = name[1+(2*i)];
		if (c1 < 'A' || c1 > 'P' ||
		    c2 < 'A' || c2 > 'P') {
			return NT_STATUS_BAD_NETWORK_NAME;
		}
		name[i] = ((c1-'A')<<4) | (c2-'A');		    
	}
	name[i] = 0;
	if (i == 16) {
		*type = (enum nbt_name_type)(name[15]);
		name[15] = 0;
		i--;
	} else {
		*type = NBT_NAME_CLIENT;
	}

	/* trim trailing spaces */
	for (;i>0 && name[i-1]==' ';i--) {
		name[i-1] = 0;
	}
	
	return NT_STATUS_OK;
}


/*
  compress a name component
 */
static uint8_t *compress_name(TALLOC_CTX *mem_ctx, 
			      const uint8_t *name, enum nbt_name_type type)
{
	uint8_t *cname;
	int i;
	uint8_t pad_char;

	if (strlen(name) > 15) {
		return NULL;
	}

	cname = talloc_array(mem_ctx, uint8_t, 33);
	if (cname == NULL) return NULL;

	for (i=0;name[i];i++) {
		cname[2*i]   = 'A' + (name[i]>>4);
		cname[1+2*i] = 'A' + (name[i]&0xF);
	}
	if (strcmp(name, "*") == 0) {
		pad_char = 0;
	} else {
		pad_char = ' ';
	}
	for (;i<15;i++) {
		cname[2*i]   = 'A' + (pad_char>>4);
		cname[1+2*i] = 'A' + (pad_char&0xF);
	}

	pad_char = type;
	cname[2*i]   = 'A' + (pad_char>>4);
	cname[1+2*i] = 'A' + (pad_char&0xF);

	cname[32] = 0;
	return cname;
}


/*
  pull a nbt name from the wire
*/
NTSTATUS ndr_pull_nbt_name(struct ndr_pull *ndr, int ndr_flags, struct nbt_name *r)
{
	NTSTATUS status;
	uint8_t *scope;
	char *cname;
	const char *s;

	if (!(ndr_flags & NDR_SCALARS)) {
		return NT_STATUS_OK;
	}

	status = ndr_pull_nbt_string(ndr, ndr_flags, &s);
	NT_STATUS_NOT_OK_RETURN(status);

	scope = strchr(s, '.');
	if (scope) {
		*scope = 0;
		r->scope = talloc_strdup(ndr, scope+1);
		NT_STATUS_HAVE_NO_MEMORY(r->scope);
	} else {
		r->scope = NULL;
	}

	cname = discard_const_p(char, s);

	/* the first component is limited to 16 bytes in the DOS charset,
	   which is 32 in the 'compressed' form */
	if (strlen(cname) > 32) {
		return NT_STATUS_BAD_NETWORK_NAME;
	}

	/* decompress the first component */
	status = decompress_name(cname, &r->type);
	NT_STATUS_NOT_OK_RETURN(status);

	r->name = talloc_strdup(ndr, cname);
	NT_STATUS_HAVE_NO_MEMORY(r->name);

	talloc_free(cname);

	return NT_STATUS_OK;
}

/*
  push a nbt name to the wire
*/
NTSTATUS ndr_push_nbt_name(struct ndr_push *ndr, int ndr_flags, const struct nbt_name *r)
{
	uint8_t *cname, *fullname;
	NTSTATUS status;

	if (!(ndr_flags & NDR_SCALARS)) {
		return NT_STATUS_OK;
	}

	cname = compress_name(ndr, r->name, r->type);
	NT_STATUS_HAVE_NO_MEMORY(cname);

	if (r->scope) {
		fullname = talloc_asprintf(ndr, "%s.%s", cname, r->scope);
		NT_STATUS_HAVE_NO_MEMORY(fullname);
		talloc_free(cname);
	} else {
		fullname = cname;
	}
	
	status = ndr_push_nbt_string(ndr, ndr_flags, fullname);
#if 0
	/* this free conflicts with the use of pointers into strings
	   in the ndr_token_store() calls above. Metze, can you look
	   at this? */
	talloc_free(fullname);
#endif
	return status;
}


/*
  copy a nbt name structure
*/
NTSTATUS nbt_name_dup(TALLOC_CTX *mem_ctx, struct nbt_name *name, struct nbt_name *newname)
{
	*newname = *name;
	newname->name = talloc_strdup(mem_ctx, newname->name);
	NT_STATUS_HAVE_NO_MEMORY(newname->name);
	newname->scope = talloc_strdup(mem_ctx, newname->scope);
	if (name->scope) {
		NT_STATUS_HAVE_NO_MEMORY(newname->scope);
	}
	return NT_STATUS_OK;
}

/*
  push a nbt name into a blob
*/
NTSTATUS nbt_name_to_blob(TALLOC_CTX *mem_ctx, DATA_BLOB *blob, struct nbt_name *name)
{
	return ndr_push_struct_blob(blob, mem_ctx, name, 
				    (ndr_push_flags_fn_t)ndr_push_nbt_name);
}


/*
  pull a nbt name from a blob
*/
NTSTATUS nbt_name_from_blob(TALLOC_CTX *mem_ctx, const DATA_BLOB *blob, struct nbt_name *name)
{
	return ndr_pull_struct_blob(blob, mem_ctx, name, 
				    (ndr_pull_flags_fn_t)ndr_pull_nbt_name);
}


/*
  choose a name to use when calling a server in a NBT session request.
  we use heuristics to see if the name we have been given is a IP
  address, or a too-long name. If it is then use *SMBSERVER, or a
  truncated name
*/
void nbt_choose_called_name(TALLOC_CTX *mem_ctx,
			    struct nbt_name *n, const char *name, int type)
{
	n->scope = NULL;
	n->type = type;

	if (is_ipaddress(name)) {
		n->name = "*SMBSERVER";
		return;
	}
	if (strlen(name) > 15) {
		const char *p = strchr(name, '.');
		char *s;
		if (p - name > 15) {
			n->name = "*SMBSERVER";
			return;
		}
		s = talloc_strndup(mem_ctx, name, PTR_DIFF(p, name));
		n->name = strupper_talloc(mem_ctx, s);
		return;
	}

	n->name = strupper_talloc(mem_ctx, name);
}


/*
  escape a string into a form containing only a small set of characters,
  the rest is hex encoded. This is similar to URL encoding
*/
static const char *nbt_hex_encode(TALLOC_CTX *mem_ctx, const char *s)
{
	int i, len;
	char *ret;
	const char *valid_chars = "_-.$@ ";
#define NBT_CHAR_ALLOW(c) (isalnum((unsigned char)c) || strchr(valid_chars, c))

	for (len=i=0;s[i];i++,len++) {
		if (!NBT_CHAR_ALLOW(s[i])) {
			len += 2;
		}
	}

	ret = talloc_array(mem_ctx, char, len+1);
	if (ret == NULL) return NULL;

	for (len=i=0;s[i];i++) {
		if (NBT_CHAR_ALLOW(s[i])) {
			ret[len++] = s[i];
		} else {
			snprintf(&ret[len], 4, "%%%02x", (unsigned char)s[i]);
			len += 3;
		}
	}
	ret[len] = 0;

	return ret;
}


/*
  form a string for a NBT name
*/
char *nbt_name_string(TALLOC_CTX *mem_ctx, const struct nbt_name *name)
{
	TALLOC_CTX *tmp_ctx = talloc_new(mem_ctx);
	char *ret;
	if (name->scope) {		
		ret = talloc_asprintf(mem_ctx, "%s<%02x>-%s",
				      nbt_hex_encode(tmp_ctx, name->name),
				      name->type, 
				      nbt_hex_encode(tmp_ctx, name->scope));
	} else {
		ret = talloc_asprintf(mem_ctx, "%s<%02x>", 
				      nbt_hex_encode(tmp_ctx, name->name), 
				      name->type);
	}
	talloc_free(tmp_ctx);
	return ret;
}

