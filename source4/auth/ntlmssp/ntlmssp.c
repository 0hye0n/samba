/* 
   Unix SMB/Netbios implementation.
   Version 3.0
   handle NLTMSSP, client server side parsing

   Copyright (C) Andrew Tridgell      2001
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2001-2005
   Copyright (C) Stefan Metzmacher 2005

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
#include "lib/crypto/crypto.h"
#include "auth/ntlmssp/ntlmssp.h"

/**
 * Callbacks for NTLMSSP - for both client and server operating modes
 * 
 */

static const struct ntlmssp_callbacks {
	enum ntlmssp_role role;
	enum ntlmssp_message_type ntlmssp_command;
	NTSTATUS (*fn)(struct gensec_security *gensec_security, 
		       TALLOC_CTX *out_mem_ctx, 
		       DATA_BLOB in, DATA_BLOB *out);
} ntlmssp_callbacks[] = {
	{NTLMSSP_CLIENT, NTLMSSP_INITIAL, ntlmssp_client_initial},
	{NTLMSSP_SERVER, NTLMSSP_NEGOTIATE, ntlmssp_server_negotiate},
	{NTLMSSP_CLIENT, NTLMSSP_CHALLENGE, ntlmssp_client_challenge},
	{NTLMSSP_SERVER, NTLMSSP_AUTH, ntlmssp_server_auth},
};


/**
 * Print out the NTLMSSP flags for debugging 
 * @param neg_flags The flags from the packet
 */

void debug_ntlmssp_flags(uint32_t neg_flags)
{
	DEBUG(3,("Got NTLMSSP neg_flags=0x%08x\n", neg_flags));
	
	if (neg_flags & NTLMSSP_NEGOTIATE_UNICODE) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_UNICODE\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_OEM) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_OEM\n"));
	if (neg_flags & NTLMSSP_REQUEST_TARGET) 
		DEBUGADD(4, ("  NTLMSSP_REQUEST_TARGET\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_SIGN) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_SIGN\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_SEAL) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_SEAL\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_LM_KEY) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_LM_KEY\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_NETWARE) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_NETWARE\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_NTLM) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_NTLM\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_DOMAIN_SUPPLIED) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_DOMAIN_SUPPLIED\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_WORKSTATION_SUPPLIED) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_WORKSTATION_SUPPLIED\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_THIS_IS_LOCAL_CALL) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_THIS_IS_LOCAL_CALL\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_ALWAYS_SIGN) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_ALWAYS_SIGN\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_NTLM2) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_NTLM2\n"));
	if (neg_flags & NTLMSSP_CHAL_TARGET_INFO) 
		DEBUGADD(4, ("  NTLMSSP_CHAL_TARGET_INFO\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_128) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_128\n"));
	if (neg_flags & NTLMSSP_NEGOTIATE_KEY_EXCH) 
		DEBUGADD(4, ("  NTLMSSP_NEGOTIATE_KEY_EXCH\n"));
}

/**
 * Next state function for the wrapped NTLMSSP state machine
 * 
 * @param gensec_security GENSEC state, initialised to NTLMSSP
 * @param out_mem_ctx The TALLOC_CTX for *out to be allocated on
 * @param in The request, as a DATA_BLOB
 * @param out The reply, as an talloc()ed DATA_BLOB, on *out_mem_ctx
 * @return Error, MORE_PROCESSING_REQUIRED if a reply is sent, 
 *                or NT_STATUS_OK if the user is authenticated. 
 */

static NTSTATUS gensec_ntlmssp_update(struct gensec_security *gensec_security, 
				      TALLOC_CTX *out_mem_ctx, 
				      const DATA_BLOB input, DATA_BLOB *out) 
{
	struct gensec_ntlmssp_state *gensec_ntlmssp_state = gensec_security->private_data;
	NTSTATUS status;

	uint32_t ntlmssp_command;
	int i;

	*out = data_blob(NULL, 0);

	if (gensec_ntlmssp_state->expected_state == NTLMSSP_DONE) {
		return NT_STATUS_OK;
	}

	if (!out_mem_ctx) {
		/* if the caller doesn't want to manage/own the memory, 
		   we can put it on our context */
		out_mem_ctx = gensec_ntlmssp_state;
	}

	if (!input.length) {
		switch (gensec_ntlmssp_state->role) {
		case NTLMSSP_CLIENT:
			ntlmssp_command = NTLMSSP_INITIAL;
			break;
		case NTLMSSP_SERVER:
			/* 'datagram' mode - no neg packet */
			ntlmssp_command = NTLMSSP_NEGOTIATE;
			break;
		}
	} else {
		if (!msrpc_parse(gensec_ntlmssp_state, 
				 &input, "Cd",
				 "NTLMSSP",
				 &ntlmssp_command)) {
			DEBUG(1, ("Failed to parse NTLMSSP packet, could not extract NTLMSSP command\n"));
			dump_data(2, input.data, input.length);
			return NT_STATUS_INVALID_PARAMETER;
		}
	}

	if (ntlmssp_command != gensec_ntlmssp_state->expected_state) {
		DEBUG(1, ("got NTLMSSP command %u, expected %u\n", ntlmssp_command, gensec_ntlmssp_state->expected_state));
		return NT_STATUS_INVALID_PARAMETER;
	}

	for (i=0; i < ARRAY_SIZE(ntlmssp_callbacks); i++) {
		if (ntlmssp_callbacks[i].role == gensec_ntlmssp_state->role 
		    && ntlmssp_callbacks[i].ntlmssp_command == ntlmssp_command) {
			status = ntlmssp_callbacks[i].fn(gensec_security, out_mem_ctx, input, out);
			break;
		}
	}

	if (i == ARRAY_SIZE(ntlmssp_callbacks)) {
		
		DEBUG(1, ("failed to find NTLMSSP callback for NTLMSSP mode %u, command %u\n", 
			  gensec_ntlmssp_state->role, ntlmssp_command)); 
		
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (!NT_STATUS_IS_OK(status)) {
		/* error or more processing required */
		return status;
	}
	
	gensec_ntlmssp_state->have_features = 0;

	if (gensec_ntlmssp_state->neg_flags & NTLMSSP_NEGOTIATE_SIGN) {
		gensec_ntlmssp_state->have_features |= GENSEC_FEATURE_SIGN;
	}

	if (gensec_ntlmssp_state->neg_flags & NTLMSSP_NEGOTIATE_SEAL) {
		gensec_ntlmssp_state->have_features |= GENSEC_FEATURE_SEAL;
	}

	if (gensec_ntlmssp_state->session_key.data) {
		gensec_ntlmssp_state->have_features |= GENSEC_FEATURE_SESSION_KEY;
	}

	return status;
}

/**
 * Return the NTLMSSP master session key
 * 
 * @param gensec_ntlmssp_state NTLMSSP State
 */

NTSTATUS gensec_ntlmssp_session_key(struct gensec_security *gensec_security, 
				    DATA_BLOB *session_key)
{
	struct gensec_ntlmssp_state *gensec_ntlmssp_state = gensec_security->private_data;

	if (!gensec_ntlmssp_state->session_key.data) {
		return NT_STATUS_NO_USER_SESSION_KEY;
	}
	*session_key = gensec_ntlmssp_state->session_key;

	return NT_STATUS_OK;
}

void ntlmssp_handle_neg_flags(struct gensec_ntlmssp_state *gensec_ntlmssp_state,
			      uint32_t neg_flags, BOOL allow_lm)
{
	if (neg_flags & NTLMSSP_NEGOTIATE_UNICODE) {
		gensec_ntlmssp_state->neg_flags |= NTLMSSP_NEGOTIATE_UNICODE;
		gensec_ntlmssp_state->neg_flags &= ~NTLMSSP_NEGOTIATE_OEM;
		gensec_ntlmssp_state->unicode = True;
	} else {
		gensec_ntlmssp_state->neg_flags &= ~NTLMSSP_NEGOTIATE_UNICODE;
		gensec_ntlmssp_state->neg_flags |= NTLMSSP_NEGOTIATE_OEM;
		gensec_ntlmssp_state->unicode = False;
	}

	if ((neg_flags & NTLMSSP_NEGOTIATE_LM_KEY) && allow_lm && !gensec_ntlmssp_state->use_ntlmv2) {
		/* other end forcing us to use LM */
		gensec_ntlmssp_state->neg_flags |= NTLMSSP_NEGOTIATE_LM_KEY;
		gensec_ntlmssp_state->neg_flags &= ~NTLMSSP_NEGOTIATE_NTLM2;
	} else {
		gensec_ntlmssp_state->neg_flags &= ~NTLMSSP_NEGOTIATE_LM_KEY;
	}

	if (neg_flags & NTLMSSP_NEGOTIATE_ALWAYS_SIGN) {
		gensec_ntlmssp_state->neg_flags |= NTLMSSP_NEGOTIATE_ALWAYS_SIGN;
	}

	if (!(neg_flags & NTLMSSP_NEGOTIATE_SIGN)) {
		gensec_ntlmssp_state->neg_flags &= ~NTLMSSP_NEGOTIATE_SIGN;
	}

	if (!(neg_flags & NTLMSSP_NEGOTIATE_SEAL)) {
		gensec_ntlmssp_state->neg_flags &= ~NTLMSSP_NEGOTIATE_SEAL;
	}

	if (!(neg_flags & NTLMSSP_NEGOTIATE_NTLM2)) {
		gensec_ntlmssp_state->neg_flags &= ~NTLMSSP_NEGOTIATE_NTLM2;
	}

	if (!(neg_flags & NTLMSSP_NEGOTIATE_128)) {
		gensec_ntlmssp_state->neg_flags &= ~NTLMSSP_NEGOTIATE_128;
		if (neg_flags & NTLMSSP_NEGOTIATE_56) {
			gensec_ntlmssp_state->neg_flags |= NTLMSSP_NEGOTIATE_56;
		}
	}

	if (!(neg_flags & NTLMSSP_NEGOTIATE_56)) {
		gensec_ntlmssp_state->neg_flags &= ~NTLMSSP_NEGOTIATE_56;
	}

	if (!(neg_flags & NTLMSSP_NEGOTIATE_KEY_EXCH)) {
		gensec_ntlmssp_state->neg_flags &= ~NTLMSSP_NEGOTIATE_KEY_EXCH;
	}

	if ((neg_flags & NTLMSSP_REQUEST_TARGET)) {
		gensec_ntlmssp_state->neg_flags |= NTLMSSP_REQUEST_TARGET;
	}
	
}

/**
   Weaken NTLMSSP keys to cope with down-level clients and servers.

   We probably should have some parameters to control this, but as
   it only occours for LM_KEY connections, and this is controlled
   by the client lanman auth/lanman auth parameters, it isn't too bad.
*/

void ntlmssp_weaken_keys(struct gensec_ntlmssp_state *gensec_ntlmssp_state) 
{
	/* Key weakening not performed on the master key for NTLM2
	   and does not occour for NTLM1.  Therefore we only need
	   to do this for the LM_KEY.  
	*/

	if (gensec_ntlmssp_state->neg_flags & NTLMSSP_NEGOTIATE_LM_KEY) {
		if (gensec_ntlmssp_state->neg_flags & NTLMSSP_NEGOTIATE_128) {
			
		} else if (gensec_ntlmssp_state->neg_flags & NTLMSSP_NEGOTIATE_56) {
			gensec_ntlmssp_state->session_key.data[7] = 0xa0;
		} else { /* forty bits */
			gensec_ntlmssp_state->session_key.data[5] = 0xe5;
			gensec_ntlmssp_state->session_key.data[6] = 0x38;
			gensec_ntlmssp_state->session_key.data[7] = 0xb0;
		}
		gensec_ntlmssp_state->session_key.length = 8;
	}
}

static BOOL gensec_ntlmssp_have_feature(struct gensec_security *gensec_security,
					uint32_t feature)
{
	struct gensec_ntlmssp_state *gensec_ntlmssp_state = gensec_security->private_data;
	if (gensec_ntlmssp_state->have_features & feature) {
		return True;
	}

	return False;
}

NTSTATUS gensec_ntlmssp_start(struct gensec_security *gensec_security)
{
	struct gensec_ntlmssp_state *gensec_ntlmssp_state;
	
	gensec_ntlmssp_state = talloc(gensec_security, struct gensec_ntlmssp_state);
	if (!gensec_ntlmssp_state) {
		return NT_STATUS_NO_MEMORY;
	}

	gensec_ntlmssp_state->auth_context = NULL;
	gensec_ntlmssp_state->server_info = NULL;
	gensec_ntlmssp_state->have_features = 0;

	gensec_security->private_data = gensec_ntlmssp_state;
	return NT_STATUS_OK;
}

static const struct gensec_security_ops gensec_ntlmssp_security_ops = {
	.name		= "ntlmssp",
	.sasl_name	= "NTLM",
	.auth_type	= DCERPC_AUTH_TYPE_NTLMSSP,
	.oid            = GENSEC_OID_NTLMSSP,
	.client_start   = gensec_ntlmssp_client_start,
	.server_start   = gensec_ntlmssp_server_start,
	.update 	= gensec_ntlmssp_update,
	.sig_size	= gensec_ntlmssp_sig_size,
	.sign_packet	= gensec_ntlmssp_sign_packet,
	.check_packet	= gensec_ntlmssp_check_packet,
	.seal_packet	= gensec_ntlmssp_seal_packet,
	.unseal_packet	= gensec_ntlmssp_unseal_packet,
	.wrap           = gensec_ntlmssp_wrap,
	.unwrap         = gensec_ntlmssp_unwrap,
	.session_key	= gensec_ntlmssp_session_key,
	.session_info   = gensec_ntlmssp_session_info,
	.have_feature   = gensec_ntlmssp_have_feature,
	.enabled        = True
};


NTSTATUS gensec_ntlmssp_init(void)
{
	NTSTATUS ret;
	ret = gensec_register(&gensec_ntlmssp_security_ops);
	if (!NT_STATUS_IS_OK(ret)) {
		DEBUG(0,("Failed to register '%s' gensec backend!\n",
			gensec_ntlmssp_security_ops.name));
		return ret;
	}

	return ret;
}
