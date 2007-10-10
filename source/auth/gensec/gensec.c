/* 
   Unix SMB/CIFS implementation.
 
   Generic Authentication Interface

   Copyright (C) Andrew Tridgell 2003
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2004-2005
   
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
#include "auth/auth.h"
#include "lib/events/events.h"

/* the list of currently registered GENSEC backends */
const static struct gensec_security_ops **generic_security_ops;
static int gensec_num_backends;

static const struct gensec_security_ops *gensec_security_by_authtype(uint8_t auth_type)
{
	int i;
	for (i=0; i < gensec_num_backends; i++) {
		if (generic_security_ops[i]->auth_type == auth_type) {
			return generic_security_ops[i];
		}
	}

	return NULL;
}

static const struct gensec_security_ops *gensec_security_by_oid(const char *oid_string)
{
	int i, j;
	for (i=0; i < gensec_num_backends; i++) {
		if (generic_security_ops[i]->oid) {
			for (j=0; generic_security_ops[i]->oid[j]; j++) { 
				if (generic_security_ops[i]->oid[j] &&
				    (strcmp(generic_security_ops[i]->oid[j], oid_string) == 0)) {
					return generic_security_ops[i];
				}
			}
		}
	}

	return NULL;
}

static const struct gensec_security_ops *gensec_security_by_sasl_name(const char *sasl_name)
{
	int i;
	for (i=0; i < gensec_num_backends; i++) {
		if (generic_security_ops[i]->sasl_name 
		    && (strcmp(generic_security_ops[i]->sasl_name, sasl_name) == 0)) {
			return generic_security_ops[i];
		}
	}

	return NULL;
}

static const struct gensec_security_ops *gensec_security_by_name(const char *name)
{
	int i;
	for (i=0; i < gensec_num_backends; i++) {
		if (generic_security_ops[i]->name 
		    && (strcmp(generic_security_ops[i]->name, name) == 0)) {
			return generic_security_ops[i];
		}
	}

	return NULL;
}

const struct gensec_security_ops **gensec_security_all(int *num_backends_out)
{
	*num_backends_out = gensec_num_backends;
	return generic_security_ops;
}

/**
 * Return a unique list of security subsystems from those specified in
 * the OID list.  That is, where two OIDs refer to the same module,
 * return that module only once 
 *
 * The list is in the exact order of the OIDs asked for, where available.
 */

const struct gensec_security_ops_wrapper *gensec_security_by_oid_list(TALLOC_CTX *mem_ctx, 
								      const char **oid_strings,
								      const char *skip)
{
	struct gensec_security_ops_wrapper *backends_out;
	const struct gensec_security_ops **backends;
	int i, j, k, oid_idx;
	int num_backends_out = 0;
	int num_backends;

	if (!oid_strings) {
		return NULL;
	}

	backends = gensec_security_all(&num_backends);

	backends_out = talloc_array(mem_ctx, struct gensec_security_ops_wrapper, 1);
	if (!backends_out) {
		return NULL;
	}
	backends_out[0].op = NULL;
	backends_out[0].oid = NULL;

	for (oid_idx = 0; oid_strings[oid_idx]; oid_idx++) {
		if (strcmp(oid_strings[oid_idx], skip) == 0) {
			continue;
		}

		for (i=0; i < num_backends; i++) {
			if (!backends[i]->oid) {
				continue;
			}
			for (j=0; backends[i]->oid[j]; j++) { 
				if (!backends[i]->oid[j] ||
				    !(strcmp(backends[i]->oid[j], 
					    oid_strings[oid_idx]) == 0)) {
					continue;
				}
				
				for (k=0; backends_out[k].op; k++) {
					if (backends_out[k].op == backends[i]) {
						break;
					}
				}
				
				if (k < num_backends_out) {
					/* already in there */
					continue;
				}

				backends_out = talloc_realloc(mem_ctx, backends_out, 
							      struct gensec_security_ops_wrapper, 
							      num_backends_out + 2);
				if (!backends_out) {
					return NULL;
				}
				
				backends_out[num_backends_out].op = backends[i];
				backends_out[num_backends_out].oid = backends[i]->oid[j];
				num_backends_out++;
				backends_out[num_backends_out].op = NULL;
				backends_out[num_backends_out].oid = NULL;
			}
		}
	}
	return backends_out;
}

/**
 * Return OIDS from the security subsystems listed
 */

const char **gensec_security_oids_from_ops(TALLOC_CTX *mem_ctx, 
					   const struct gensec_security_ops **ops,				   
					   int num_backends,
					   const char *skip) 
{
	int i;
	int j = 0;
	int k;
	const char **oid_list;
	if (!ops) {
		return NULL;
	}
	oid_list = talloc_array(mem_ctx, const char *, 1);
	if (!oid_list) {
		return NULL;
	}
	
	for (i=0; i<num_backends; i++) {
		if (!ops[i]->oid) {
			continue;
		}
		
		for (k = 0; ops[i]->oid[k]; k++) {
			if (skip && strcmp(skip, ops[i]->oid[k])==0) {
			} else {
				oid_list = talloc_realloc(mem_ctx, oid_list, const char *, j + 2);
				if (!oid_list) {
					return NULL;
				}
				oid_list[j] = ops[i]->oid[k];
				j++;
			}
		}
	}
	oid_list[j] = NULL;
	return oid_list;
}


/**
 * Return all the security subsystems currently enabled in GENSEC 
 */

const char **gensec_security_oids(TALLOC_CTX *mem_ctx, const char *skip) 
{
	int num_backends;
	const struct gensec_security_ops **ops = gensec_security_all(&num_backends);
	return gensec_security_oids_from_ops(mem_ctx, ops, 
					     num_backends, skip);
}



/**
  Start the GENSEC system, returning a context pointer.
  @param mem_ctx The parent TALLOC memory context.
  @param gensec_security Returned GENSEC context pointer.
  @note  The mem_ctx is only a parent and may be NULL.
*/
static NTSTATUS gensec_start(TALLOC_CTX *mem_ctx, 
			     struct gensec_security **gensec_security,
			     struct event_context *ev) 
{
	(*gensec_security) = talloc(mem_ctx, struct gensec_security);
	NT_STATUS_HAVE_NO_MEMORY(*gensec_security);

	(*gensec_security)->ops = NULL;

	ZERO_STRUCT((*gensec_security)->target);

	(*gensec_security)->subcontext = False;
	(*gensec_security)->want_features = 0;
	
	if (ev == NULL) {
		ev = event_context_init(*gensec_security);
		if (ev == NULL) {
			talloc_free(*gensec_security);
			return NT_STATUS_NO_MEMORY;
		}
	}

	(*gensec_security)->event_ctx = ev;

	return NT_STATUS_OK;
}

/** 
 * Start a GENSEC subcontext, with a copy of the properties of the parent
 * @param mem_ctx The parent TALLOC memory context.
 * @param parent The parent GENSEC context 
 * @param gensec_security Returned GENSEC context pointer.
 * @note Used by SPNEGO in particular, for the actual implementation mechanism
 */

NTSTATUS gensec_subcontext_start(TALLOC_CTX *mem_ctx, 
				 struct gensec_security *parent, 
				 struct gensec_security **gensec_security)
{
	(*gensec_security) = talloc(mem_ctx, struct gensec_security);
	NT_STATUS_HAVE_NO_MEMORY(*gensec_security);

	(**gensec_security) = *parent;
	(*gensec_security)->ops = NULL;
	(*gensec_security)->private_data = NULL;

	(*gensec_security)->subcontext = True;
	(*gensec_security)->event_ctx = parent->event_ctx;

	return NT_STATUS_OK;
}

/**
  Start the GENSEC system, in client mode, returning a context pointer.
  @param mem_ctx The parent TALLOC memory context.
  @param gensec_security Returned GENSEC context pointer.
  @note  The mem_ctx is only a parent and may be NULL.
*/
NTSTATUS gensec_client_start(TALLOC_CTX *mem_ctx, 
			     struct gensec_security **gensec_security,
			     struct event_context *ev)
{
	NTSTATUS status;
	status = gensec_start(mem_ctx, gensec_security, ev);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}
	(*gensec_security)->gensec_role = GENSEC_CLIENT;

	return status;
}

/**
  Start the GENSEC system, in server mode, returning a context pointer.
  @param mem_ctx The parent TALLOC memory context.
  @param gensec_security Returned GENSEC context pointer.
  @note  The mem_ctx is only a parent and may be NULL.
*/
NTSTATUS gensec_server_start(TALLOC_CTX *mem_ctx, 
			     struct gensec_security **gensec_security,
			     struct event_context *ev)
{
	NTSTATUS status;
	status = gensec_start(mem_ctx, gensec_security, ev);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}
	(*gensec_security)->gensec_role = GENSEC_SERVER;

	return status;
}

static NTSTATUS gensec_start_mech(struct gensec_security *gensec_security) 
{
	NTSTATUS status;
	DEBUG(5, ("Starting GENSEC %smechanism %s\n", 
		  gensec_security->subcontext ? "sub" : "", 
		  gensec_security->ops->name));
	switch (gensec_security->gensec_role) {
	case GENSEC_CLIENT:
		if (gensec_security->ops->client_start) {
			status = gensec_security->ops->client_start(gensec_security);
			if (!NT_STATUS_IS_OK(status)) {
				DEBUG(2, ("Failed to start GENSEC client mech %s: %s\n",
					  gensec_security->ops->name, nt_errstr(status))); 
			}
			return status;
		}
	case GENSEC_SERVER:
		if (gensec_security->ops->server_start) {
			status = gensec_security->ops->server_start(gensec_security);
			if (!NT_STATUS_IS_OK(status)) {
				DEBUG(1, ("Failed to start GENSEC server mech %s: %s\n",
					  gensec_security->ops->name, nt_errstr(status))); 
			}
			return status;
		}
	}
	return NT_STATUS_INVALID_PARAMETER;
}

/** 
 * Start a GENSEC sub-mechanism by DCERPC allocated 'auth type' number 
 * @param gensec_security GENSEC context pointer.
 * @param auth_type DCERPC auth type
 * @param auth_level DCERPC auth level 
 */

NTSTATUS gensec_start_mech_by_authtype(struct gensec_security *gensec_security, 
				       uint8_t auth_type, uint8_t auth_level) 
{
	gensec_security->ops = gensec_security_by_authtype(auth_type);
	if (!gensec_security->ops) {
		DEBUG(3, ("Could not find GENSEC backend for auth_type=%d\n", (int)auth_type));
		return NT_STATUS_INVALID_PARAMETER;
	}
	gensec_want_feature(gensec_security, GENSEC_FEATURE_DCE_STYLE);
	if (auth_level == DCERPC_AUTH_LEVEL_INTEGRITY) {
		gensec_want_feature(gensec_security, GENSEC_FEATURE_SIGN);
	} else if (auth_level == DCERPC_AUTH_LEVEL_PRIVACY) {
		gensec_want_feature(gensec_security, GENSEC_FEATURE_SIGN);
		gensec_want_feature(gensec_security, GENSEC_FEATURE_SEAL);
	} else if (auth_level == DCERPC_AUTH_LEVEL_CONNECT) {
		/* Default features */
	} else {
		DEBUG(2,("auth_level %d not supported in DCE/RPC authentication\n", 
			 auth_level));
		return NT_STATUS_INVALID_PARAMETER;
	}

	return gensec_start_mech(gensec_security);
}

const char *gensec_get_name_by_authtype(uint8_t authtype) 
{
	const struct gensec_security_ops *ops;
	ops = gensec_security_by_authtype(authtype);
	if (ops) {
		return ops->name;
	}
	return NULL;
}
	

const char *gensec_get_name_by_oid(const char *oid_string) 
{
	const struct gensec_security_ops *ops;
	ops = gensec_security_by_oid(oid_string);
	if (ops) {
		return ops->name;
	}
	return NULL;
}
	

/** 
 * Start a GENSEC sub-mechanism with a specifed mechansim structure, used in SPNEGO
 *
 */

NTSTATUS gensec_start_mech_by_ops(struct gensec_security *gensec_security, 
				  const struct gensec_security_ops *ops) 
{
	gensec_security->ops = ops;
	return gensec_start_mech(gensec_security);
}

/** 
 * Start a GENSEC sub-mechanism by OID, used in SPNEGO
 *
 * @note This should also be used when you wish to just start NLTMSSP (for example), as it uses a
 *       well-known #define to hook it in.
 */

NTSTATUS gensec_start_mech_by_oid(struct gensec_security *gensec_security, 
				  const char *mech_oid) 
{
	gensec_security->ops = gensec_security_by_oid(mech_oid);
	if (!gensec_security->ops) {
		DEBUG(3, ("Could not find GENSEC backend for oid=%s\n", mech_oid));
		return NT_STATUS_INVALID_PARAMETER;
	}
	return gensec_start_mech(gensec_security);
}

/** 
 * Start a GENSEC sub-mechanism by a well know SASL name
 *
 */

NTSTATUS gensec_start_mech_by_sasl_name(struct gensec_security *gensec_security, 
					const char *sasl_name) 
{
	gensec_security->ops = gensec_security_by_sasl_name(sasl_name);
	if (!gensec_security->ops) {
		DEBUG(3, ("Could not find GENSEC backend for sasl_name=%s\n", sasl_name));
		return NT_STATUS_INVALID_PARAMETER;
	}
	return gensec_start_mech(gensec_security);
}

/*
  wrappers for the gensec function pointers
*/
NTSTATUS gensec_unseal_packet(struct gensec_security *gensec_security, 
			      TALLOC_CTX *mem_ctx, 
			      uint8_t *data, size_t length, 
			      const uint8_t *whole_pdu, size_t pdu_length, 
			      const DATA_BLOB *sig)
{
	if (!gensec_security->ops->unseal_packet) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}
	if (!gensec_have_feature(gensec_security, GENSEC_FEATURE_SEAL)) {
		if (gensec_have_feature(gensec_security, GENSEC_FEATURE_SIGN)) {
			return gensec_check_packet(gensec_security, mem_ctx, 
						   data, length, 
						   whole_pdu, pdu_length, 
						   sig);
		}
		return NT_STATUS_INVALID_PARAMETER;
	}

	return gensec_security->ops->unseal_packet(gensec_security, mem_ctx, 
						   data, length, 
						   whole_pdu, pdu_length, 
						   sig);
}

NTSTATUS gensec_check_packet(struct gensec_security *gensec_security, 
			     TALLOC_CTX *mem_ctx, 
			     const uint8_t *data, size_t length, 
			     const uint8_t *whole_pdu, size_t pdu_length, 
			     const DATA_BLOB *sig)
{
	if (!gensec_security->ops->check_packet) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}
	if (!gensec_have_feature(gensec_security, GENSEC_FEATURE_SIGN)) {
		return NT_STATUS_INVALID_PARAMETER;
	}
	
	return gensec_security->ops->check_packet(gensec_security, mem_ctx, data, length, whole_pdu, pdu_length, sig);
}

NTSTATUS gensec_seal_packet(struct gensec_security *gensec_security, 
			    TALLOC_CTX *mem_ctx, 
			    uint8_t *data, size_t length, 
			    const uint8_t *whole_pdu, size_t pdu_length, 
			    DATA_BLOB *sig)
{
	if (!gensec_security->ops->seal_packet) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}
	if (!gensec_have_feature(gensec_security, GENSEC_FEATURE_SEAL)) {
		if (gensec_have_feature(gensec_security, GENSEC_FEATURE_SIGN)) {
			return gensec_sign_packet(gensec_security, mem_ctx, 
						  data, length, 
						  whole_pdu, pdu_length, 
						  sig);
		}
		return NT_STATUS_INVALID_PARAMETER;
	}

	return gensec_security->ops->seal_packet(gensec_security, mem_ctx, data, length, whole_pdu, pdu_length, sig);
}

NTSTATUS gensec_sign_packet(struct gensec_security *gensec_security, 
			    TALLOC_CTX *mem_ctx, 
			    const uint8_t *data, size_t length, 
			    const uint8_t *whole_pdu, size_t pdu_length, 
			    DATA_BLOB *sig)
{
	if (!gensec_security->ops->sign_packet) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}
	if (!gensec_have_feature(gensec_security, GENSEC_FEATURE_SIGN)) {
		return NT_STATUS_INVALID_PARAMETER;
	}
	
	return gensec_security->ops->sign_packet(gensec_security, mem_ctx, data, length, whole_pdu, pdu_length, sig);
}

size_t gensec_sig_size(struct gensec_security *gensec_security) 
{
	if (!gensec_security->ops->sig_size) {
		return 0;
	}
	if (!gensec_have_feature(gensec_security, GENSEC_FEATURE_SIGN)) {
		return 0;
	}
	
	return gensec_security->ops->sig_size(gensec_security);
}

NTSTATUS gensec_wrap(struct gensec_security *gensec_security, 
		     TALLOC_CTX *mem_ctx, 
		     const DATA_BLOB *in, 
		     DATA_BLOB *out) 
{
	if (!gensec_security->ops->wrap) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}
	return gensec_security->ops->wrap(gensec_security, mem_ctx, in, out);
}

NTSTATUS gensec_unwrap(struct gensec_security *gensec_security, 
		       TALLOC_CTX *mem_ctx, 
		       const DATA_BLOB *in, 
		       DATA_BLOB *out) 
{
	if (!gensec_security->ops->unwrap) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}
	return gensec_security->ops->unwrap(gensec_security, mem_ctx, in, out);
}

NTSTATUS gensec_session_key(struct gensec_security *gensec_security, 
			    DATA_BLOB *session_key)
{
	if (!gensec_security->ops->session_key) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}
	return gensec_security->ops->session_key(gensec_security, session_key);
}

/** 
 * Return the credentials of a logged on user, including session keys
 * etc.
 *
 * Only valid after a successful authentication
 *
 * May only be called once per authentication.
 *
 */

NTSTATUS gensec_session_info(struct gensec_security *gensec_security, 
			     struct auth_session_info **session_info)
{
	if (!gensec_security->ops->session_info) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}
	return gensec_security->ops->session_info(gensec_security, session_info);
}

/**
 * Next state function for the GENSEC state machine
 * 
 * @param gensec_security GENSEC State
 * @param out_mem_ctx The TALLOC_CTX for *out to be allocated on
 * @param in The request, as a DATA_BLOB
 * @param out The reply, as an talloc()ed DATA_BLOB, on *out_mem_ctx
 * @return Error, MORE_PROCESSING_REQUIRED if a reply is sent, 
 *                or NT_STATUS_OK if the user is authenticated. 
 */

NTSTATUS gensec_update(struct gensec_security *gensec_security, TALLOC_CTX *out_mem_ctx, 
		       const DATA_BLOB in, DATA_BLOB *out) 
{
	return gensec_security->ops->update(gensec_security, out_mem_ctx, in, out);
}

/** 
 * Set the requirement for a certain feature on the connection
 *
 */

void gensec_want_feature(struct gensec_security *gensec_security,
			 uint32_t feature) 
{
	gensec_security->want_features |= feature;
}

/** 
 * Check the requirement for a certain feature on the connection
 *
 */

BOOL gensec_have_feature(struct gensec_security *gensec_security,
			 uint32_t feature) 
{
	if (!gensec_security->ops->have_feature) {
		return False;
	}
	return gensec_security->ops->have_feature(gensec_security, feature);
}

/** 
 * Associate a credentails structure with a GENSEC context - talloc_reference()s it to the context 
 *
 */

NTSTATUS gensec_set_credentials(struct gensec_security *gensec_security, struct cli_credentials *credentials) 
{
	gensec_security->credentials = talloc_reference(gensec_security, credentials);
	return NT_STATUS_OK;
}

/** 
 * Return the credentails structure associated with a GENSEC context
 *
 */

struct cli_credentials *gensec_get_credentials(struct gensec_security *gensec_security) 
{
	return gensec_security->credentials;
}

/** 
 * Set the target service (such as 'http' or 'host') on a GENSEC context - ensures it is talloc()ed 
 *
 */

NTSTATUS gensec_set_target_service(struct gensec_security *gensec_security, const char *service) 
{
	gensec_security->target.service = talloc_strdup(gensec_security, service);
	if (!gensec_security->target.service) {
		return NT_STATUS_NO_MEMORY;
	}
	return NT_STATUS_OK;
}

/** 
 * Set the target hostname (suitable for kerberos resolutation) on a GENSEC context - ensures it is talloc()ed 
 *
 */

NTSTATUS gensec_set_target_hostname(struct gensec_security *gensec_security, const char *hostname) 
{
	gensec_security->target.hostname = talloc_strdup(gensec_security, hostname);
	if (!gensec_security->target.hostname) {
		return NT_STATUS_NO_MEMORY;
	}
	return NT_STATUS_OK;
}

const char *gensec_get_target_hostname(struct gensec_security *gensec_security) 
{
	if (gensec_security->target.hostname) {
		return gensec_security->target.hostname;
	}

	/* TODO: Add a 'set sockaddr' call, and do a reverse lookup */
	return NULL;
}

const char *gensec_get_target_service(struct gensec_security *gensec_security) 
{
	if (gensec_security->target.service) {
		return gensec_security->target.service;
	}

	return "host";
}

/*
  register a GENSEC backend. 

  The 'name' can be later used by other backends to find the operations
  structure for this backend.
*/
NTSTATUS gensec_register(const void *_ops)
{
	const struct gensec_security_ops *ops = _ops;
	
	if (!lp_parm_bool(-1, "gensec", ops->name, ops->enabled)) {
		DEBUG(2,("gensec subsystem %s is disabled\n", ops->name));
		return NT_STATUS_OK;
	}

	if (gensec_security_by_name(ops->name) != NULL) {
		/* its already registered! */
		DEBUG(0,("GENSEC backend '%s' already registered\n", 
			 ops->name));
		return NT_STATUS_OBJECT_NAME_COLLISION;
	}

	generic_security_ops = realloc_p(generic_security_ops, 
					 const struct gensec_security_ops *, 
					 gensec_num_backends+1);
	if (!generic_security_ops) {
		smb_panic("out of memory in gensec_register");
	}

	generic_security_ops[gensec_num_backends] = ops;

	gensec_num_backends++;

	DEBUG(3,("GENSEC backend '%s' registered\n", 
		 ops->name));

	return NT_STATUS_OK;
}

/*
  return the GENSEC interface version, and the size of some critical types
  This can be used by backends to either detect compilation errors, or provide
  multiple implementations for different smbd compilation options in one module
*/
const struct gensec_critical_sizes *gensec_interface_version(void)
{
	static const struct gensec_critical_sizes critical_sizes = {
		GENSEC_INTERFACE_VERSION,
		sizeof(struct gensec_security_ops),
		sizeof(struct gensec_security),
	};

	return &critical_sizes;
}

/*
  initialise the GENSEC subsystem
*/
NTSTATUS gensec_init(void)
{
	return NT_STATUS_OK;
}
