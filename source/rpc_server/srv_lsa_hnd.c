/* 
 *  Unix SMB/Netbios implementation.
 *  Version 1.9.
 *  RPC Pipe client / server routines
 *  Copyright (C) Andrew Tridgell              1992-1997,
 *  Copyright (C) Luke Kenneth Casson Leighton 1996-1997,
 *  Copyright (C) Jeremy Allison			   2001.
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "includes.h"

extern int DEBUGLEVEL;

/* This is the max handles per pipe. */
#ifndef MAX_OPEN_POLS
#define MAX_OPEN_POLS 256
#endif

/****************************************************************************
  initialise policy handle states...
****************************************************************************/

void init_pipe_handles(pipes_struct *p)
{
	p->pipe_handles.Policy = NULL;
	p->pipe_handles.count = 0;
}

/****************************************************************************
  find first available policy slot.  creates a policy handle for you.
****************************************************************************/

BOOL create_policy_hnd(pipes_struct *p, POLICY_HND *hnd, void (*free_fn)(void *), void *data_ptr)
{
	static uint32 pol_hnd_low  = 0;
	static uint32 pol_hnd_high = 0;

	struct policy *pol;

	if (p->pipe_handles.count > MAX_OPEN_POLS) {
		DEBUG(0,("create_policy_hnd: ERROR: too many handles (%d) on this pipe.\n", (int)p->pipe_handles.count));
		return False;
	}

	pol = (struct policy *)malloc(sizeof(*p));
	if (!pol) {
		DEBUG(0,("create_policy_hnd: ERROR: out of memory!\n"));
		return False;
	}

	ZERO_STRUCTP(pol);

	pol->p = p;
	pol->data_ptr = data_ptr;
	pol->free_fn = free_fn;

    pol_hnd_low++;
    if (pol_hnd_low == 0) (pol_hnd_high)++;

    SIVAL(&pol->pol_hnd.data1, 0 , 0);  /* first bit must be null */
    SIVAL(&pol->pol_hnd.data2, 0 , pol_hnd_low ); /* second bit is incrementing */
    SSVAL(&pol->pol_hnd.data3, 0 , pol_hnd_high); /* second bit is incrementing */
    SSVAL(&pol->pol_hnd.data4, 0 , (pol_hnd_high>>16)); /* second bit is incrementing */
    SIVAL(pol->pol_hnd.data5, 0, time(NULL)); /* something random */
    SIVAL(pol->pol_hnd.data5, 4, sys_getpid()); /* something more random */

	DLIST_ADD(p->pipe_handles.Policy, pol);
	p->pipe_handles.count++;

	*hnd = pol->pol_hnd;
	
	DEBUG(4,("Opened policy hnd[%d] ", (int)p->pipe_handles.count));
	dump_data(4, (char *)hnd, sizeof(*hnd));

	return True;
}

/****************************************************************************
  find policy by handle - internal version.
****************************************************************************/

static struct policy *find_policy_by_hnd_internal(pipes_struct *p, POLICY_HND *hnd, void **data_p)
{
	struct policy *pol;
	size_t i;

	if (data_p)
		*data_p = NULL;

	for (i = 0, pol=p->pipe_handles.Policy;pol;pol=pol->next, i++) {
		if (memcmp(&pol->pol_hnd, hnd, sizeof(*hnd)) == 0) {
			DEBUG(4,("Found policy hnd[%d] ", (int)i));
			dump_data(4, (char *)hnd, sizeof(*hnd));
			if (data_p)
				*data_p = pol->data_ptr;
			return pol;
		}
	}

	DEBUG(4,("Policy not found: "));
	dump_data(4, (char *)hnd, sizeof(*hnd));

	return NULL;
}

/****************************************************************************
  find policy by handle
****************************************************************************/

BOOL find_policy_by_hnd(pipes_struct *p, POLICY_HND *hnd, void **data_p)
{
	return find_policy_by_hnd_internal(p, hnd, data_p) == NULL ? False : True;
}

/****************************************************************************
  Close a policy.
****************************************************************************/

BOOL close_policy_hnd(pipes_struct *p, POLICY_HND *hnd)
{
	struct policy *pol = find_policy_by_hnd_internal(p, hnd, NULL);

	if (!pol) {
		DEBUG(3,("Error closing policy\n"));
		return False;
	}

	DEBUG(3,("Closed policy\n"));

	if (pol->free_fn && pol->data_ptr)
		(*pol->free_fn)(pol->data_ptr);

	pol->p->pipe_handles.count--;

	DLIST_REMOVE(pol->p->pipe_handles.Policy, pol);

	ZERO_STRUCTP(pol);

	free(pol);

	return True;
}

/****************************************************************************
 Close all the pipe handles.
****************************************************************************/

void close_policy_by_pipe(pipes_struct *p)
{
	while (p->pipe_handles.Policy)
		close_policy_hnd(p, &p->pipe_handles.Policy->pol_hnd);

	p->pipe_handles.Policy = NULL;
	p->pipe_handles.count = 0;
}
