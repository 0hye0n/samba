/* 
   Unix SMB/CIFS implementation.
   handle SMBsessionsetup
   Copyright (C) Andrew Tridgell 1998-2001
   Copyright (C) Andrew Bartlett      2001

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

uint32 global_client_caps = 0;
static struct auth_context *ntlmssp_auth_context = NULL;

/*
  on a logon error possibly map the error to success if "map to guest"
  is set approriately
*/
static NTSTATUS do_map_to_guest(NTSTATUS status, auth_serversupplied_info **server_info,
				const char *user, const char *domain)
{
	if (NT_STATUS_EQUAL(status, NT_STATUS_NO_SUCH_USER)) {
		if ((lp_map_to_guest() == MAP_TO_GUEST_ON_BAD_USER) || 
		    (lp_map_to_guest() == MAP_TO_GUEST_ON_BAD_PASSWORD)) {
			DEBUG(3,("No such user %s [%s] - using guest account\n",
				 user, domain));
			make_server_info_guest(server_info);
			status = NT_STATUS_OK;
		}
	}

	if (NT_STATUS_EQUAL(status, NT_STATUS_WRONG_PASSWORD)) {
		if (lp_map_to_guest() == MAP_TO_GUEST_ON_BAD_PASSWORD) {
			DEBUG(3,("Registered username %s for guest access\n",user));
			make_server_info_guest(server_info);
			status = NT_STATUS_OK;
		}
	}

	return status;
}


/****************************************************************************
 Add the standard 'Samba' signature to the end of the session setup.
****************************************************************************/
static void add_signature(char *outbuf) 
{
	char *p;
	p = smb_buf(outbuf);
	p += srvstr_push(outbuf, p, "Unix", -1, STR_TERMINATE);
	p += srvstr_push(outbuf, p, "Samba", -1, STR_TERMINATE);
	p += srvstr_push(outbuf, p, lp_workgroup(), -1, STR_TERMINATE);
	set_message_end(outbuf,p);
}

/****************************************************************************
 Do a 'guest' logon, getting back the 
****************************************************************************/
static NTSTATUS check_guest_password(auth_serversupplied_info **server_info) 
{
	struct auth_context *auth_context;
	auth_usersupplied_info *user_info = NULL;
	
	NTSTATUS nt_status;
	unsigned char chal[8];

	ZERO_STRUCT(chal);

	DEBUG(3,("Got anonymous request\n"));

	if (!NT_STATUS_IS_OK(nt_status = make_auth_context_fixed(&auth_context, chal))) {
		return nt_status;
	}

	if (!make_user_info_guest(&user_info)) {
		(auth_context->free)(&auth_context);
		return NT_STATUS_NO_MEMORY;
	}
	
	nt_status = auth_context->check_ntlm_password(auth_context, user_info, server_info);
	(auth_context->free)(&auth_context);
	free_user_info(&user_info);
	return nt_status;
}


#ifdef HAVE_KRB5
/****************************************************************************
reply to a session setup spnego negotiate packet for kerberos
****************************************************************************/
static int reply_spnego_kerberos(connection_struct *conn, 
				 char *inbuf, char *outbuf,
				 int length, int bufsize,
				 DATA_BLOB *secblob)
{
	DATA_BLOB ticket;
	char *client, *p;
	const struct passwd *pw;
	char *user;
	int sess_vuid;
	NTSTATUS ret;
	DATA_BLOB auth_data;
	auth_serversupplied_info *server_info = NULL;
	ADS_STRUCT *ads;

	if (!spnego_parse_krb5_wrap(*secblob, &ticket)) {
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	ads = ads_init_simple();

	ret = ads_verify_ticket(ads, &ticket, &client, &auth_data);
	if (!NT_STATUS_IS_OK(ret)) {
		DEBUG(1,("Failed to verify incoming ticket!\n"));	
		ads_destroy(&ads);
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	DEBUG(3,("Ticket name is [%s]\n", client));

	p = strchr_m(client, '@');
	if (!p) {
		DEBUG(3,("Doesn't look like a valid principal\n"));
		ads_destroy(&ads);
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	*p = 0;
	if (strcasecmp(p+1, ads->realm) != 0) {
		DEBUG(3,("Ticket for foreign realm %s@%s\n", client, p+1));
		if (!lp_allow_trusted_domains()) {
			return ERROR_NT(NT_STATUS_LOGON_FAILURE);
		}
		/* this gives a fully qualified user name (ie. with full realm).
		   that leads to very long usernames, but what else can we do? */
		asprintf(&user, "%s%s%s", p+1, lp_winbind_separator(), client);
	} else {
		user = strdup(client);
	}
	ads_destroy(&ads);

	/* the password is good - let them in */
	pw = smb_getpwnam(user,False);
	if (!pw && !strstr(user, lp_winbind_separator())) {
		char *user2;
		/* try it with a winbind domain prefix */
		asprintf(&user2, "%s%s%s", lp_workgroup(), lp_winbind_separator(), user);
		pw = smb_getpwnam(user2,False);
		if (pw) {
			free(user);
			user = user2;
		}
	}

	if (!pw) {
		DEBUG(1,("Username %s is invalid on this system\n",user));
		return ERROR_NT(NT_STATUS_NO_SUCH_USER);
	}

	if (!make_server_info_pw(&server_info,pw)) {
		DEBUG(1,("make_server_info_from_pw failed!\n"));
		return ERROR_NT(NT_STATUS_NO_MEMORY);
	}
	
	sess_vuid = register_vuid(server_info, user);

	free(user);
	free_server_info(&server_info);

	if (sess_vuid == -1) {
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	set_message(outbuf,4,0,True);
	SSVAL(outbuf, smb_vwv3, 0);
	add_signature(outbuf);
 
	SSVAL(outbuf,smb_uid,sess_vuid);
	SSVAL(inbuf,smb_uid,sess_vuid);
	
	return chain_reply(inbuf,outbuf,length,bufsize);
}
#endif


/****************************************************************************
send a security blob via a session setup reply
****************************************************************************/
static BOOL reply_sesssetup_blob(connection_struct *conn, char *outbuf,
				 DATA_BLOB blob)
{
	char *p;

	set_message(outbuf,4,0,True);

	/* we set NT_STATUS_MORE_PROCESSING_REQUIRED to tell the other end
	   that we aren't finished yet */

	SIVAL(outbuf, smb_rcls, NT_STATUS_V(NT_STATUS_MORE_PROCESSING_REQUIRED));
	SSVAL(outbuf, smb_vwv0, 0xFF); /* no chaining possible */
	SSVAL(outbuf, smb_vwv3, blob.length);
	p = smb_buf(outbuf);
	memcpy(p, blob.data, blob.length);
	p += blob.length;
	p += srvstr_push(outbuf, p, "Unix", -1, STR_TERMINATE);
	p += srvstr_push(outbuf, p, "Samba", -1, STR_TERMINATE);
	p += srvstr_push(outbuf, p, lp_workgroup(), -1, STR_TERMINATE);
	set_message_end(outbuf,p);
	
	return send_smb(smbd_server_fd(),outbuf);
}

/****************************************************************************
reply to a session setup spnego negotiate packet
****************************************************************************/
static int reply_spnego_negotiate(connection_struct *conn, 
				  char *inbuf,
				  char *outbuf,
				  int length, int bufsize,
				  DATA_BLOB blob1)
{
	char *OIDs[ASN1_MAX_OIDS];
	DATA_BLOB secblob;
	int i;
	uint32 ntlmssp_command, neg_flags, chal_flags;
	DATA_BLOB chal, spnego_chal, extra_data;
	const uint8 *cryptkey;
	BOOL got_kerberos = False;
	NTSTATUS nt_status;
	extern pstring global_myname;

	/* parse out the OIDs and the first sec blob */
	if (!parse_negTokenTarg(blob1, OIDs, &secblob)) {
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}
	
	for (i=0;OIDs[i];i++) {
		DEBUG(3,("Got OID %s\n", OIDs[i]));
		if (strcmp(OID_KERBEROS5, OIDs[i]) == 0 ||
		    strcmp(OID_KERBEROS5_OLD, OIDs[i]) == 0) {
			got_kerberos = True;
		}
		free(OIDs[i]);
	}
	DEBUG(3,("Got secblob of size %d\n", secblob.length));

#ifdef HAVE_KRB5
	if (got_kerberos) {
		int ret = reply_spnego_kerberos(conn, inbuf, outbuf, 
						length, bufsize, &secblob);
		data_blob_free(&secblob);
		return ret;
	}
#endif

	/* parse the NTLMSSP packet */
#if 0
	file_save("secblob.dat", secblob.data, secblob.length);
#endif

	if (!msrpc_parse(&secblob, "CddB",
			 "NTLMSSP",
			 &ntlmssp_command,
			 &neg_flags,
			 &extra_data)) {
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}
       
	DEBUG(5, ("Extra data: \n"));
	dump_data(5, extra_data.data, extra_data.length);

	data_blob_free(&secblob);
	data_blob_free(&extra_data);

	if (ntlmssp_command != NTLMSSP_NEGOTIATE) {
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	DEBUG(3,("Got neg_flags=0x%08x\n", neg_flags));

	debug_ntlmssp_flags(neg_flags);

	if (ntlmssp_auth_context) {
		(ntlmssp_auth_context->free)(&ntlmssp_auth_context);
	}

	if (!NT_STATUS_IS_OK(nt_status = make_auth_context_subsystem(&ntlmssp_auth_context))) {
		return ERROR_NT(nt_status);
	}

	cryptkey = ntlmssp_auth_context->get_ntlm_challenge(ntlmssp_auth_context);

	/* Give them the challenge. For now, ignore neg_flags and just
	   return the flags we want. Obviously this is not correct */
	
	chal_flags = NTLMSSP_NEGOTIATE_UNICODE | 
		NTLMSSP_NEGOTIATE_LM_KEY | 
		NTLMSSP_NEGOTIATE_NTLM |
		NTLMSSP_CHAL_TARGET_INFO;
	
	{
		DATA_BLOB domain_blob, netbios_blob, realm_blob;
		
		msrpc_gen(&domain_blob, 
			  "U",
			  lp_workgroup());

		msrpc_gen(&netbios_blob, 
			  "U",
			  global_myname);
		
		msrpc_gen(&realm_blob, 
			  "U",
			  lp_realm());
		

		msrpc_gen(&chal, "CddddbBBBB",
			  "NTLMSSP", 
			  NTLMSSP_CHALLENGE,
			  0,
			  0x30, /* ?? */
			  chal_flags,
			  cryptkey, 8,
			  domain_blob.data, domain_blob.length,
			  domain_blob.data, domain_blob.length,
			  netbios_blob.data, netbios_blob.length,
			  realm_blob.data, realm_blob.length);

		data_blob_free(&domain_blob);
		data_blob_free(&netbios_blob);
		data_blob_free(&realm_blob);
	}

	if (!spnego_gen_challenge(&spnego_chal, &chal, &chal)) {
		DEBUG(3,("Failed to generate challenge\n"));
		data_blob_free(&chal);
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	/* now tell the client to send the auth packet */
	reply_sesssetup_blob(conn, outbuf, spnego_chal);

	data_blob_free(&chal);
	data_blob_free(&spnego_chal);

	/* and tell smbd that we have already replied to this packet */
	return -1;
}

	
/****************************************************************************
reply to a session setup spnego auth packet
****************************************************************************/
static int reply_spnego_auth(connection_struct *conn, char *inbuf, char *outbuf,
			     int length, int bufsize,
			     DATA_BLOB blob1)
{
	DATA_BLOB auth;
	char *workgroup = NULL, *user = NULL, *machine = NULL;
	DATA_BLOB lmhash, nthash, sess_key;
	DATA_BLOB plaintext_password = data_blob(NULL, 0);
	uint32 ntlmssp_command, neg_flags;
	NTSTATUS nt_status;
	int sess_vuid;
	BOOL as_guest;
	uint32 auth_flags = AUTH_FLAG_NONE;
	auth_usersupplied_info *user_info = NULL;
	auth_serversupplied_info *server_info = NULL;

	/* we must have setup the auth context by now */
	if (!ntlmssp_auth_context) {
		DEBUG(2,("ntlmssp_auth_context is NULL in reply_spnego_auth\n"));
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	if (!spnego_parse_auth(blob1, &auth)) {
#if 0
		file_save("auth.dat", blob1.data, blob1.length);
#endif
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	/* now the NTLMSSP encoded auth hashes */
	if (!msrpc_parse(&auth, "CdBBUUUBd", 
			 "NTLMSSP", 
			 &ntlmssp_command, 
			 &lmhash,
			 &nthash,
			 &workgroup, 
			 &user, 
			 &machine,
			 &sess_key,
			 &neg_flags)) {
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	data_blob_free(&auth);
	data_blob_free(&sess_key);
	
	DEBUG(3,("Got user=[%s] workgroup=[%s] machine=[%s] len1=%d len2=%d\n",
		 user, workgroup, machine, lmhash.length, nthash.length));

#if 0
	file_save("nthash1.dat", nthash.data, nthash.length);
	file_save("lmhash1.dat", lmhash.data, lmhash.length);
#endif

	if (lmhash.length) {
		auth_flags |= AUTH_FLAG_LM_RESP;
	}

	if (nthash.length == 24) {
		auth_flags |= AUTH_FLAG_NTLM_RESP;
	} else if (nthash.length > 24) {
		auth_flags |= AUTH_FLAG_NTLMv2_RESP;
	}

	if (!make_user_info_map(&user_info, 
				user, workgroup, 
				machine, 
				lmhash, nthash,
				plaintext_password, 
				auth_flags, True)) {
		return ERROR_NT(NT_STATUS_NO_MEMORY);
	}

	nt_status = ntlmssp_auth_context->check_ntlm_password(ntlmssp_auth_context, user_info, &server_info); 

	if (!NT_STATUS_IS_OK(nt_status)) {
		nt_status = do_map_to_guest(nt_status, &server_info, user, workgroup);
	}

	SAFE_FREE(workgroup);
	SAFE_FREE(machine);
			
	(ntlmssp_auth_context->free)(&ntlmssp_auth_context);

	free_user_info(&user_info);
	
	data_blob_free(&lmhash);
	
	data_blob_free(&nthash);

	if (!NT_STATUS_IS_OK(nt_status)) {
		SAFE_FREE(user);
		return ERROR_NT(nt_status_squash(nt_status));
	}

	as_guest = server_info->guest;

	sess_vuid = register_vuid(server_info, user);
	free_server_info(&server_info);

	SAFE_FREE(user);
  
	if (sess_vuid == -1) {
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	set_message(outbuf,4,0,True);
	SSVAL(outbuf, smb_vwv3, 0);

	if (as_guest) {
		SSVAL(outbuf,smb_vwv2,1);
	}

	add_signature(outbuf);
 
	SSVAL(outbuf,smb_uid,sess_vuid);
	SSVAL(inbuf,smb_uid,sess_vuid);
	
	return chain_reply(inbuf,outbuf,length,bufsize);
}


/****************************************************************************
reply to a session setup spnego anonymous packet
****************************************************************************/
static int reply_spnego_anonymous(connection_struct *conn, char *inbuf, char *outbuf,
				  int length, int bufsize)
{
	int sess_vuid;
	auth_serversupplied_info *server_info = NULL;
	NTSTATUS nt_status;

	nt_status = check_guest_password(&server_info);

	if (!NT_STATUS_IS_OK(nt_status)) {
		return ERROR_NT(nt_status_squash(nt_status));
	}

	sess_vuid = register_vuid(server_info, lp_guestaccount());

	free_server_info(&server_info);
  
	if (sess_vuid == -1) {
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	set_message(outbuf,4,0,True);
	SSVAL(outbuf, smb_vwv3, 0);
	add_signature(outbuf);
 
	SSVAL(outbuf,smb_uid,sess_vuid);
	SSVAL(inbuf,smb_uid,sess_vuid);
	
	return chain_reply(inbuf,outbuf,length,bufsize);
}


/****************************************************************************
reply to a session setup command
****************************************************************************/
static int reply_sesssetup_and_X_spnego(connection_struct *conn, char *inbuf,char *outbuf,
					int length,int bufsize)
{
	uint8 *p;
	DATA_BLOB blob1;
	int ret;

	DEBUG(3,("Doing spnego session setup\n"));

	if (global_client_caps == 0) {
		global_client_caps = IVAL(inbuf,smb_vwv10);
	}
		
	p = (uint8 *)smb_buf(inbuf);

	if (SVAL(inbuf, smb_vwv7) == 0) {
		/* an anonymous request */
		return reply_spnego_anonymous(conn, inbuf, outbuf, length, bufsize);
	}

	/* pull the spnego blob */
	blob1 = data_blob(p, SVAL(inbuf, smb_vwv7));

#if 0
	file_save("negotiate.dat", blob1.data, blob1.length);
#endif

	if (blob1.data[0] == ASN1_APPLICATION(0)) {
		/* its a negTokenTarg packet */
		ret = reply_spnego_negotiate(conn, inbuf, outbuf, length, bufsize, blob1);
		data_blob_free(&blob1);
		return ret;
	}

	if (blob1.data[0] == ASN1_CONTEXT(1)) {
		/* its a auth packet */
		ret = reply_spnego_auth(conn, inbuf, outbuf, length, bufsize, blob1);
		data_blob_free(&blob1);
		return ret;
	}

	/* what sort of packet is this? */
	DEBUG(1,("Unknown packet in reply_sesssetup_and_X_spnego\n"));

	data_blob_free(&blob1);

	return ERROR_NT(NT_STATUS_LOGON_FAILURE);
}


/****************************************************************************
reply to a session setup command
****************************************************************************/
int reply_sesssetup_and_X(connection_struct *conn, char *inbuf,char *outbuf,
			  int length,int bufsize)
{
	int sess_vuid;
	int   smb_bufsize;    
	DATA_BLOB lm_resp;
	DATA_BLOB nt_resp;
	DATA_BLOB plaintext_password;
	pstring user;
	pstring sub_user; /* Sainitised username for substituion */
	fstring domain;
	fstring native_os;
	fstring native_lanman;
	static BOOL done_sesssetup = False;
	extern BOOL global_encrypted_passwords_negotiated;
	extern BOOL global_spnego_negotiated;
	extern int Protocol;
	extern fstring remote_machine;
	extern userdom_struct current_user_info;
	extern int max_send;

	auth_usersupplied_info *user_info = NULL;
	extern struct auth_context *negprot_global_auth_context;
	auth_serversupplied_info *server_info = NULL;

	NTSTATUS nt_status;

	BOOL doencrypt = global_encrypted_passwords_negotiated;

	START_PROFILE(SMBsesssetupX);

	ZERO_STRUCT(lm_resp);
	ZERO_STRUCT(nt_resp);
	ZERO_STRUCT(plaintext_password);

	DEBUG(3,("wct=%d flg2=0x%x\n", CVAL(inbuf, smb_wct), SVAL(inbuf, smb_flg2)));

	/* a SPNEGO session setup has 12 command words, whereas a normal
	   NT1 session setup has 13. See the cifs spec. */
	if (CVAL(inbuf, smb_wct) == 12 &&
	    (SVAL(inbuf, smb_flg2) & FLAGS2_EXTENDED_SECURITY)) {
		if (!global_spnego_negotiated) {
			DEBUG(0,("reply_sesssetup_and_X:  Rejecting attempt at SPNEGO session setup when it was not negoitiated.\n"));
			return ERROR_NT(NT_STATUS_UNSUCCESSFUL);
		}

		return reply_sesssetup_and_X_spnego(conn, inbuf, outbuf, length, bufsize);
	}

	smb_bufsize = SVAL(inbuf,smb_vwv2);

	if (Protocol < PROTOCOL_NT1) {
		uint16 passlen1 = SVAL(inbuf,smb_vwv7);
		if (passlen1 > MAX_PASS_LEN) {
			return ERROR_DOS(ERRDOS,ERRbuftoosmall);
		}

		if (doencrypt) {
			lm_resp = data_blob(smb_buf(inbuf), passlen1);
		} else {
			plaintext_password = data_blob(smb_buf(inbuf), passlen1+1);
			/* Ensure null termination */
			plaintext_password.data[passlen1] = 0;
		}

		srvstr_pull_buf(inbuf, user, smb_buf(inbuf)+passlen1, sizeof(user), STR_TERMINATE);
		*domain = 0;
  
	} else {
		uint16 passlen1 = SVAL(inbuf,smb_vwv7);
		uint16 passlen2 = SVAL(inbuf,smb_vwv8);
		enum remote_arch_types ra_type = get_remote_arch();
		char *p = smb_buf(inbuf);    

		if(global_client_caps == 0)
			global_client_caps = IVAL(inbuf,smb_vwv11);
		
		/* client_caps is used as final determination if client is NT or Win95. 
		   This is needed to return the correct error codes in some
		   circumstances.
		*/
		
		if(ra_type == RA_WINNT || ra_type == RA_WIN2K || ra_type == RA_WIN95) {
			if(!(global_client_caps & (CAP_NT_SMBS | CAP_STATUS32))) {
				set_remote_arch( RA_WIN95);
			}
		}
		
		if (passlen1 > MAX_PASS_LEN) {
			return ERROR_DOS(ERRDOS,ERRbuftoosmall);
		}

		passlen1 = MIN(passlen1, MAX_PASS_LEN);
		passlen2 = MIN(passlen2, MAX_PASS_LEN);

		if (!doencrypt) {
			/* both Win95 and WinNT stuff up the password lengths for
			   non-encrypting systems. Uggh. 
			   
			   if passlen1==24 its a win95 system, and its setting the
			   password length incorrectly. Luckily it still works with the
			   default code because Win95 will null terminate the password
			   anyway 
			   
			   if passlen1>0 and passlen2>0 then maybe its a NT box and its
			   setting passlen2 to some random value which really stuffs
			   things up. we need to fix that one.  */
			
			if (passlen1 > 0 && passlen2 > 0 && passlen2 != 24 && passlen2 != 1)
				passlen2 = 0;
		}
		
		/* Save the lanman2 password and the NT md4 password. */
		
		if ((doencrypt) && (passlen1 != 0) && (passlen1 != 24)) {
			doencrypt = False;
		}
		
		if (doencrypt) {
			lm_resp = data_blob(p, passlen1);
			nt_resp = data_blob(p+passlen1, passlen2);
		} else {
			plaintext_password = data_blob(p, passlen1+1);
			/* Ensure null termination */
			plaintext_password.data[passlen1] = 0;
		}
		
		p += passlen1 + passlen2;
		p += srvstr_pull_buf(inbuf, user, p, sizeof(user), STR_TERMINATE);
		p += srvstr_pull_buf(inbuf, domain, p, sizeof(domain), STR_TERMINATE);
		p += srvstr_pull_buf(inbuf, native_os, p, sizeof(native_os), STR_TERMINATE);
		p += srvstr_pull_buf(inbuf, native_lanman, p, sizeof(native_lanman), STR_TERMINATE);
		DEBUG(3,("Domain=[%s]  NativeOS=[%s] NativeLanMan=[%s]\n",
			 domain,native_os,native_lanman));
	}
	
	/* don't allow for weird usernames or domains */
	alpha_strcpy(user, user, ". _-$", sizeof(user));
	alpha_strcpy(domain, domain, ". _-", sizeof(domain));
	if (strstr(user, "..") || strstr(domain,"..")) {
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

	DEBUG(3,("sesssetupX:name=[%s]\\[%s]@[%s]\n", domain, user, remote_machine));

	if (*user) {
		if (global_spnego_negotiated) {
			
			/* This has to be here, becouse this is a perfectly valid behaviour for guest logons :-( */
			
			DEBUG(0,("reply_sesssetup_and_X:  Rejecting attempt at 'normal' session setup after negotiating spnego.\n"));
			return ERROR_NT(NT_STATUS_UNSUCCESSFUL);
		}
		pstrcpy(sub_user, user);
	} else {
		pstrcpy(sub_user, lp_guestaccount());
	}

	pstrcpy(current_user_info.smb_name,sub_user);

	reload_services(True);
	
	if (lp_security() == SEC_SHARE) {
		/* in share level we should ignore any passwords */

		data_blob_free(&lm_resp);
		data_blob_free(&nt_resp);
		data_blob_clear_free(&plaintext_password);

		map_username(sub_user);
		add_session_user(sub_user);
		/* Then force it to null for the benfit of the code below */
		*user = 0;
	}
	
	if (!*user) {

		nt_status = check_guest_password(&server_info);

	} else if (doencrypt) {
		if (!make_user_info_for_reply_enc(&user_info, 
						  user, domain, 
						  lm_resp, nt_resp)) {
			nt_status = NT_STATUS_NO_MEMORY;
		} else {
			nt_status = negprot_global_auth_context->check_ntlm_password(negprot_global_auth_context, 
										     user_info, 
										     &server_info);
		}
	} else {
		struct auth_context *plaintext_auth_context = NULL;
		const uint8 *chal;
		if (NT_STATUS_IS_OK(nt_status = make_auth_context_subsystem(&plaintext_auth_context))) {
			chal = plaintext_auth_context->get_ntlm_challenge(plaintext_auth_context);
			
			if (!make_user_info_for_reply(&user_info, 
						      user, domain, chal,
						      plaintext_password)) {
				nt_status = NT_STATUS_NO_MEMORY;
			}
		
			if (NT_STATUS_IS_OK(nt_status)) {
				nt_status = plaintext_auth_context->check_ntlm_password(plaintext_auth_context, 
											user_info, 
											&server_info); 
				
				(plaintext_auth_context->free)(&plaintext_auth_context);
			}
		}
	}

	free_user_info(&user_info);
	
	data_blob_free(&lm_resp);
	data_blob_free(&nt_resp);
	data_blob_clear_free(&plaintext_password);
	
	if (!NT_STATUS_IS_OK(nt_status)) {
		nt_status = do_map_to_guest(nt_status, &server_info, user, domain);
	}
	
	if (!NT_STATUS_IS_OK(nt_status)) {
		return ERROR_NT(nt_status_squash(nt_status));
	}
	
	/* it's ok - setup a reply */
	if (Protocol < PROTOCOL_NT1) {
		set_message(outbuf,3,0,True);
	} else {
		set_message(outbuf,3,0,True);
		add_signature(outbuf);
		/* perhaps grab OS version here?? */
	}
	
	if (server_info->guest) {
		SSVAL(outbuf,smb_vwv2,1);
	}

	/* register the name and uid as being validated, so further connections
	   to a uid can get through without a password, on the same VC */

	sess_vuid = register_vuid(server_info, sub_user);

	free_server_info(&server_info);
  
	if (sess_vuid == -1) {
		return ERROR_NT(NT_STATUS_LOGON_FAILURE);
	}

 
	SSVAL(outbuf,smb_uid,sess_vuid);
	SSVAL(inbuf,smb_uid,sess_vuid);
	
	if (!done_sesssetup)
		max_send = MIN(max_send,smb_bufsize);
	
	done_sesssetup = True;
	
	END_PROFILE(SMBsesssetupX);
	return chain_reply(inbuf,outbuf,length,bufsize);
}
