/* 
   Unix SMB/Netbios implementation.
   Version 3.0
   client error handling routines
   Copyright (C) Andrew Tridgell 1994-1998
   
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

#define NO_SYSLOG

#include "includes.h"

extern int DEBUGLEVEL;

/*****************************************************
 RAP error codes - a small start but will be extended.
*******************************************************/

static struct
{
  int err;
  char *message;
} rap_errmap[] =
{
  {5,    "User has insufficient privilege" },
  {86,   "The specified password is invalid" },
  {2226, "Operation only permitted on a Primary Domain Controller"  },
  {2242, "The password of this user has expired." },
  {2243, "The password of this user cannot change." },
  {2244, "This password cannot be used now (password history conflict)." },
  {2245, "The password is shorter than required." },
  {2246, "The password of this user is too recent to change."},

  /* these really shouldn't be here ... */
  {0x80, "Not listening on called name"},
  {0x81, "Not listening for calling name"},
  {0x82, "Called name not present"},
  {0x83, "Called name present, but insufficient resources"},

  {0, NULL}
};  

/****************************************************************************
  return a description of an SMB error
****************************************************************************/
static char *cli_smb_errstr(struct cli_state *cli)
{
	return smb_errstr(cli->inbuf);
}

/***************************************************************************
 Return an error message - either an NT error, SMB error or a RAP error.
 Note some of the NT errors are actually warnings or "informational" errors
 in which case they can be safely ignored.
****************************************************************************/
    
char *cli_errstr(struct cli_state *cli)
{   
	static fstring error_message;
	uint32 flgs2 = SVAL(cli->inbuf,smb_flg2), errnum;
        uint8 errclass;
        int i;

        /* Case #1: 32-bit NT errors */
	if (flgs2 & FLAGS2_32_BIT_ERROR_CODES) {
                uint32 status = IVAL(cli->inbuf,smb_rcls);

                return get_nt_error_msg(status);
        }

        cli_dos_error(cli, &errclass, &errnum);

        /* Case #2: SMB error */

        if (errclass != 0)
                return cli_smb_errstr(cli);

        /* Case #3: RAP error */
	for (i = 0; rap_errmap[i].message != NULL; i++) {
		if (rap_errmap[i].err == cli->rap_error) {
			return rap_errmap[i].message;
		}
	} 

	slprintf(error_message, sizeof(error_message) - 1, "code %d", 
                 cli->rap_error);

	return error_message;
}


/* Return the 32-bit NT status code from the last packet */
uint32 cli_nt_error(struct cli_state *cli)
{
        int flgs2 = SVAL(cli->inbuf,smb_flg2);

	if (!(flgs2 & FLAGS2_32_BIT_ERROR_CODES)) {
		int class  = CVAL(cli->inbuf,smb_rcls);
		int code  = SVAL(cli->inbuf,smb_err);
		return dos_to_ntstatus(class, code);
        }

        return IVAL(cli->inbuf,smb_rcls);
}


/* Return the DOS error from the last packet - an error class and an error
   code. */
void cli_dos_error(struct cli_state *cli, uint8 *eclass, uint32 *ecode)
{
	int  flgs2;
	char rcls;
	int code;

	if(!cli->initialised) return;

	flgs2 = SVAL(cli->inbuf,smb_flg2);

	if (flgs2 & FLAGS2_32_BIT_ERROR_CODES) {
		uint32 ntstatus = IVAL(cli->inbuf, smb_rcls);
		ntstatus_to_dos(ntstatus, eclass, ecode);
                return;
        }

	rcls  = CVAL(cli->inbuf,smb_rcls);
	code  = SVAL(cli->inbuf,smb_err);

	if (eclass) *eclass = rcls;
	if (ecode) *ecode    = code;
}

/* Return a UNIX errno from a dos error class, error number tuple */

int cli_errno_from_dos(uint8 eclass, uint32 num)
{
	if (eclass == ERRDOS) {
		switch (num) {
		case ERRbadfile: return ENOENT;
		case ERRbadpath: return ENOTDIR;
		case ERRnoaccess: return EACCES;
		case ERRfilexists: return EEXIST;
		case ERRrename: return EEXIST;
		case ERRbadshare: return EBUSY;
		case ERRlock: return EBUSY;
		case ERRinvalidname: return ENOENT;
		case ERRnosuchshare: return ENODEV;
		}
	}

	if (eclass == ERRSRV) {
		switch (num) {
		case ERRbadpw: return EPERM;
		case ERRaccess: return EACCES;
		case ERRnoresource: return ENOMEM;
		case ERRinvdevice: return ENODEV;
		case ERRinvnetname: return ENODEV;
		}
	}

	/* for other cases */
	return EINVAL;
}

/* Return a UNIX errno from a NT status code */

int cli_errno_from_nt(uint32 status)
{
        DEBUG(10,("cli_errno_from_nt: 32 bit codes: code=%08x\n", status));

        /* Status codes without this bit set are not errors */

        if (!(status & 0xc0000000))
                return 0;

        switch (status) {
        case NT_STATUS_ACCESS_VIOLATION: return EACCES;
        case NT_STATUS_NO_SUCH_FILE: return ENOENT;
        case NT_STATUS_NO_SUCH_DEVICE: return ENODEV;
        case NT_STATUS_INVALID_HANDLE: return EBADF;
        case NT_STATUS_NO_MEMORY: return ENOMEM;
        case NT_STATUS_ACCESS_DENIED: return EACCES;
        case NT_STATUS_OBJECT_NAME_NOT_FOUND: return ENOENT;
        case NT_STATUS_SHARING_VIOLATION: return EBUSY;
        case NT_STATUS_OBJECT_PATH_INVALID: return ENOTDIR;
        case NT_STATUS_OBJECT_NAME_COLLISION: return EEXIST;
        case NT_STATUS_PATH_NOT_COVERED: return ENOENT;
        }

        /* for all other cases - a default code */
        return EINVAL;
}

/* Return a UNIX errno appropriate for the error received in the last
   packet. */

int cli_errno(struct cli_state *cli)
{
        uint32 status;

        if (cli_is_dos_error) {
                uint8 eclass;
                uint32 ecode;

                cli_dos_error(cli, &eclass, &ecode);
                return cli_errno_from_dos(eclass, ecode);
        }

        status = cli_nt_error(cli);

        return cli_errno_from_nt(status);
}

/* Return true if the last packet was in error */

BOOL cli_is_error(struct cli_state *cli)
{
	uint32 flgs2 = SVAL(cli->inbuf,smb_flg2), rcls = 0;

        if (flgs2 & FLAGS2_32_BIT_ERROR_CODES) {
                /* Return error is error bits are set */
                rcls = IVAL(cli->inbuf, smb_rcls);
                return (rcls & 0xF0000000) == 0xC0000000;
        }
                
        /* Return error if error class in non-zero */

        rcls = CVAL(cli->inbuf, smb_rcls);
        return rcls != 0;
}

/* Return true if the last error was an NT error */

BOOL cli_is_nt_error(struct cli_state *cli)
{
	uint32 flgs2 = SVAL(cli->inbuf,smb_flg2);

        return cli_is_error(cli) && (flgs2 & FLAGS2_32_BIT_ERROR_CODES);
}

/* Return true if the last error was a DOS error */

BOOL cli_is_dos_error(struct cli_state *cli)
{
	uint32 flgs2 = SVAL(cli->inbuf,smb_flg2);

        return cli_is_error(cli) && !(flgs2 & FLAGS2_32_BIT_ERROR_CODES);
}

