/* 
   Unix SMB/CIFS implementation.
   SMB Signing Code
   Copyright (C) Jeremy Allison 2002.
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2002-2003
   Copyright (C) James J Myers <myersjj@samba.org> 2003
   
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

struct smb_basic_signing_context {
	DATA_BLOB mac_key;
	uint32 next_seq_num;
};

/***********************************************************
 SMB signing - Common code before we set a new signing implementation
************************************************************/
static BOOL set_smb_signing_common(struct cli_transport *transport)
{
	if (!(transport->negotiate.sec_mode & 
	      (NEGOTIATE_SECURITY_SIGNATURES_REQUIRED|NEGOTIATE_SECURITY_SIGNATURES_ENABLED))) {
		return False;
	}

	if (transport->negotiate.sign_info.doing_signing) {
		return False;
	}
	
	if (transport->negotiate.sign_info.free_signing_context)
		transport->negotiate.sign_info.free_signing_context(transport);

	/* These calls are INCOMPATIBLE with SMB signing */
	transport->negotiate.readbraw_supported = False;
	transport->negotiate.writebraw_supported = False;
	
	return True;
}

/***********************************************************
 SMB signing - Common code for 'real' implementations
************************************************************/
static BOOL set_smb_signing_real_common(struct cli_transport *transport) 
{
	if (transport->negotiate.sec_mode & NEGOTIATE_SECURITY_SIGNATURES_REQUIRED) {
		DEBUG(5, ("Mandatory SMB signing enabled!\n"));
		transport->negotiate.sign_info.doing_signing = True;
	}

	DEBUG(5, ("SMB signing enabled!\n"));

	return True;
}

static void mark_packet_signed(struct cli_request *req) 
{
	uint16 flags2;
	flags2 = SVAL(req->out.hdr, HDR_FLG2);
	flags2 |= FLAGS2_SMB_SECURITY_SIGNATURES;
	SSVAL(req->out.hdr, HDR_FLG2, flags2);
}

static BOOL signing_good(struct cli_request *req, BOOL good) 
{
	if (good && !req->transport->negotiate.sign_info.doing_signing) {
		req->transport->negotiate.sign_info.doing_signing = True;
	}

	if (!good) {
		if (req->transport->negotiate.sign_info.doing_signing) {
			DEBUG(1, ("SMB signature check failed!\n"));
			return False;
		} else {
			DEBUG(3, ("Server did not sign reply correctly\n"));
			cli_transport_free_signing_context(req->transport);
			return False;
		}
	}
	return True;
}	

/***********************************************************
 SMB signing - Simple implementation - calculate a MAC to send.
************************************************************/
static void cli_request_simple_sign_outgoing_message(struct cli_request *req)
{
	unsigned char calc_md5_mac[16];
	struct MD5Context md5_ctx;
	struct smb_basic_signing_context *data = req->transport->negotiate.sign_info.signing_context;

#if 0
	/* enable this when packet signing is preventing you working out why valgrind 
	   says that data is uninitialised */
	file_save("pkt.dat", req->out.buffer, req->out.size);
#endif

	req->seq_num = data->next_seq_num;
	
	/* some requests (eg. NTcancel) are one way, and the sequence number
	   should be increased by 1 not 2 */
	if (req->one_way_request) {
		data->next_seq_num += 1;
	} else {
		data->next_seq_num += 2;
	}

	/*
	 * Firstly put the sequence number into the first 4 bytes.
	 * and zero out the next 4 bytes.
	 */
	SIVAL(req->out.hdr, HDR_SS_FIELD, req->seq_num);
	SIVAL(req->out.hdr, HDR_SS_FIELD + 4, 0);

	/* mark the packet as signed - BEFORE we sign it...*/
	mark_packet_signed(req);

	/* Calculate the 16 byte MAC and place first 8 bytes into the field. */
	MD5Init(&md5_ctx);
	MD5Update(&md5_ctx, data->mac_key.data, 
		  data->mac_key.length); 
	MD5Update(&md5_ctx, 
		  req->out.buffer + NBT_HDR_SIZE, 
		  req->out.size - NBT_HDR_SIZE);
	MD5Final(calc_md5_mac, &md5_ctx);

	memcpy(&req->out.hdr[HDR_SS_FIELD], calc_md5_mac, 8);

/*	req->out.hdr[HDR_SS_FIELD+2]=0; 
	Uncomment this to test if the remote server actually verifies signitures...*/
}


/***********************************************************
 SMB signing - Simple implementation - check a MAC sent by server.
************************************************************/
static BOOL cli_request_simple_check_incoming_message(struct cli_request *req)
{
	BOOL good;
	unsigned char calc_md5_mac[16];
	unsigned char server_sent_mac[8];
	unsigned char sequence_buf[8];
	struct MD5Context md5_ctx;
	struct smb_basic_signing_context *data = req->transport->negotiate.sign_info.signing_context;
	const size_t offset_end_of_sig = (HDR_SS_FIELD + 8);
	int i;
	const int sign_range = 0;

	/* its quite bogus to be guessing sequence numbers, but very useful
	   when debugging signing implementations */
	for (i = 1-sign_range; i <= 1+sign_range; i++) {
		/*
		 * Firstly put the sequence number into the first 4 bytes.
		 * and zero out the next 4 bytes.
		 */
		SIVAL(sequence_buf, 0, req->seq_num+i);
		SIVAL(sequence_buf, 4, 0);
		
		/* get a copy of the server-sent mac */
		memcpy(server_sent_mac, &req->in.hdr[HDR_SS_FIELD], sizeof(server_sent_mac));
		
		/* Calculate the 16 byte MAC and place first 8 bytes into the field. */
		MD5Init(&md5_ctx);
		MD5Update(&md5_ctx, data->mac_key.data, 
			  data->mac_key.length); 
		MD5Update(&md5_ctx, req->in.hdr, HDR_SS_FIELD);
		MD5Update(&md5_ctx, sequence_buf, sizeof(sequence_buf));
		
		MD5Update(&md5_ctx, req->in.hdr + offset_end_of_sig, 
			  req->in.size - NBT_HDR_SIZE - (offset_end_of_sig));
		MD5Final(calc_md5_mac, &md5_ctx);
		
		good = (memcmp(server_sent_mac, calc_md5_mac, 8) == 0);
		if (good) break;
	}

	if (good && i != 1) {
		DEBUG(0,("SIGNING OFFSET %d\n", i));
	}

	if (!good) {
		DEBUG(5, ("cli_request_simple_check_incoming_message: BAD SIG: wanted SMB signature of\n"));
		dump_data(5, calc_md5_mac, 8);
		
		DEBUG(5, ("cli_request_simple_check_incoming_message: BAD SIG: got SMB signature of\n"));
		dump_data(5, server_sent_mac, 8);
	}
	return signing_good(req, good);
}


/***********************************************************
 SMB signing - Simple implementation - free signing context
************************************************************/
static void cli_transport_simple_free_signing_context(struct cli_transport *transport)
{
	struct smb_basic_signing_context *data = transport->negotiate.sign_info.signing_context;

	data_blob_free(&data->mac_key);
	SAFE_FREE(transport->negotiate.sign_info.signing_context);

	return;
}


/***********************************************************
 SMB signing - Simple implementation - setup the MAC key.
************************************************************/
BOOL cli_transport_simple_set_signing(struct cli_transport *transport,
				      const uchar user_transport_key[16], const DATA_BLOB response)
{
	struct smb_basic_signing_context *data;

	if (!set_smb_signing_common(transport)) {
		return False;
	}

	if (!set_smb_signing_real_common(transport)) {
		return False;
	}

	data = smb_xmalloc(sizeof(*data));
	transport->negotiate.sign_info.signing_context = data;
	
	data->mac_key = data_blob(NULL, MIN(response.length + 16, 40));

	memcpy(&data->mac_key.data[0], user_transport_key, 16);
	memcpy(&data->mac_key.data[16],response.data, MIN(response.length, 40 - 16));

	/* Initialise the sequence number */
	data->next_seq_num = 0;

	transport->negotiate.sign_info.sign_outgoing_message = cli_request_simple_sign_outgoing_message;
	transport->negotiate.sign_info.check_incoming_message = cli_request_simple_check_incoming_message;
	transport->negotiate.sign_info.free_signing_context = cli_transport_simple_free_signing_context;

	return True;
}


/***********************************************************
 SMB signing - NULL implementation - calculate a MAC to send.
************************************************************/
static void cli_request_null_sign_outgoing_message(struct cli_request *req)
{
	/* we can't zero out the sig, as we might be trying to send a
	   transport request - which is NBT-level, not SMB level and doesn't
	   have the field */
}


/***********************************************************
 SMB signing - NULL implementation - check a MAC sent by server.
************************************************************/
static BOOL cli_request_null_check_incoming_message(struct cli_request *req)
{
	return True;
}


/***********************************************************
 SMB signing - NULL implementation - free signing context
************************************************************/
static void cli_null_free_signing_context(struct cli_transport *transport)
{
}

/**
 SMB signing - NULL implementation - setup the MAC key.

 @note Used as an initialisation only - it will not correctly
       shut down a real signing mechanism
*/
BOOL cli_null_set_signing(struct cli_transport *transport)
{
	transport->negotiate.sign_info.signing_context = NULL;
	
	transport->negotiate.sign_info.sign_outgoing_message = cli_request_null_sign_outgoing_message;
	transport->negotiate.sign_info.check_incoming_message = cli_request_null_check_incoming_message;
	transport->negotiate.sign_info.free_signing_context = cli_null_free_signing_context;

	return True;
}


/**
 * Free the signing context
 */
void cli_transport_free_signing_context(struct cli_transport *transport) 
{
	if (transport->negotiate.sign_info.free_signing_context) {
		transport->negotiate.sign_info.free_signing_context(transport);
	}

	cli_null_set_signing(transport);
}


/**
 * Sign a packet with the current mechanism
 */
void cli_request_calculate_sign_mac(struct cli_request *req)
{
	req->transport->negotiate.sign_info.sign_outgoing_message(req);
}


/**
 * Check a packet with the current mechanism
 * @return False if we had an established signing connection
 *         which had a back checksum, True otherwise
 */
BOOL cli_request_check_sign_mac(struct cli_request *req) 
{
	BOOL good;

	if (req->in.size < (HDR_SS_FIELD + 8)) {
		good = False;
	} else {
		good = req->transport->negotiate.sign_info.check_incoming_message(req);
	}

	if (!good && req->transport->negotiate.sign_info.doing_signing) {
		return False;
	}

	return True;
}
