
/* 
 *  Unix SMB/Netbios implementation.
 *  Version 1.9.
 *  RPC Pipe client / server routines
 *  Copyright (C) Andrew Tridgell              1992-1997,
 *  Copyright (C) Luke Kenneth Casson Leighton 1996-1997,
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

#ifndef MAX_OPEN_POLS
#define MAX_OPEN_POLS 64
#endif

#define POL_NO_INFO 0
#define POL_REG_INFO 1
#define POL_SAMR_INFO 2
#define POL_CLI_INFO 3

struct reg_info
{
    /* for use by \PIPE\winreg */
	fstring name; /* name of registry key */
};

struct samr_info
{
    /* for use by the \PIPE\samr policy */
	DOM_SID sid;
    uint32 rid; /* relative id associated with the pol_hnd */
    uint32 status; /* some sort of flag.  best to record it.  comes from opnum 0x39 */
};

struct cli_info
{
	struct cli_state *cli;
	uint16 fnum;
	void (*free)(struct cli_state*, uint16 fnum);
};

static struct policy
{
	struct policy *next, *prev;
	int pnum;
	BOOL open;
	POLICY_HND pol_hnd;
	int type;

	union {
		struct samr_info *samr;
		struct reg_info *reg;
		struct cli_info *cli;

	} dev;

} *Policy;

static struct bitmap *bmap;


/****************************************************************************
  create a unique policy handle
****************************************************************************/
static void create_pol_hnd(POLICY_HND *hnd)
{
	static uint32 pol_hnd_low  = 0;
	static uint32 pol_hnd_high = 0;

	if (hnd == NULL) return;

	/* i severely doubt that pol_hnd_high will ever be non-zero... */
	pol_hnd_low++;
	if (pol_hnd_low == 0) pol_hnd_high++;

	SIVAL(hnd->data, 0 , 0x0);  /* first bit must be null */
	SIVAL(hnd->data, 4 , pol_hnd_low ); /* second bit is incrementing */
	SIVAL(hnd->data, 8 , pol_hnd_high); /* second bit is incrementing */
	SIVAL(hnd->data, 12, time(NULL)); /* something random */
	SIVAL(hnd->data, 16, getpid()); /* something more random */
}

/****************************************************************************
  initialise policy handle states...
****************************************************************************/
BOOL init_policy_hnd(int num_pol_hnds)
{
	bmap = bitmap_allocate(num_pol_hnds);
	
	return bmap != NULL;
}

/****************************************************************************
  find first available policy slot.  creates a policy handle for you.
****************************************************************************/
BOOL open_policy_hnd(POLICY_HND *hnd)
{
	int i;
	struct policy *p;

	i = bitmap_find(bmap, 1);

	if (i == -1) {
		DEBUG(0,("ERROR: out of Policy Handles!\n"));
		return False;
	}

	p = (struct policy *)malloc(sizeof(*p));
	if (!p) {
		DEBUG(0,("ERROR: out of memory!\n"));
		return False;
	}

	ZERO_STRUCTP(p);

	p->open = True;				
	p->pnum = i;
	p->type = POL_NO_INFO;

	create_pol_hnd(hnd);
	memcpy(&p->pol_hnd, hnd, sizeof(*hnd));

	bitmap_set(bmap, i);

	DLIST_ADD(Policy, p);
	
	DEBUG(4,("Opened policy hnd[%x] ", i));
	dump_data(4, (char *)hnd->data, sizeof(hnd->data));

	return True;
}

/****************************************************************************
  find policy by handle
****************************************************************************/
static struct policy *find_policy(POLICY_HND *hnd)
{
	struct policy *p;

	for (p=Policy;p;p=p->next) {
		if (memcmp(&p->pol_hnd, hnd, sizeof(*hnd)) == 0) {
			DEBUG(4,("Found policy hnd[%x] ", p->pnum));
			dump_data(4, (char *)hnd->data, sizeof(hnd->data));
			return p;
		}
	}

	DEBUG(4,("Policy not found: "));
	dump_data(4, (char *)hnd->data, sizeof(hnd->data));

	return NULL;
}

/****************************************************************************
  find policy index by handle
****************************************************************************/
int find_policy_by_hnd(POLICY_HND *hnd)
{
	struct policy *p = find_policy(hnd);

	return p?p->pnum:-1;
}

/****************************************************************************
  set samr rid
****************************************************************************/
BOOL set_policy_samr_rid(POLICY_HND *hnd, uint32 rid)
{
	struct policy *p = find_policy(hnd);

	if (p && p->open)
	{
		DEBUG(3,("Setting policy device rid=%x pnum=%x\n",
			 rid, p->pnum));

		if (p->dev.samr == NULL)
		{
			p->dev.samr = (struct samr_info*)malloc(sizeof(*p->dev.samr));
		}
		if (p->dev.samr == NULL)
		{
			return False;
		}
		p->dev.samr->rid = rid;
		return True;
	}

	DEBUG(3,("Error setting policy rid=%x\n",rid));
	return False;
}


/****************************************************************************
  set samr pol status.  absolutely no idea what this is.
****************************************************************************/
BOOL set_policy_samr_pol_status(POLICY_HND *hnd, uint32 pol_status)
{
	struct policy *p = find_policy(hnd);

	if (p && p->open)
	{
		DEBUG(3,("Setting policy status=%x pnum=%x\n",
		          pol_status, p->pnum));

		if (p->dev.samr == NULL)
		{
			p->type = POL_SAMR_INFO;
			p->dev.samr = (struct samr_info*)malloc(sizeof(*p->dev.samr));
		}
		if (p->dev.samr == NULL)
		{
			return False;
		}
		p->dev.samr->status = pol_status;
		return True;
	} 

	DEBUG(3,("Error setting policy status=%x\n",
		 pol_status));
	return False;
}

/****************************************************************************
  set samr sid
****************************************************************************/
BOOL set_policy_samr_sid(POLICY_HND *hnd, DOM_SID *sid)
{
	pstring sidstr;
	struct policy *p = find_policy(hnd);

	if (p && p->open) {
		DEBUG(3,("Setting policy sid=%s pnum=%x\n",
			 sid_to_string(sidstr, sid), p->pnum));

		if (p->dev.samr == NULL)
		{
			p->type = POL_SAMR_INFO;
			p->dev.samr = (struct samr_info*)malloc(sizeof(*p->dev.samr));
		}
		if (p->dev.samr == NULL)
		{
			return False;
		}
		memcpy(&p->dev.samr->sid, sid, sizeof(*sid));
		return True;
	}

	DEBUG(3,("Error setting policy sid=%s\n",
		  sid_to_string(sidstr, sid)));
	return False;
}

/****************************************************************************
  get samr sid
****************************************************************************/
BOOL get_policy_samr_sid(POLICY_HND *hnd, DOM_SID *sid)
{
	struct policy *p = find_policy(hnd);

	if (p != NULL && p->open)
	{
		pstring sidstr;
		memcpy(sid, &p->dev.samr->sid, sizeof(*sid));
		DEBUG(3,("Getting policy sid=%s pnum=%x\n",
			 sid_to_string(sidstr, sid), p->pnum));

		return True;
	}

	DEBUG(3,("Error getting policy\n"));
	return False;
}

/****************************************************************************
  get samr rid
****************************************************************************/
uint32 get_policy_samr_rid(POLICY_HND *hnd)
{
	struct policy *p = find_policy(hnd);

	if (p && p->open) {
		uint32 rid = p->dev.samr->rid;
		DEBUG(3,("Getting policy device rid=%x pnum=%x\n",
		          rid, p->pnum));

		return rid;
	}

	DEBUG(3,("Error getting policy\n"));
	return 0xffffffff;
}

/****************************************************************************
  set reg name 
****************************************************************************/
BOOL set_policy_reg_name(POLICY_HND *hnd, fstring name)
{
	struct policy *p = find_policy(hnd);

	if (p && p->open)
	{
		DEBUG(3,("Getting policy pnum=%x\n",
			 p->pnum));

		if (p->dev.reg == NULL)
		{
			p->type = POL_REG_INFO;
			p->dev.reg = (struct reg_info*)malloc(sizeof(*p->dev.reg));
		}
		if (p->dev.reg == NULL)
		{
			return False;
		}
		fstrcpy(p->dev.reg->name, name);
		return True;
	}

	DEBUG(3,("Error setting policy name=%s\n", name));
	return False;
}

/****************************************************************************
  set reg name 
****************************************************************************/
BOOL get_policy_reg_name(POLICY_HND *hnd, fstring name)
{
	struct policy *p = find_policy(hnd);

	if (p && p->open)
	{
		DEBUG(3,("Setting policy pnum=%x name=%s\n",
			 p->pnum, name));

		fstrcpy(name, p->dev.reg->name);
		DEBUG(5,("getting policy reg name=%s\n", name));
		return True;
	}

	DEBUG(3,("Error getting policy reg name\n"));
	return False;
}

/****************************************************************************
  set cli state
****************************************************************************/
BOOL set_policy_cli_state(POLICY_HND *hnd, struct cli_state *cli, uint16 fnum,
				void (*free_fn)(struct cli_state *, uint16))
{
	struct policy *p = find_policy(hnd);

	if (p && p->open)
	{
		DEBUG(3,("Setting policy cli state pnum=%x\n", p->pnum));

		if (p->dev.cli == NULL)
		{
			p->type = POL_CLI_INFO;
			p->dev.cli = (struct cli_info*)malloc(sizeof(*p->dev.cli));
		}
		if (p->dev.cli == NULL)
		{
			return False;
		}
		p->dev.cli->cli  = cli;
		p->dev.cli->free = free_fn;
		p->dev.cli->fnum = fnum;
		return True;
	}

	DEBUG(3,("Error setting policy cli state\n"));

	return False;
}

/****************************************************************************
  get cli state
****************************************************************************/
BOOL get_policy_cli_state(POLICY_HND *hnd, struct cli_state **cli, uint16 *fnum)
{
	struct policy *p = find_policy(hnd);

	if (p != NULL && p->open)
	{
		DEBUG(3,("Getting cli state pnum=%x\n", p->pnum));

		(*cli ) = p->dev.cli->cli;
		(*fnum) = p->dev.cli->fnum;

		return True;
	}

	DEBUG(3,("Error getting policy\n"));
	return False;
}

/****************************************************************************
  close an lsa policy
****************************************************************************/
BOOL close_policy_hnd(POLICY_HND *hnd)
{
	struct policy *p = find_policy(hnd);

	if (!p)
	{
		DEBUG(3,("Error closing policy\n"));
		return False;
	}

	DEBUG(3,("Closed policy name pnum=%x\n",  p->pnum));

	DLIST_REMOVE(Policy, p);

	bitmap_clear(bmap, p->pnum);

	ZERO_STRUCTP(p);
	ZERO_STRUCTP(hnd);

	switch (p->type)
	{
		case POL_REG_INFO:
		{
			free(p->dev.reg);
			break;
		}
		case POL_SAMR_INFO:
		{
			free(p->dev.samr);
			break;
		}
		case POL_CLI_INFO:
		{
			if (p->dev.cli->free != NULL)
			{
				p->dev.cli->free(p->dev.cli->cli,
				                 p->dev.cli->fnum);
			}
			break;
		}
	}

	free(p);

	return True;
}

