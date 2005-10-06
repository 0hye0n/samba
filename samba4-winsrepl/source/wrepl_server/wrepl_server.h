/* 
   Unix SMB/CIFS implementation.
   
   WINS Replication server
   
   Copyright (C) Stefan Metzmacher	2005
   
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

struct wreplsrv_service;
struct wreplsrv_in_connection;
struct wreplsrv_out_connection;
struct wreplsrv_partner;

#define WREPLSRV_VALID_ASSOC_CTX	0x12345678
#define WREPLSRV_INVALID_ASSOC_CTX	0x0000000a

/*
  state of an incoming wrepl call
*/
struct wreplsrv_in_call {
	struct wreplsrv_in_connection *wreplconn;
	struct wrepl_packet req_packet;
	struct wrepl_packet rep_packet;
};

/*
  state of an incoming wrepl connection
*/
struct wreplsrv_in_connection {
	struct wreplsrv_in_connection *prev,*next;
	struct stream_connection *conn;

	/* our global service context */
	struct wreplsrv_service *service;

	/*
	 * the partner that connects us,
	 * can be NULL, when we got a connection
	 * from an unknown address
	 */
	struct wreplsrv_partner *partner;

	/*
	 * we need to take care of our own ip address,
	 * as this is the WINS-Owner ID the peer expect
	 * from us.
	 */
	const char *our_ip;

	/* keep track of the assoc_ctx's */
	struct {
		BOOL stopped;
		uint32_t our_ctx;
		uint32_t peer_ctx;
	} assoc_ctx;

	/* the partial input on the connection */
	DATA_BLOB partial;
	size_t partial_read;

	/*
	 * are we currently processing a request?
	 * this prevents loops, with half async code
	 */
	BOOL processing;

	/*
	 * if this is set we no longer accept incoming packets
	 * and terminate the connection after we have send all packets
	 */
	BOOL terminate;

	/* the list of outgoing DATA_BLOB's that needs to be send */
	struct data_blob_list_item *send_queue;
};

/*
  state of an outcoming wrepl connection
*/
struct wreplsrv_out_connection {
	/* our global service context */
	struct wreplsrv_service *service;

	/*
	 * the partner that connects us,
	 * can be NULL, when we got a connection
	 * from an unknown address
	 */
	struct wreplsrv_partner *partner;

	/* keep track of the assoc_ctx's */
	struct {
		uint32_t our_ctx;
		uint32_t peer_ctx;
	} assoc_ctx;

	/* 
	 * the client socket to the partner,
	 * NULL if not yet connected
	 */
	struct wrepl_socket *sock;
};

enum winsrepl_partner_type {
	WINSREPL_PARTNER_NONE = 0x0,
	WINSREPL_PARTNER_PULL = 0x1,
	WINSREPL_PARTNER_PUSH = 0x2,
	WINSREPL_PARTNER_BOTH = (WINSREPL_PARTNER_PULL | WINSREPL_PARTNER_PUSH)
};

#define WINSREPL_DEFAULT_PULL_INTERVAL (30*60)
#define WINSREPL_DEFAULT_PULL_RETRY_INTERVAL (30)

/*
 this represents one of our configured partners
*/
struct wreplsrv_partner {
	struct wreplsrv_partner *prev,*next;

	/* our global service context */
	struct wreplsrv_service *service;

	/* the netbios name of the partner, mostly just for debugging */
	const char *name;

	/* the ip-address of the partner */
	const char *address;

	/* 
	 * as wins partners identified by ip-address, we need to use a specific source-ip
	 *  when we want to connect to the partner
	 */
	const char *our_address;

	/* the type of the partner, pull, push or both */
	enum winsrepl_partner_type type;

	/* pull specific options */
	struct {
		/* the interval between 2 pull replications to the partner */
		uint32_t interval;

		/* the retry_interval if a pull cycle failed to the partner */
		uint32_t retry_interval;

		/* the error count till the last success */
		uint32_t error_count;

		/* the status of the last pull cycle */
		NTSTATUS last_status;

		/* this is a list of each wins_owner the partner knows about */
		struct wreplsrv_owner *table;

		/* the outgoing connection to the partner */
		struct wreplsrv_out_connection *wreplconn;

		/* the current pending pull cycle request */
		struct composite_context *creq;

		/* the pull cycle io params */
		struct wreplsrv_pull_cycle_io *cycle_io;

		/* the current timed_event to the next pull cycle */
		struct timed_event *te;
	} pull;

	/* push specific options */
	struct {
		/* the outgoing connection to the partner */
		struct wreplsrv_out_connection *wreplconn;
	} push;
};

struct wreplsrv_owner {
	struct wreplsrv_owner *prev,*next;

	/* this hold the owner_id (address), min_version, max_version and partner_type */
	struct wrepl_wins_owner owner;

	/* can be NULL if this owner isn't a configure partner */
	struct wreplsrv_partner *partner; 
};

/*
  state of the whole wrepl service
*/
struct wreplsrv_service {
	/* the whole wrepl service is in one task */
	struct task_server *task;

	/* the winsdb handle */
	struct ldb_context *wins_db;

	/* all incoming connections */
	struct wreplsrv_in_connection *in_connections;

	/* all partners (pull and push) */
	struct wreplsrv_partner *partners;

	/* this is a list of each wins_owner we know about in our database */
	struct wreplsrv_owner *table;
};
