/* 
   Unix SMB/CIFS implementation.

   code to manipulate domain credentials

   Copyright (C) Andrew Tridgell 1997-2003
   
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

/*
  initialise the credentials state and return the initial credentials
  to be sent as part of a netr_ServerAuthenticate*() call.

  this call is made after the netr_ServerReqChallenge call
*/
static void creds_init(struct netr_CredentialState *creds,
		       const struct netr_Credential *client_challenge,
		       const struct netr_Credential *server_challenge,
		       const uint8 machine_password[16])
{
	struct netr_Credential time_cred;
	uint32 sum[2];
	uint8 sum2[8];

	sum[0] = IVAL(client_challenge->data, 0) + IVAL(server_challenge->data, 0);
	sum[1] = IVAL(client_challenge->data, 4) + IVAL(server_challenge->data, 4);

	SIVAL(sum2,0,sum[0]);
	SIVAL(sum2,4,sum[1]);

	cred_hash1(creds->session_key, sum2, machine_password);

	creds->sequence = time(NULL);

	SIVAL(time_cred.data, 0, IVAL(client_challenge->data, 0));
	SIVAL(time_cred.data, 4, IVAL(client_challenge->data, 4));
	cred_hash2(creds->client.data, time_cred.data, creds->session_key);

	SIVAL(time_cred.data, 0, IVAL(server_challenge->data, 0));
	SIVAL(time_cred.data, 4, IVAL(server_challenge->data, 4));
	cred_hash2(creds->server.data, time_cred.data, creds->session_key);

	creds->seed = creds->client;
}


/*
  step the credentials to the next element in the chain
*/
static void creds_step(struct netr_CredentialState *creds)
{
	struct netr_Credential time_cred;

	creds->sequence += 2;

	DEBUG(5,("\tseed        %08x:%08x\n", 
		 IVAL(creds->seed.data, 0), IVAL(creds->seed.data, 4)));

	SIVAL(time_cred.data, 0, IVAL(creds->seed.data, 0) + creds->sequence);
	SIVAL(time_cred.data, 4, IVAL(creds->seed.data, 4));

	DEBUG(5,("\tseed+time   %08x:%08x\n", IVAL(time_cred.data, 0), IVAL(time_cred.data, 4)));

	cred_hash2(creds->client.data, time_cred.data, creds->session_key);

	DEBUG(5,("\tCLIENT      %08x:%08x\n", 
		 IVAL(creds->client.data, 0), IVAL(creds->client.data, 4)));

	SIVAL(time_cred.data, 0, IVAL(creds->seed.data, 0) + creds->sequence + 1);
	SIVAL(time_cred.data, 4, IVAL(creds->seed.data, 4));

	DEBUG(5,("\tseed+time+1 %08x:%08x\n", 
		 IVAL(time_cred.data, 0), IVAL(time_cred.data, 4)));

	cred_hash2(creds->server.data, time_cred.data, creds->session_key);

	DEBUG(5,("\tSERVER      %08x:%08x\n", 
		 IVAL(creds->server.data, 0), IVAL(creds->server.data, 4)));

	creds->seed = time_cred;
}


/*
  initialise the credentials chain and return the first client
  credentials
*/
void creds_client_init(struct netr_CredentialState *creds,
		       const struct netr_Credential *client_challenge,
		       const struct netr_Credential *server_challenge,
		       const uint8 machine_password[16],
		       struct netr_Credential *initial_credential)
{
	creds_init(creds, client_challenge, server_challenge, machine_password);

	*initial_credential = creds->client;
}

/*
  check that a credentials reply from a server is correct
*/
BOOL creds_client_check(struct netr_CredentialState *creds,
			const struct netr_Credential *received_credentials)
{
	if (memcmp(received_credentials->data, creds->server.data, 8) != 0) {
		DEBUG(2,("credentials check failed\n"));
		return False;
	}
	return True;
}

/*
  produce the next authenticator in the sequence ready to send to 
  the server
*/
void creds_client_authenticator(struct netr_CredentialState *creds,
				struct netr_Authenticator *next)
{
	creds_step(creds);

	next->cred = creds->client;
	next->timestamp = creds->sequence;
}


/*
  encrypt a 16 byte password buffer using the session key
*/
void creds_client_encrypt(struct netr_CredentialState *creds, struct netr_Password *pass)
{
	struct netr_Password tmp;
	cred_hash3(tmp.data, pass->data, creds->session_key, 1);
	*pass = tmp;
}
