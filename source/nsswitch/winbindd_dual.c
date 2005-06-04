/* 
   Unix SMB/CIFS implementation.

   Winbind background daemon

   Copyright (C) Andrew Tridgell 2002
   Copyright (C) Volker Lendecke 2004,2005
   
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
  the idea of the optional dual daemon mode is ot prevent slow domain
  responses from clagging up the rest of the system. When in dual
  daemon mode winbindd always responds to requests from cache if the
  request is in cache, and if the cached answer is stale then it asks
  the "dual daemon" to update the cache for that request

 */

#include "includes.h"
#include "winbindd.h"

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_WINBIND

extern BOOL opt_dual_daemon;
BOOL background_process = False;
int dual_daemon_pipe = -1;


/* a list of requests ready to be sent to the dual daemon */
struct dual_list {
	struct dual_list *next;
	char *data;
	int length;
	int offset;
};

static struct dual_list *dual_list;
static struct dual_list *dual_list_end;

/* Read some data from a client connection */

static void dual_client_read(struct winbindd_cli_state *state)
{
	int n;
    
	/* Read data */

	n = sys_read(state->sock, state->read_buf_len + 
		 (char *)&state->request, 
		 sizeof(state->request) - state->read_buf_len);
	
	DEBUG(10,("client_read: read %d bytes. Need %ld more for a full "
		  "request.\n", n, (unsigned long)(sizeof(state->request) - n -
						   state->read_buf_len) ));

	/* Read failed, kill client */
	
	if (n == -1 || n == 0) {
		DEBUG(5,("read failed on sock %d, pid %lu: %s\n",
			 state->sock, (unsigned long)state->pid, 
			 (n == -1) ? strerror(errno) : "EOF"));
		
		state->finished = True;
		return;
	}
	
	/* Update client state */
	
	state->read_buf_len += n;
	state->last_access = time(NULL);
}

/*
  setup a select() including the dual daemon pipe
 */
int dual_select_setup(fd_set *fds, int maxfd)
{
	if (dual_daemon_pipe == -1 ||
	    !dual_list) {
		return maxfd;
	}

	FD_SET(dual_daemon_pipe, fds);
	if (dual_daemon_pipe > maxfd) {
		maxfd = dual_daemon_pipe;
	}
	return maxfd;
}


/*
  a hook called from the main winbindd select() loop to handle writes
  to the dual daemon pipe 
*/
void dual_select(fd_set *fds)
{
	int n;

	if (dual_daemon_pipe == -1 ||
	    !dual_list ||
	    !FD_ISSET(dual_daemon_pipe, fds)) {
		return;
	}

	n = sys_write(dual_daemon_pipe, 
		  &dual_list->data[dual_list->offset],
		  dual_list->length - dual_list->offset);

	if (n <= 0) {
		/* the pipe is dead! fall back to normal operation */
		dual_daemon_pipe = -1;
		return;
	}

	dual_list->offset += n;

	if (dual_list->offset == dual_list->length) {
		struct dual_list *next;
		next = dual_list->next;
		free(dual_list->data);
		free(dual_list);
		dual_list = next;
		if (!dual_list) {
			dual_list_end = NULL;
		}
	}
}

/* 
   send a request to the background daemon 
   this is called for stale cached entries
*/
void dual_send_request(struct winbindd_cli_state *state)
{
	struct dual_list *list;

	if (!background_process) return;

	list = SMB_MALLOC_P(struct dual_list);
	if (!list) return;

	list->next = NULL;
	list->data = memdup(&state->request, sizeof(state->request));
	list->length = sizeof(state->request);
	list->offset = 0;
	
	if (!dual_list_end) {
		dual_list = list;
		dual_list_end = list;
	} else {
		dual_list_end->next = list;
		dual_list_end = list;
	}

	background_process = False;
}


/* 
the main dual daemon 
*/
void do_dual_daemon(void)
{
	int fdpair[2];
	struct winbindd_cli_state state;
	
	if (pipe(fdpair) != 0) {
		return;
	}

	ZERO_STRUCT(state);
	state.pid = getpid();

	dual_daemon_pipe = fdpair[1];
	state.sock = fdpair[0];

	if (sys_fork() != 0) {
		close(fdpair[0]);
		return;
	}
	close(fdpair[1]);

	/* tdb needs special fork handling */
	if (tdb_reopen_all() == -1) {
		DEBUG(0,("tdb_reopen_all failed.\n"));
		_exit(0);
	}
	
	dual_daemon_pipe = -1;
	opt_dual_daemon = False;

	while (1) {
		/* free up any talloc memory */
		lp_talloc_free();
		main_loop_talloc_free();

		/* fetch a request from the main daemon */
		dual_client_read(&state);

		if (state.finished) {
			/* we lost contact with our parent */
			exit(0);
		}

		/* process full rquests */
		if (state.read_buf_len == sizeof(state.request)) {
			DEBUG(4,("dual daemon request %d\n", (int)state.request.cmd));

			/* special handling for the stateful requests */
			switch (state.request.cmd) {
			case WINBINDD_GETPWENT:
				winbindd_setpwent(&state);
				break;
				
			case WINBINDD_GETGRENT:
			case WINBINDD_GETGRLST:
				winbindd_setgrent(&state);
				break;
			default:
				break;
			}

			winbind_process_packet(&state);
			SAFE_FREE(state.response.extra_data);

			free_getent_state(state.getpwent_state);
			free_getent_state(state.getgrent_state);
			state.getpwent_state = NULL;
			state.getgrent_state = NULL;
		}
	}
}

/*
 * Machinery for async requests sent to children. You set up a
 * winbindd_request, select a child to query, and issue a async_request
 * call. When the request is completed, the callback function you specified is
 * called back with the private pointer you gave to async_request.
 */

struct winbindd_async_request {
	struct winbindd_async_request *next, *prev;
	TALLOC_CTX *mem_ctx;
	struct winbindd_child *child;
	struct winbindd_request *request;
	struct winbindd_response *response;
	void (*continuation)(void *private, BOOL success);
	void *private;
};

static void async_request_sent(void *private, BOOL success);
static void async_reply_recv(void *private, BOOL success);
static void schedule_async_request(struct winbindd_child *child);

void async_request(TALLOC_CTX *mem_ctx, struct winbindd_child *child,
		   struct winbindd_request *request,
		   struct winbindd_response *response,
		   void (*continuation)(void *private, BOOL success),
		   void *private)
{
	struct winbindd_async_request *state, *tmp;

	SMB_ASSERT(continuation != NULL);

	state = TALLOC_P(mem_ctx, struct winbindd_async_request);

	if (state == NULL) {
		DEBUG(0, ("talloc failed\n"));
		continuation(private, False);
		return;
	}

	state->mem_ctx = mem_ctx;
	state->child = child;
	state->request = request;
	state->response = response;
	state->continuation = continuation;
	state->private = private;

	DLIST_ADD_END(child->requests, state, tmp);

	schedule_async_request(child);

	return;
}

static void async_request_sent(void *private, BOOL success)
{
	struct winbindd_async_request *state =
		talloc_get_type_abort(private, struct winbindd_async_request);

	if (!success) {
		DEBUG(5, ("Could not send async request"));

		state->response->length = sizeof(struct winbindd_response);
		state->response->result = WINBINDD_ERROR;
		state->continuation(state->private, False);
		return;
	}

	/* Request successfully sent to the child, setup the wait for reply */

	setup_async_read(&state->child->event,
			 &state->response->result,
			 sizeof(state->response->result),
			 async_reply_recv, state);
}

static void async_reply_recv(void *private, BOOL success)
{
	struct winbindd_async_request *state =
		talloc_get_type_abort(private, struct winbindd_async_request);
	struct winbindd_child *child = state->child;

	state->response->length = sizeof(struct winbindd_response);

	if (!success) {
		DEBUG(5, ("Could not receive async reply\n"));
		state->response->result = WINBINDD_ERROR;
	}

	if (state->response->result == WINBINDD_OK)
		SMB_ASSERT(cache_retrieve_response(child->pid,
						   state->response));

	DLIST_REMOVE(child->requests, state);

	schedule_async_request(child);

	state->continuation(state->private, True);
}

static void schedule_async_request(struct winbindd_child *child)
{
	struct winbindd_async_request *request = child->requests;

	if (request == NULL)
		return;

	if (child->event.flags != 0)
		return;		/* Busy */

	setup_async_write(&child->event, request->request,
			  sizeof(*request->request),
			  async_request_sent, request);
}

struct domain_request_state {
	TALLOC_CTX *mem_ctx;
	struct winbindd_domain *domain;
	struct winbindd_request *request;
	struct winbindd_response *response;
	void (*continuation)(void *private, BOOL success);
	void *private;
};

static void domain_init_recv(void *private, BOOL success);

void async_domain_request(TALLOC_CTX *mem_ctx,
			  struct winbindd_domain *domain,
			  struct winbindd_request *request,
			  struct winbindd_response *response,
			  void (*continuation)(void *private, BOOL success),
			  void *private)
{
	struct domain_request_state *state;

	if (domain->initialized) {
		async_request(mem_ctx, &domain->child, request, response,
			      continuation, private);
		return;
	}

	state = TALLOC_P(mem_ctx, struct domain_request_state);
	if (state == NULL) {
		DEBUG(0, ("talloc failed\n"));
		continuation(private, False);
		return;
	}

	state->mem_ctx = mem_ctx;
	state->domain = domain;
	state->request = request;
	state->response = response;
	state->continuation = continuation;
	state->private = private;

	init_child_connection(domain, domain_init_recv, state);
}

static void domain_init_recv(void *private, BOOL success)
{
	struct domain_request_state *state =
		talloc_get_type_abort(private, struct domain_request_state);

	if (!success) {
		DEBUG(5, ("Domain init returned an error\n"));
		state->continuation(state->private, False);
		return;
	}

	async_request(state->mem_ctx, &state->domain->child,
		      state->request, state->response,
		      state->continuation, state->private);
}

struct winbindd_child_dispatch_table {
	enum winbindd_cmd cmd;
	enum winbindd_result (*fn)(struct winbindd_domain *domain,
				   struct winbindd_cli_state *state);
	const char *winbindd_cmd_name;
};

static struct winbindd_child_dispatch_table child_dispatch_table[] = {
	
	{ WINBINDD_LOOKUPSID, winbindd_dual_lookupsid, "LOOKUPSID" },
	{ WINBINDD_LOOKUPNAME, winbindd_dual_lookupname, "LOOKUPNAME" },
	{ WINBINDD_LIST_TRUSTDOM, winbindd_dual_list_trusted_domains,
	  "LIST_TRUSTDOM" },
	{ WINBINDD_INIT_CONNECTION, winbindd_dual_init_connection,
	  "INIT_CONNECTION" },
	{ WINBINDD_GETDCNAME, winbindd_dual_getdcname, "GETDCNAME" },
	{ WINBINDD_SHOW_SEQUENCE, winbindd_dual_show_sequence,
	  "SHOW_SEQUENCE" },
	{ WINBINDD_PAM_AUTH, winbindd_dual_pam_auth, "PAM_AUTH" },
	{ WINBINDD_PAM_AUTH_CRAP, winbindd_dual_pam_auth_crap, "AUTH_CRAP" },
	{ WINBINDD_CHECK_MACHACC, winbindd_dual_check_machine_acct,
	  "CHECK_MACHACC" },
	{ WINBINDD_DUAL_SID2UID, winbindd_dual_sid2uid, "DUAL_SID2UID" },
	{ WINBINDD_DUAL_SID2GID, winbindd_dual_sid2gid, "DUAL_SID2GID" },
	{ WINBINDD_DUAL_UID2NAME, winbindd_dual_uid2name, "DUAL_UID2NAME" },
	{ WINBINDD_DUAL_NAME2UID, winbindd_dual_name2uid, "DUAL_NAME2UID" },
	{ WINBINDD_DUAL_GID2NAME, winbindd_dual_gid2name, "DUAL_GID2NAME" },
	{ WINBINDD_DUAL_NAME2GID, winbindd_dual_name2gid, "DUAL_NAME2GID" },
	{ WINBINDD_DUAL_IDMAPSET, winbindd_dual_idmapset, "DUAL_IDMAPSET" },
	{ WINBINDD_DUAL_USERINFO, winbindd_dual_userinfo, "DUAL_USERINFO" },
	{ WINBINDD_ALLOCATE_RID, winbindd_dual_allocate_rid, "ALLOCATE_RID" },
	{ WINBINDD_ALLOCATE_RID_AND_GID, winbindd_dual_allocate_rid_and_gid,
	  "ALLOCATE_RID_AND_GID" },
	{ WINBINDD_GETUSERDOMGROUPS, winbindd_dual_getuserdomgroups,
	  "GETUSERDOMGROUPS" },
	{ WINBINDD_DUAL_GETSIDALIASES, winbindd_dual_getsidaliases,
	  "GETSIDALIASES" },
	/* End of list */

	{ WINBINDD_NUM_CMDS, NULL, "NONE" }
};

static void child_process_request(struct winbindd_domain *domain,
				  struct winbindd_cli_state *state)
{
	struct winbindd_child_dispatch_table *table;

	/* Free response data - we may be interrupted and receive another
	   command before being able to send this data off. */

	state->response.result = WINBINDD_ERROR;
	state->response.length = sizeof(struct winbindd_response);

	state->mem_ctx = talloc_init("winbind request");
	if (state->mem_ctx == NULL)
		return;

	/* Process command */

	for (table = child_dispatch_table; table->fn; table++) {
		if (state->request.cmd == table->cmd) {
			DEBUG(10,("process_request: request fn %s\n",
				  table->winbindd_cmd_name ));
			state->response.result = table->fn(domain, state);
			break;
		}
	}

	if (!table->fn) {
		DEBUG(10,("process_request: unknown request fn number %d\n",
			  (int)state->request.cmd ));
		state->response.result = WINBINDD_ERROR;
	}

	talloc_destroy(state->mem_ctx);
}

BOOL setup_domain_child(struct winbindd_domain *domain,
			struct winbindd_child *child,
			const char *explicit_logfile)
{
	int fdpair[2];
	struct winbindd_cli_state state;

	extern BOOL override_logfile;
	pstring logfilename;

	if (explicit_logfile != NULL) {
		pstr_sprintf(logfilename, "%s/log.winbindd-%s",
			     dyn_LOGFILEBASE, explicit_logfile);
	} else if (domain != NULL) {
		pstr_sprintf(logfilename, "%s/log.wb-%s",
			     dyn_LOGFILEBASE, domain->name);
	} else {
		smb_panic("Internal error: domain == NULL && "
			  "explicit_logfile == NULL");
	}

	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, fdpair) != 0) {
		DEBUG(0, ("Could not open child pipe: %s\n",
			  strerror(errno)));
		return False;
	}

	ZERO_STRUCT(state);
	state.pid = getpid();

	child->pid = sys_fork();

	if (child->pid == -1) {
		DEBUG(0, ("Could not fork: %s\n", strerror(errno)));
		return False;
	}

	if (child->pid != 0) {
		/* Parent */
		close(fdpair[0]);
		child->event.fd = fdpair[1];
		child->event.flags = 0;
		child->requests = NULL;
		add_fd_event(&child->event);
		return True;
	}

	/* Child */

	state.sock = fdpair[0];
	close(fdpair[1]);

	/* tdb needs special fork handling */
	if (tdb_reopen_all() == -1) {
		DEBUG(0,("tdb_reopen_all failed.\n"));
		_exit(0);
	}

	close_conns_after_fork();

	if (!override_logfile) {
		lp_set_logfile(logfilename);
		reopen_logs();
	}
	
	dual_daemon_pipe = -1;
	opt_dual_daemon = False;

	while (1) {
		/* free up any talloc memory */
		lp_talloc_free();
		main_loop_talloc_free();

		/* fetch a request from the main daemon */
		dual_client_read(&state);

		if (state.finished) {
			/* we lost contact with our parent */
			exit(0);
		}

		/* process full rquests */
		if (state.read_buf_len == sizeof(state.request)) {
			DEBUG(4,("dual daemon request %d\n", (int)state.request.cmd));

			/* special handling for the stateful requests */
			switch (state.request.cmd) {
			case WINBINDD_GETPWENT:
				winbindd_setpwent(&state);
				break;
				
			case WINBINDD_GETGRENT:
			case WINBINDD_GETGRLST:
				winbindd_setgrent(&state);
				break;
			default:
				break;
			}

			state.request.null_term = '\0';
			child_process_request(domain, &state);

			if (state.response.result == WINBINDD_OK)
				cache_store_response(sys_getpid(),
						     &state.response);

			SAFE_FREE(state.response.extra_data);
			free_getent_state(state.getpwent_state);
			free_getent_state(state.getgrent_state);
			state.getpwent_state = NULL;
			state.getgrent_state = NULL;

			/* We just send the result code back, the result
			 * structure needs to be fetched via the
			 * winbindd_cache. Hmm. That needs fixing... */

			if (write_data(state.sock,
				       (void *)&state.response.result,
				       sizeof(state.response.result)) !=
			    sizeof(state.response.result)) {
				DEBUG(0, ("Could not write result\n"));
				exit(1);
			}

			state.read_buf_len = 0;
		}
	}
}
