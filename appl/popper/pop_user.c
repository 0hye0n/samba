/*
 * Copyright (c) 1989 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#include <popper.h>
RCSID("$Id$");

/* 
 *  user:   Prompt for the user name at the start of a POP session
 */

int
pop_user (POP *p)
{
    char ss[256];

    strcpy(p->user, p->pop_parm[1]);

    if (otp_challenge (&p->otp_ctx, p->user, ss, sizeof(ss)) == 0) {
	return pop_msg(p, POP_SUCCESS, "Password %s required for %s.",
		       ss, p->user);
    } else if (p->no_passwd) {
	char *s = otp_error(&p->otp_ctx);
	return pop_msg(p, POP_FAILURE, "Permission denied%s%s",
		       s ? ":" : "", s);
    } else
	return pop_msg(p, POP_SUCCESS, "Password required for %s.", p->user);
}
