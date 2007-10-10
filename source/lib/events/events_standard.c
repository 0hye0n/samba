/* 
   Unix SMB/CIFS implementation.
   main select loop and event handling
   Copyright (C) Andrew Tridgell	2003-2005
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

/*
  This is SAMBA's default event loop code

  - we try to use epoll if configure detected support for it
    otherwise we use select()
  - if epoll is broken on the system or the kernel doesn't support it
    at runtime we fallback to select()
*/

#include "includes.h"
#include "system/filesys.h"
#include "dlinklist.h"
#include "lib/events/events.h"
#include "lib/events/events_internal.h"

/* use epoll if it is available */
#if defined(HAVE_EPOLL_CREATE) && defined(HAVE_SYS_EPOLL_H)
#define WITH_EPOLL 1
#endif

#if WITH_EPOLL
#include <sys/epoll.h>
#endif

struct std_event_context {
	/* list of filedescriptor events */
	struct fd_event *fd_events;

	/* list of timed events */
	struct timed_event *timed_events;

	/* the maximum file descriptor number in fd_events */
	int maxfd;

	/* information for exiting from the event loop */
	int exit_code;

	/* this is changed by the destructors for the fd event
	   type. It is used to detect event destruction by event
	   handlers, which means the code that is calling the event
	   handler needs to assume that the linked list is no longer
	   valid
	*/
	uint32_t destruction_count;

#if WITH_EPOLL
	/* when using epoll this is the handle from epoll_create */
	int epoll_fd;
#endif
};

/*
  destroy an event context
*/
static int std_event_context_destructor(void *ptr)
{
#if WITH_EPOLL
	struct event_context *ev = talloc_get_type(ptr, struct event_context);
	struct std_event_context *std_ev = talloc_get_type(ev->additional_data,
							   struct std_event_context);
	if (std_ev->epoll_fd != -1) {
		close(std_ev->epoll_fd);
		std_ev->epoll_fd = -1;
	}
#endif
	return 0;
}

/*
  create a std_event_context structure.
*/
static int std_event_context_init(struct event_context *ev, void *privata_data)
{
	struct std_event_context *std_ev;

	std_ev = talloc_zero(ev, struct std_event_context);
	if (!std_ev) return -1;

#if WITH_EPOLL
	std_ev->epoll_fd = epoll_create(64);
#endif

	ev->additional_data = std_ev;

	talloc_set_destructor(ev, std_event_context_destructor);

	return 0;
}

/*
  recalculate the maxfd
*/
static void calc_maxfd(struct event_context *ev)
{
	struct std_event_context *std_ev = talloc_get_type(ev->additional_data,
							   struct std_event_context);
	struct fd_event *fde;

	std_ev->maxfd = 0;
	for (fde = std_ev->fd_events; fde; fde = fde->next) {
		if (fde->fd > std_ev->maxfd) {
			std_ev->maxfd = fde->fd;
		}
	}
}


/* to mark the ev->maxfd invalid
 * this means we need to recalculate it
 */
#define EVENT_INVALID_MAXFD (-1)


#if WITH_EPOLL
/*
  called when a epoll call fails, and we should fallback
  to using select
*/
static void epoll_fallback_to_select(struct event_context *ev, const char *reason)
{
	struct std_event_context *std_ev = talloc_get_type(ev->additional_data,
							   struct std_event_context);
	DEBUG(0,("%s (%s) - falling back to select()\n", reason, strerror(errno)));
	close(std_ev->epoll_fd);
	std_ev->epoll_fd = -1;
}
#endif


#if WITH_EPOLL
/*
  map from EVENT_FD_* to EPOLLIN/EPOLLOUT
*/
static uint32_t epoll_map_flags(uint16_t flags)
{
	uint32_t ret = 0;
	if (flags & EVENT_FD_READ) ret |= EPOLLIN;
	if (flags & EVENT_FD_WRITE) ret |= EPOLLOUT;
	return ret;
}
#endif

/*
  destroy an fd_event
*/
static int std_event_fd_destructor(void *ptr)
{
	struct fd_event *fde = talloc_get_type(ptr, struct fd_event);
	struct event_context *ev = fde->event_ctx;
	struct std_event_context *std_ev = talloc_get_type(ev->additional_data,
							   struct std_event_context);

	if (std_ev->maxfd == fde->fd) {
		std_ev->maxfd = EVENT_INVALID_MAXFD;
	}
	DLIST_REMOVE(std_ev->fd_events, fde);
	std_ev->destruction_count++;
#if WITH_EPOLL
	if (std_ev->epoll_fd != -1) {
		struct epoll_event event;
		ZERO_STRUCT(event);
		event.events = epoll_map_flags(fde->flags);
		event.data.ptr = fde;
		epoll_ctl(std_ev->epoll_fd, EPOLL_CTL_DEL, fde->fd, &event);
	}
#endif
	return 0;
}

/*
  add a fd based event
  return NULL on failure (memory allocation error)
*/
static struct fd_event *std_event_add_fd(struct event_context *ev, TALLOC_CTX *mem_ctx,
					 int fd, uint16_t flags,
					 event_fd_handler_t handler,
					 void *private_data)
{
	struct std_event_context *std_ev = talloc_get_type(ev->additional_data,
							   struct std_event_context);
	struct fd_event *fde;

	fde = talloc(mem_ctx?mem_ctx:ev, struct fd_event);
	if (!fde) return NULL;

	fde->event_ctx		= ev;
	fde->fd			= fd;
	fde->flags		= flags;
	fde->handler		= handler;
	fde->private_data	= private_data;
	fde->additional_data	= NULL;

	DLIST_ADD(std_ev->fd_events, fde);

	if (fde->fd > std_ev->maxfd) {
		std_ev->maxfd = fde->fd;
	}

	talloc_set_destructor(fde, std_event_fd_destructor);

#if WITH_EPOLL
	if (std_ev->epoll_fd != -1) {
		struct epoll_event event;
		ZERO_STRUCT(event);
		event.events = epoll_map_flags(flags);
		event.data.ptr = fde;
		if (epoll_ctl(std_ev->epoll_fd, EPOLL_CTL_ADD, fde->fd, &event) != 0) {
			epoll_fallback_to_select(ev, "EPOLL_CTL_ADD failed");
		}
	}
#endif

	return fde;
}


/*
  return the fd event flags
*/
static uint16_t std_event_get_fd_flags(struct fd_event *fde)
{
	return fde?fde->flags:0;
}

/*
  set the fd event flags
*/
static void std_event_set_fd_flags(struct fd_event *fde, uint16_t flags)
{
#if WITH_EPOLL
	struct event_context *ev;
	struct std_event_context *std_ev;
	if (fde == NULL || 
	    fde->flags == flags) {
		return;
	}
	ev = fde->event_ctx;
	std_ev = talloc_get_type(ev->additional_data, struct std_event_context);
	if (std_ev->epoll_fd != -1) {
		struct epoll_event event;
		ZERO_STRUCT(event);
		event.events = epoll_map_flags(flags);
		event.data.ptr = fde;
		if (epoll_ctl(std_ev->epoll_fd, EPOLL_CTL_MOD, fde->fd, &event) != 0) {
			epoll_fallback_to_select(ev, "EPOLL_CTL_MOD failed");
		}
	}
#endif
	if (fde) {
		fde->flags = flags;
	}
}

/*
  destroy a timed event
*/
static int std_event_timed_destructor(void *ptr)
{
	struct timed_event *te = talloc_get_type(ptr, struct timed_event);
	struct std_event_context *std_ev = talloc_get_type(te->event_ctx->additional_data,
							   struct std_event_context);
	DLIST_REMOVE(std_ev->timed_events, te);
	return 0;
}

/*
  add a timed event
  return NULL on failure (memory allocation error)
*/
static struct timed_event *std_event_add_timed(struct event_context *ev, TALLOC_CTX *mem_ctx,
					       struct timeval next_event, 
					       event_timed_handler_t handler, 
					       void *private_data) 
{
	struct std_event_context *std_ev = talloc_get_type(ev->additional_data,
							   struct std_event_context);
	struct timed_event *te, *last_te, *cur_te;

	te = talloc(mem_ctx?mem_ctx:ev, struct timed_event);
	if (te == NULL) return NULL;

	te->event_ctx		= ev;
	te->next_event		= next_event;
	te->handler		= handler;
	te->private_data	= private_data;
	te->additional_data	= NULL;

	/* keep the list ordered */
	last_te = NULL;
	for (cur_te = std_ev->timed_events; cur_te; cur_te = cur_te->next) {
		/* if the new event comes before the current one break */
		if (!timeval_is_zero(&cur_te->next_event) &&
		    timeval_compare(&cur_te->next_event, &te->next_event) < 0) {
			break;
		}

		last_te = cur_te;
	}

	DLIST_ADD_AFTER(std_ev->timed_events, te, last_te);

	talloc_set_destructor(te, std_event_timed_destructor);

	return te;
}

/*
  a timer has gone off - call it
*/
static void std_event_loop_timer(struct event_context *ev)
{
	struct std_event_context *std_ev = talloc_get_type(ev->additional_data,
							   struct std_event_context);
	struct timeval t = timeval_current();
	struct timed_event *te = std_ev->timed_events;

	te->next_event = timeval_zero();

	te->handler(ev, te, t, te->private_data);

	/* note the care taken to prevent referencing a event
	   that could have been freed by the handler */
	if (std_ev->timed_events) {
		if (timeval_is_zero(&std_ev->timed_events->next_event)) {
			talloc_free(te);
		}
	}
}

#if WITH_EPOLL
/*
  event loop handling using epoll
*/
static int std_event_loop_epoll(struct event_context *ev, struct timeval *tvalp)
{
	struct std_event_context *std_ev = talloc_get_type(ev->additional_data,
		 					   struct std_event_context);
	int ret, i;
#define MAXEVENTS 8
	struct epoll_event events[MAXEVENTS];
	uint32_t destruction_count = std_ev->destruction_count;
	int timeout = -1;

	if (tvalp) {
		/* it's better to trigger timed events a bit later than to early */
		timeout = ((tvalp->tv_usec+999) / 1000) + (tvalp->tv_sec*1000);
	}

	ret = epoll_wait(std_ev->epoll_fd, events, MAXEVENTS, timeout);

	if (ret == -1 && errno != EINTR) {
		epoll_fallback_to_select(ev, "epoll_wait() failed");
		return -1;
	}

	if (ret == 0 && tvalp) {
		std_event_loop_timer(ev);
		return 0;
	}

	for (i=0;i<ret;i++) {
		struct fd_event *fde = talloc_get_type(events[i].data.ptr, 
						       struct fd_event);
		uint16_t flags = 0;

		if (fde == NULL) {
			epoll_fallback_to_select(ev, "epoll_wait() gave bad data");
			return -1;
		}
		if (events[i].events & (EPOLLIN|EPOLLHUP|EPOLLERR)) 
			flags |= EVENT_FD_READ;
		if (events[i].events & EPOLLOUT) flags |= EVENT_FD_WRITE;
		if (flags) {
			fde->handler(ev, fde, flags, fde->private_data);
			if (destruction_count != std_ev->destruction_count) {
				break;
			}
		}
	}

	return 0;
}		
#endif

/*
  event loop handling using select()
*/
static int std_event_loop_select(struct event_context *ev, struct timeval *tvalp)
{
	struct std_event_context *std_ev = talloc_get_type(ev->additional_data,
		 					   struct std_event_context);
	fd_set r_fds, w_fds;
	struct fd_event *fde;
	int selrtn;
	uint32_t destruction_count = std_ev->destruction_count;

	/* we maybe need to recalculate the maxfd */
	if (std_ev->maxfd == EVENT_INVALID_MAXFD) {
		calc_maxfd(ev);
	}

	FD_ZERO(&r_fds);
	FD_ZERO(&w_fds);

	/* setup any fd events */
	for (fde = std_ev->fd_events; fde; fde = fde->next) {
		if (fde->flags & EVENT_FD_READ) {
			FD_SET(fde->fd, &r_fds);
		}
		if (fde->flags & EVENT_FD_WRITE) {
			FD_SET(fde->fd, &w_fds);
		}
	}

	selrtn = select(std_ev->maxfd+1, &r_fds, &w_fds, NULL, tvalp);

	if (selrtn == -1 && errno == EBADF) {
		/* the socket is dead! this should never
		   happen as the socket should have first been
		   made readable and that should have removed
		   the event, so this must be a bug. This is a
		   fatal error. */
		DEBUG(0,("ERROR: EBADF on std_event_loop_once\n"));
		std_ev->exit_code = EBADF;
		return -1;
	}

	if (selrtn == 0 && tvalp) {
		std_event_loop_timer(ev);
		return 0;
	}

	if (selrtn > 0) {
		/* at least one file descriptor is ready - check
		   which ones and call the handler, being careful to allow
		   the handler to remove itself when called */
		for (fde = std_ev->fd_events; fde; fde = fde->next) {
			uint16_t flags = 0;

			if (FD_ISSET(fde->fd, &r_fds)) flags |= EVENT_FD_READ;
			if (FD_ISSET(fde->fd, &w_fds)) flags |= EVENT_FD_WRITE;
			if (flags) {
				fde->handler(ev, fde, flags, fde->private_data);
				if (destruction_count != std_ev->destruction_count) {
					break;
				}
			}
		}
	}

	return 0;
}		

/*
  do a single event loop using the events defined in ev 
*/
static int std_event_loop_once(struct event_context *ev)
{
	struct std_event_context *std_ev = talloc_get_type(ev->additional_data,
		 					   struct std_event_context);
	struct timeval tval, *tvalp;

	tvalp = NULL;

	/* work out the right timeout for all timed events */
	if (std_ev->timed_events) {
		struct timeval t = timeval_current();

		tval = timeval_diff(&std_ev->timed_events->next_event, &t);
		tvalp = &tval;
		if (timeval_is_zero(tvalp)) {
			std_event_loop_timer(ev);
			return 0;
		}
	}

#if WITH_EPOLL
	if (std_ev->epoll_fd != -1) {
		if (std_event_loop_epoll(ev, tvalp) == 0) {
			return 0;
		}
	}
#endif

	return std_event_loop_select(ev, tvalp);
}

/*
  return on failure or (with 0) if all fd events are removed
*/
static int std_event_loop_wait(struct event_context *ev)
{
	struct std_event_context *std_ev = talloc_get_type(ev->additional_data,
							   struct std_event_context);
	std_ev->exit_code = 0;

	while (std_ev->fd_events && std_ev->exit_code == 0) {
		if (std_event_loop_once(ev) != 0) {
			break;
		}
	}

	return std_ev->exit_code;
}

static const struct event_ops std_event_ops = {
	.context_init	= std_event_context_init,
	.add_fd		= std_event_add_fd,
	.get_fd_flags	= std_event_get_fd_flags,
	.set_fd_flags	= std_event_set_fd_flags,
	.add_timed	= std_event_add_timed,
	.loop_once	= std_event_loop_once,
	.loop_wait	= std_event_loop_wait,
};

const struct event_ops *event_standard_get_ops(void)
{
	return &std_event_ops;
}
