/* 
   Unix SMB/CIFS implementation.
   Password and authentication handling
   Copyright (C) Andrew Tridgell 1992-1998
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2005
   
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
#include "smb_server/smb_server.h"


/****************************************************************************
init the tcon structures
****************************************************************************/
void smbsrv_vuid_init(struct smbsrv_connection *smb_conn)
{
	smb_conn->sessions.idtree_vuid = idr_init(smb_conn);
}


/****************************************************************************
Find the session structure assoicated with a VUID (not one from an in-progress session setup)
****************************************************************************/
struct smbsrv_session *smbsrv_session_find(struct smbsrv_connection *smb_conn, uint16_t vuid)
{
	struct smbsrv_session *sess = idr_find(smb_conn->sessions.idtree_vuid, vuid);
	if (sess && sess->finished_sesssetup) {
		return sess;
	}
	return NULL;
}

/****************************************************************************
 Find a VUID assoicated with an in-progress session setup
****************************************************************************/
struct smbsrv_session *smbsrv_session_find_sesssetup(struct smbsrv_connection *smb_conn, uint16_t vuid)
{
	struct smbsrv_session *sess = idr_find(smb_conn->sessions.idtree_vuid, vuid);
	if (sess && !sess->finished_sesssetup) {
		return sess;
	}
	return NULL;
}

/****************************************************************************
invalidate a session
****************************************************************************/
static int smbsrv_session_destructor(void *p) 
{
	struct smbsrv_session *sess = p;
	struct smbsrv_connection *smb_conn = sess->smb_conn;

	/* clear the vuid from the 'cache' on each connection, and
	   from the vuid 'owner' of connections */
	/* REWRITE: conn_clear_vuid_cache(smb, vuid); */

	smb_conn->sessions.num_validated_vuids--;

	idr_remove(smb_conn->sessions.idtree_vuid, sess->vuid);
	return 0;
}

/****************************************************************************
invalidate a uid
****************************************************************************/
void smbsrv_invalidate_vuid(struct smbsrv_connection *smb_conn, uint16_t vuid)
{
	struct smbsrv_session *sess = smbsrv_session_find(smb_conn, vuid);
	talloc_free(sess);
}

/**
 *  register that a valid login has been performed, establish 'session'.
 *  @param session_info The token returned from the authentication process (if the authentication has completed)
 *   (now 'owned' by register_vuid)
 *
 *  @param smb_name The untranslated name of the user
 *
 *  @return Newly allocated vuid, biased by an offset. (This allows us to
 *   tell random client vuid's (normally zero) from valid vuids.)
 *
 */

struct smbsrv_session *smbsrv_register_session(struct smbsrv_connection *smb_conn,
					       struct auth_session_info *session_info,
					       struct gensec_security *gensec_ctx)
{
	struct smbsrv_session *sess = NULL;
	int i;

	/* Ensure no vuid gets registered in share level security. */
	/* TODO: replace lp_security with a flag in smbsrv_connection */
	if (lp_security() == SEC_SHARE)
		return UID_FIELD_INVALID;

	sess = talloc(smb_conn, struct smbsrv_session);
	if (sess == NULL) {
		DEBUG(0,("talloc(smb_conn->mem_ctx, struct smbsrv_session) failed\n"));
		return sess;
	}

	ZERO_STRUCTP(sess);

	i = idr_get_new_random(smb_conn->sessions.idtree_vuid, sess, UINT16_MAX);
	if (i == -1) {
		DEBUG(1,("ERROR! Out of connection structures\n"));	       
		talloc_free(sess);
		return NULL;
	}
	sess->vuid = i;

	smb_conn->sessions.num_validated_vuids++;

	/* use this to keep tabs on all our info from the authentication */
	sess->session_info = talloc_reference(sess, session_info);
	
	sess->gensec_ctx = talloc_reference(sess, gensec_ctx);

	sess->smb_conn = smb_conn;

	talloc_set_destructor(sess, smbsrv_session_destructor);

	return sess;
}
