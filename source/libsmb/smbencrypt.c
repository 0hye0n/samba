/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   SMB parameters and setup
   Copyright (C) Andrew Tridgell 1992-1998
   Modified by Jeremy Allison 1995.
   
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

extern int DEBUGLEVEL;

#include "byteorder.h"

/*
   This implements the X/Open SMB password encryption
   It takes a password, a 8 byte "crypt key" and puts 24 bytes of 
   encrypted password into p24 */
void SMBencrypt(uchar *passwd, uchar *c8, uchar *p24)
{
	uchar p21[21];

	lm_owf_gen(passwd, p21);
	SMBOWFencrypt(p21, c8, p24);

#ifdef DEBUG_PASSWORD
	DEBUG(100,("SMBencrypt: lm#, challenge, response\n"));
	dump_data(100, p21, 16);
	dump_data(100, c8, 8);
	dump_data(100, p24, 24);
#endif
}

void SMBNTencrypt(uchar *passwd, uchar *c8, uchar *p24)
{
	uchar p21[21];
 
	memset(p21,'\0',21);
 
	nt_owf_gen(passwd, p21);    
	SMBOWFencrypt(p21, c8, p24);

#ifdef DEBUG_PASSWORD
	DEBUG(100,("SMBNTencrypt: nt#, challenge, response\n"));
	dump_data(100, p21, 16);
	dump_data(100, c8, 8);
	dump_data(100, p24, 24);
#endif
}

/* Routines for Windows NT MD4 Hash functions. */
static int _my_wcslen(int16 *str)
{
	int len = 0;
	while(*str++ != 0)
		len++;
	return len;
}

/*
 * Convert a string into an NT UNICODE string.
 * Note that regardless of processor type 
 * this must be in intel (little-endian)
 * format.
 */
 
static int _my_mbstowcsupper(int16 *dst, const uchar *src, int len)
{
	int i;
	int16 val;
 
	for(i = 0; i < len; i++) {
		val = toupper(*src);
		SSVAL(dst,0,val);
		dst++;
		src++;
		if(val == 0)
			break;
	}
	return i;
}

static int _my_mbstowcs(int16 *dst, const uchar *src, int len)
{
	int i;
	int16 val;
 
	for(i = 0; i < len; i++) {
		val = *src;
		SSVAL(dst,0,val);
		dst++;
		src++;
		if(val == 0)
			break;
	}
	return i;
}

/* 
 * Creates the MD4 Hash of the users password in NT UNICODE.
 */
 
void E_md4hash(uchar *passwd, uchar *p16)
{
	int len;
	int16 wpwd[129];
	
	/* Password cannot be longer than 128 characters */
	len = strlen((char *)passwd);
	if(len > 128)
		len = 128;
	/* Password must be converted to NT unicode */
	_my_mbstowcs(wpwd, passwd, len);
	wpwd[len] = 0; /* Ensure string is null terminated */
	/* Calculate length in bytes */
	len = _my_wcslen(wpwd) * sizeof(int16);

	mdfour(p16, (unsigned char *)wpwd, len);
}

/* Does the LM owf of a user's password */
void lm_owf_gen(const char *pwd, uchar p16[16])
{
	char passwd[15];

	memset(passwd,'\0',15);
	if (pwd != NULL)
	{
		safe_strcpy( passwd, pwd, sizeof(passwd)-1);
	}

	/* Mangle the passwords into Lanman format */
	passwd[14] = '\0';
	strupper(passwd);

	/* Calculate the SMB (lanman) hash functions of the password */

	memset(p16, '\0', 16);
	E_P16((uchar *) passwd, (uchar *)p16);

#ifdef DEBUG_PASSWORD
	DEBUG(100,("nt_lm_owf_gen: pwd, lm#\n"));
	dump_data(120, passwd, strlen(passwd));
	dump_data(100, p16, 16);
#endif
	/* clear out local copy of user's password (just being paranoid). */
	bzero(passwd, sizeof(passwd));
}

/* Does both the NT and LM owfs of a user's password */
void nt_owf_gen(const char *pwd, uchar nt_p16[16])
{
	char passwd[130];

	memset(passwd,'\0',130);
	if (pwd != NULL)
	{
		safe_strcpy( passwd, pwd, sizeof(passwd)-1);
	}

	/* Calculate the MD4 hash (NT compatible) of the password */
	memset(nt_p16, '\0', 16);
	E_md4hash((uchar *)passwd, nt_p16);

#ifdef DEBUG_PASSWORD
	DEBUG(100,("nt_lm_owf_gen: pwd, nt#\n"));
	dump_data(120, passwd, strlen(passwd));
	dump_data(100, nt_p16, 16);
#endif
	/* clear out local copy of user's password (just being paranoid). */
	bzero(passwd, sizeof(passwd));
}

/* Does both the NT and LM owfs of a user's password */
void nt_lm_owf_gen(const char *pwd, uchar nt_p16[16], uchar lm_p16[16])
{
	nt_owf_gen(pwd, nt_p16);
	lm_owf_gen(pwd, lm_p16);
}

/* Does the des encryption from the NT or LM MD4 hash. */
void SMBOWFencrypt(uchar passwd[16], uchar *c8, uchar p24[24])
{
	uchar p21[21];
 
	memset(p21,'\0',21);
 
	memcpy(p21, passwd, 16);    
	E_P24(p21, c8, p24);
}

void SMBOWFencrypt_ntv2(const uchar kr[16], 
				const uchar *srv_chal, int srv_chal_len,
				const uchar *cli_chal, int cli_chal_len,
				char resp_buf[16])
{
	HMACMD5Context ctx;

	hmac_md5_init_limK_to_64(kr, 16, &ctx);
	hmac_md5_update(srv_chal, srv_chal_len, &ctx);
	hmac_md5_update(cli_chal, cli_chal_len, &ctx);
	hmac_md5_final(resp_buf, &ctx);

#ifdef DEBUG_PASSWORD
	DEBUG(100,("SMBOWFencrypt_ntv2: srv_chal, cli_chal, resp_buf\n"));
	dump_data(100, srv_chal, srv_chal_len);
	dump_data(100, cli_chal, cli_chal_len);
	dump_data(100, resp_buf, 16);
#endif
}

void SMBgenclientchals(char *lm_cli_chal,
				char *nt_cli_chal, int *nt_cli_chal_len,
				const char *srv, const char *dom)
{
	NTTIME nt_time;
	int srv_len = strlen(srv);
	int dom_len = strlen(dom);
	fstring server;
	fstring domain;
	fstrcpy(server, srv);
	fstrcpy(domain, dom);
	strupper(server);
	strupper(domain);

	generate_random_buffer(lm_cli_chal, 8, False);
	unix_to_nt_time(&nt_time, time(NULL));

	CVAL(nt_cli_chal,0) = 0x1;
	CVAL(nt_cli_chal,1) = 0x1;
	SSVAL(nt_cli_chal, 2, 0x0);
	SIVAL(nt_cli_chal, 4, 0x0);
	SIVAL(nt_cli_chal, 8, nt_time.low);
	SIVAL(nt_cli_chal, 12, nt_time.high);
	memcpy(nt_cli_chal+16, lm_cli_chal, 8);
	/* fill in offset 24, size of structure, later */

	*nt_cli_chal_len = 28;

	SSVAL(nt_cli_chal, *nt_cli_chal_len, 2);
	*nt_cli_chal_len += 2;
	SSVAL(nt_cli_chal, *nt_cli_chal_len, dom_len*2);
	*nt_cli_chal_len += 2;
	ascii_to_unibuf(nt_cli_chal+(*nt_cli_chal_len), domain, dom_len*2);
	*nt_cli_chal_len += dom_len*2;
	*nt_cli_chal_len += 4 - ((*nt_cli_chal_len) % 4);

	SSVAL(nt_cli_chal, *nt_cli_chal_len, 2);
	*nt_cli_chal_len += 2;
	SSVAL(nt_cli_chal, 30, srv_len*2);
	*nt_cli_chal_len += 2;
	ascii_to_unibuf(nt_cli_chal+(*nt_cli_chal_len), server, srv_len*2);
	*nt_cli_chal_len += srv_len*2;

	SSVAL(nt_cli_chal, 24, (*nt_cli_chal_len)+16);
	SSVAL(nt_cli_chal, 26, (*nt_cli_chal_len)+15);

	DEBUG(100,("SMBgenclientchals: srv %s, dom %s\n", server, domain));
	dump_data(100, nt_cli_chal, *nt_cli_chal_len);
}

void ntv2_owf_gen(const uchar owf[16], 
				const char *user_n,
				const char *domain_n,
				uchar kr_buf[16])
{
	pstring user_u;
	pstring dom_u;
	HMACMD5Context ctx;

	int user_l   = strlen(user_n  );
	int domain_l = strlen(domain_n);

	_my_mbstowcsupper((int16*)user_u, user_n  , user_l*2  );
	_my_mbstowcsupper((int16*)dom_u , domain_n, domain_l*2);

	hmac_md5_init_limK_to_64(owf, 16, &ctx);
	hmac_md5_update(user_u, user_l*2, &ctx);
	hmac_md5_update(dom_u, domain_l*2, &ctx);
	hmac_md5_final(kr_buf, &ctx);

#ifdef DEBUG_PASSWORD
	DEBUG(100,("ntv2_owf_gen: user, domain, owfkey, kr\n"));
	dump_data(100, user_u, user_l*2);
	dump_data(100, dom_u, domain_l*2);
	dump_data(100, owf, 16);
	dump_data(100, kr_buf, 16);
#endif
}

/* Does the des encryption from the FIRST 8 BYTES of the NT or LM MD4 hash. */
void NTLMSSPOWFencrypt(uchar passwd[8], uchar *ntlmchalresp, uchar p24[24])
{
	uchar p21[21];
 
	memset(p21,'\0',21);
	memcpy(p21, passwd, 8);    
	memset(p21 + 8, 0xbd, 8);    

	E_P24(p21, ntlmchalresp, p24);
#ifdef DEBUG_PASSWORD
	DEBUG(100,("NTLMSSPOWFencrypt: p21, c8, p24\n"));
	dump_data(100, p21, 21);
	dump_data(100, ntlmchalresp, 8);
	dump_data(100, p24, 24);
#endif
}

BOOL make_oem_passwd_hash(char data[516], const char *passwd, uchar old_pw_hash[16], BOOL unicode)
{
	int new_pw_len = strlen(passwd) * (unicode ? 2 : 1);

	if (new_pw_len > 512)
	{
		DEBUG(0,("make_oem_passwd_hash: new password is too long.\n"));
		return False;
	}

	/*
	 * Now setup the data area.
	 * We need to generate a random fill
	 * for this area to make it harder to
	 * decrypt. JRA.
	 */
	generate_random_buffer((unsigned char *)data, 516, False);
	if (unicode)
	{
		ascii_to_unibuf(&data[512 - new_pw_len], passwd, new_pw_len);
	}
	else
	{
		fstrcpy( &data[512 - new_pw_len], passwd);
	}
	SIVAL(data, 512, new_pw_len);

#ifdef DEBUG_PASSWORD
	DEBUG(100,("make_oem_passwd_hash\n"));
	dump_data(100, data, 516);
#endif
	SamOEMhash( (unsigned char *)data, (unsigned char *)old_pw_hash, True);

	return True;
}

BOOL nt_decrypt_string2(STRING2 *out, const STRING2 *in, char nt_hash[16])
{
	uchar bufhdr[8];
	int datalen;

	uchar key[16];
	uchar *keyptr = key;
	uchar *keyend = key + sizeof(key);

	uchar *outbuf = (uchar *)out->buffer;
	const uchar *inbuf = (const uchar *)in->buffer;
	const uchar *inbufend;

	mdfour(key, nt_hash, 16);

	smbhash(bufhdr, inbuf, keyptr, 0);
	datalen = IVAL(bufhdr, 0);

	if ((datalen > in->str_str_len) || (datalen > MAX_STRINGLEN))
	{
		DEBUG(0, ("nt_decrypt_string2: failed\n"));
		return False;
	}

	out->str_max_len = out->str_str_len = datalen;
	inbuf += 8;
	inbufend = inbuf + datalen;

	while (inbuf < inbufend)
	{
		keyptr += 7;
		if (keyptr + 7 > keyend)
		{
			keyptr = (keyend - keyptr) + key;
		}

		smbhash(outbuf, inbuf, keyptr, 0);

		inbuf += 8;
		outbuf += 8;
	}

	return True;
}

/*******************************************************************
 creates a DCE/RPC bind authentication response

 - initialises the parse structure.
 - dynamically allocates the header data structure
 - caller is expected to free the header data structure once used.

 ********************************************************************/
void create_ntlmssp_resp(struct pwd_info *pwd,
				char *domain, char *user_name, char *my_name,
				uint32 ntlmssp_cli_flgs,
                                prs_struct *auth_resp)
{
	RPC_AUTH_NTLMSSP_RESP     ntlmssp_resp;
	unsigned char lm_owf[24];
	unsigned char nt_owf[128];
	size_t nt_owf_len;

	pwd_get_lm_nt_owf(pwd, lm_owf, nt_owf, &nt_owf_len);
			
	make_rpc_auth_ntlmssp_resp(&ntlmssp_resp,
			         lm_owf, nt_owf, nt_owf_len,
			         domain, user_name, my_name,
			         ntlmssp_cli_flgs);

	smb_io_rpc_auth_ntlmssp_resp("ntlmssp_resp", &ntlmssp_resp, auth_resp, 0);
	mem_realloc_data(auth_resp->data, auth_resp->offset);
}

/***********************************************************
 decode a password buffer
************************************************************/
BOOL decode_pw_buffer(const char buffer[516], char *new_passwd,
			int new_passwd_size, BOOL nt_pass_set)
{
	/* 
	 * The length of the new password is in the last 4 bytes of
	 * the data buffer.
	 */

	uint32 new_pw_len = IVAL(buffer, 512);

#ifdef DEBUG_PASSWORD
	dump_data(100, buffer, 516);
#endif

	if (new_pw_len < 0 || new_pw_len > new_passwd_size - 1)
	{
		DEBUG(0,("check_oem_password: incorrect password length (%d).\n", new_pw_len));
		return False;
	}

	if (nt_pass_set)
	{
		/*
		 * nt passwords are in unicode
		 */
		int uni_pw_len = new_pw_len;
		new_pw_len /= 2;
		unibuf_to_ascii(new_passwd, &buffer[512-uni_pw_len], new_pw_len);
	}
	else
	{
		memcpy(new_passwd, &buffer[512-new_pw_len], new_pw_len);
		new_passwd[new_pw_len] = '\0';
	}

	return True;
}

/***********************************************************
 encode a password buffer
************************************************************/
BOOL encode_pw_buffer(char buffer[516], const char *new_pass,
			int new_pw_len, BOOL nt_pass_set)
{
	generate_random_buffer(buffer, 516, True);

	if (nt_pass_set)
	{
		/*
		 * nt passwords are in unicode.  last char overwrites NULL
		 * in ascii_to_unibuf, so use SIVAL *afterwards*.
		 */
		new_pw_len *= 2;
		ascii_to_unibuf(&buffer[512-new_pw_len], new_pass, new_pw_len);
	}
	else
	{
		memcpy(&buffer[512-new_pw_len], new_pass, new_pw_len);
	}

	/* 
	 * The length of the new password is in the last 4 bytes of
	 * the data buffer.
	 */

	SIVAL(buffer, 512, new_pw_len);

#ifdef DEBUG_PASSWORD
	dump_data(100, buffer, 516);
#endif

	return True;
}
