/*
   Unix SMB/CIFS implementation.
   client connect/disconnect routines
   Copyright (C) Andrew Tridgell 2003

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
  wrapper around cli_sock_connect()
*/
BOOL cli_socket_connect(struct cli_state *cli, const char *server, struct in_addr *ip)
{
	struct cli_socket *sock;

	sock = cli_sock_init();
	if (!sock) return False;

	if (!cli_sock_connect_byname(sock, server, 0)) {
		cli_sock_close(sock);
		return False;
	}
	
	cli->transport = cli_transport_init(sock);
	if (!cli->transport) {
		cli_sock_close(sock);
		return False;
	}

	return True;
}

/* wrapper around cli_transport_connect() */
BOOL cli_transport_establish(struct cli_state *cli, 
			     struct nmb_name *calling,
			     struct nmb_name *called)
{
	return cli_transport_connect(cli->transport, calling, called);
}

/* wrapper around smb_raw_negotiate() */
NTSTATUS cli_negprot(struct cli_state *cli)
{
	return smb_raw_negotiate(cli->transport);
}

/* wrapper around smb_raw_session_setup() */
NTSTATUS cli_session_setup(struct cli_state *cli, 
			   const char *user, 
			   const char *password, 
			   const char *domain)
{
	union smb_sesssetup setup;
	NTSTATUS status;
	TALLOC_CTX *mem_ctx;

	cli->session = cli_session_init(cli->transport);
	if (!cli->session) return NT_STATUS_UNSUCCESSFUL;

	mem_ctx = talloc_init("cli_session_setup");
	if (!mem_ctx) return NT_STATUS_NO_MEMORY;

	setup.generic.level = RAW_SESSSETUP_GENERIC;
	setup.generic.in.sesskey = cli->transport->negotiate.sesskey;
	setup.generic.in.capabilities = CAP_UNICODE | CAP_STATUS32 | 
		CAP_LARGE_FILES | CAP_NT_SMBS | CAP_LEVEL_II_OPLOCKS | 
		CAP_W2K_SMBS | CAP_LARGE_READX | CAP_LARGE_WRITEX;
	setup.generic.in.password = password;
	setup.generic.in.user = user;
	setup.generic.in.domain = domain;

	status = smb_raw_session_setup(cli->session, mem_ctx, &setup);

	cli->session->vuid = setup.generic.out.vuid;

	talloc_destroy(mem_ctx);

	return status;
}

/* wrapper around smb_tree_connect() */
NTSTATUS cli_send_tconX(struct cli_state *cli, const char *sharename, 
			const char *devtype, const char *password)
{
	union smb_tcon tcon;
	TALLOC_CTX *mem_ctx;
	NTSTATUS status;

	cli->tree = cli_tree_init(cli->session);
	if (!cli->tree) return NT_STATUS_UNSUCCESSFUL;

	cli->tree->reference_count++;

	/* setup a tree connect */
	tcon.generic.level = RAW_TCON_TCONX;
	tcon.tconx.in.flags = 0;
	tcon.tconx.in.password = data_blob(password, strlen(password)+1);
	tcon.tconx.in.path = sharename;
	tcon.tconx.in.device = devtype;
	
	mem_ctx = talloc_init("tcon");
	if (!mem_ctx)
		return NT_STATUS_NO_MEMORY;

	status = smb_tree_connect(cli->tree, mem_ctx, &tcon);

	cli->tree->tid = tcon.tconx.out.cnum;

	talloc_destroy(mem_ctx);

	return status;
}


/*
  easy way to get to a fully connected cli_state in one call
*/
NTSTATUS cli_full_connection(struct cli_state **ret_cli, 
			     const char *myname,
			     const char *host,
			     struct in_addr *ip,
			     const char *sharename,
			     const char *devtype,
			     const char *username,
			     const char *domain,
			     const char *password,
			     uint_t flags,
			     BOOL *retry)
{
	struct cli_tree *tree;
	NTSTATUS status;
	char *p;
	TALLOC_CTX *mem_ctx;

	mem_ctx = talloc_init("cli_full_connection");

	*ret_cli = NULL;

	/* if the username is of the form DOMAIN\username then split out the domain */
	p = strpbrk(username, "\\/");
	if (p) {
		domain = talloc_strndup(mem_ctx, username, PTR_DIFF(p, username));
		username = talloc_strdup(mem_ctx, p+1);
	}

	status = cli_tree_full_connection(&tree, myname, host, 0, sharename, devtype,
					  username, domain, password);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	(*ret_cli) = cli_state_init();

	(*ret_cli)->tree = tree;
	(*ret_cli)->session = tree->session;
	(*ret_cli)->transport = tree->session->transport;
	tree->reference_count++;

done:
	talloc_destroy(mem_ctx);
	return status;
}


/*
  disconnect the tree
*/
NTSTATUS cli_tdis(struct cli_state *cli)
{
	return smb_tree_disconnect(cli->tree);
}

/****************************************************************************
 Initialise a client state structure.
****************************************************************************/
struct cli_state *cli_state_init(void)
{
	struct cli_state *cli;
	TALLOC_CTX *mem_ctx;

	mem_ctx = talloc_init("cli_state");
	if (!mem_ctx) return NULL;

	cli = talloc_zero(mem_ctx, sizeof(*cli));
	cli->mem_ctx = mem_ctx;

	return cli;
}

/****************************************************************************
 Shutdown a client structure.
****************************************************************************/
void cli_shutdown(struct cli_state *cli)
{
	if (!cli) return;
	if (cli->tree) {
		cli->tree->reference_count++;
		cli_tree_close(cli->tree);
	}
	if (cli->mem_ctx) {
		talloc_destroy(cli->mem_ctx);
	}
}
