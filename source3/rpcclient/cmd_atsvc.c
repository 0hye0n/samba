/* 
   Unix SMB/Netbios implementation.
   Version 2.1.
   MSRPC client: scheduler service
   Copyright (C) Matthew Chapman 1999
   Copyright (C) Luke Kenneth Casson Leighton 1996-1999
   Copyright (C) Andrew Tridgell 1994-1999
   
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


#ifdef SYSLOG
#undef SYSLOG
#endif

#include "includes.h"
#include "nterr.h"

extern int DEBUGLEVEL;

#define DEBUG_TESTING

extern struct cli_state *smb_cli;
extern FILE* out_hnd;


/****************************************************************************
checks for a /OPTION:param style option
****************************************************************************/
static BOOL checkopt(char *input, char *optname, char **params)
{
	char *inend;

	if (*input++ != '/')
		return False;

	for (inend = input; *inend != 0 && *inend != ':'; inend++);

	if (params != NULL)
	{
		*inend = 0;
		*params = inend;
	}

	while(input < inend)
	{
		if (toupper(*input++) != *optname++)
			return False;
	}

	return True;
}

extern char *daynames_short[];
extern char *daynames[];

/****************************************************************************
parses a list of days of the week and month
****************************************************************************/
static BOOL at_parse_days(char *str, uint32 *monthdays, uint8 *weekdays)
{
	char *tok;
	char *nexttok = str;
	int day;

	do {
		tok = nexttok;

		if ((nexttok = strchr(tok, ',')) != NULL)
		{
			*nexttok++ = 0;
		}

		if (isdigit((int)*tok))
		{
			day = strtol(tok, NULL, 10);
			if (day == 0 || day > 31)
			{
				printf("\tInvalid day of month.\n\n");
				return False;
			}

			*monthdays |= (1 << (day-1));
		}
		else
		{
			if (strlen(tok) < 3)
			{
				for (day = 0; day < 7; day++)
				{
					if (!strcasecmp(tok, daynames_short[day]))
						break;
				}
			}
			else
			{
				for (day = 0; day < 7; day++)
				{
					if (!strncasecmp(tok, daynames[day], 3))
						break;
				}
			}

			if (day < 7)
			{
				*weekdays |= (1 << day);
			}
			else
			{
				printf("\tInvalid day of week\n\n");
				return False;
			}
		}

	} while (nexttok != NULL);

	return True;
}

#define SOON_OFFSET 2 /* seconds */

/****************************************************************************
schedule the job 'soon'
****************************************************************************/
static BOOL at_soon(char *dest_srv, uint32 *hours, uint32 *minutes, uint32 *seconds)
{
	uint16 nt_pipe_fnum;
	TIME_OF_DAY_INFO tod;
	BOOL res = True;

	/* open srvsvc session. */
	res = res ? cli_nt_session_open(smb_cli, PIPE_SRVSVC, &nt_pipe_fnum) : False;

	/* enumerate files on server */
	res = res ? do_srv_net_remote_tod(smb_cli, nt_pipe_fnum,
					  dest_srv, &tod) : False;

	/* Close the session */
	cli_nt_session_close(smb_cli, nt_pipe_fnum);

	if (res)
	{
		*hours = (tod.hours - ((int)tod.zone/60)) % 24;
		*minutes = tod.mins;
		*seconds = (tod.secs + SOON_OFFSET) % 60;
		return True;
	}

	return False;
}


/****************************************************************************
scheduler add job
****************************************************************************/
void cmd_at(struct client_info *info)
{
	uint16 nt_pipe_fnum;
	pstring temp;
	fstring dest_wks;
	BOOL add = False;
	BOOL del = False;
	char *p;

	uint32 jobid = -1;
	unsigned int hours, minutes, seconds = 0;
	uint32 monthdays = 0;
	uint8 weekdays = 0;
	uint8 flags = JOB_NONINTERACTIVE;
	pstring command;

        while (next_token(NULL, temp, NULL, sizeof(temp)))
	{
		if (checkopt(temp, "DELETE", NULL))
		{
			del = True;
			continue;
		}
		else if (checkopt(temp, "YES", NULL))
		{
			/* Compatibility */
			continue;
		}

		jobid = strtol(temp, &p, 10);
		if (*p == 0)   /* Entirely numeric field */
			continue;

		if (!strcasecmp(temp, "NOW"))
		{
			if (!at_soon(dest_wks, &hours, &minutes, &seconds))
			{
				return;
			}
		}
		else if (sscanf(temp, "%d:%d:%d", &hours, &minutes, &seconds) > 2)
		{
			p = strchr(temp, 0);

			if (!strcasecmp(p-2, "AM"))
			{
				hours = (hours == 12) ? 0 : hours;
			}

			if (!strcasecmp(p-2, "PM"))
			{
				hours = (hours == 12) ? 12 : hours + 12;
			}

			if (hours > 23 || minutes > 59 || seconds > 59)
			{
				printf("\tInvalid time.\n\n");
				return;
			}
		}
		else
		{
			printf("at { {time | NOW} [/INTERACTIVE] [{/EVERY|/NEXT}:5,Sun,...] command\n\t| [/DEL] [jobid] }\n\n");
			return;
		}

		add = True;
		command[0] = 0;
		p = NULL;

		if (!next_token(NULL, temp, NULL, sizeof(temp)))
			break;

		if (checkopt(temp, "INTERACTIVE", NULL))
		{
			flags &= ~JOB_NONINTERACTIVE;

			if (!next_token(NULL, temp, NULL, sizeof(temp)))
				break;
		}

		if (checkopt(temp, "EVERY", &p))
		{
			flags |= JOB_PERIODIC;
		}
		else
		{
			checkopt(temp, "NEXT", &p);
		}

		if (p != NULL)
		{
			if (*p == ':')
			{
				if (!at_parse_days(p, &monthdays, &weekdays))
					return;
			}
			else
			{
				weekdays = 0x7F;
			}

			if (!next_token(NULL, temp, NULL, sizeof(temp)))
				break;
		}

		while (True)
		{
			safe_strcat(command, temp, sizeof(command));

			if (!next_token(NULL, temp, NULL, sizeof(temp)))
				break;

			safe_strcat(command, " ", sizeof(command));
		}

		break;
	}

	if (add && !command[0])
	{
		printf("\tNo command specified.\n\n");
		return;
	}

	safe_strcpy(dest_wks, "\\\\", sizeof(dest_wks));
	safe_strcat(dest_wks, info->dest_host, sizeof(dest_wks));
	strupper(dest_wks);

	/* open scheduler session. */
	if (!cli_nt_session_open(smb_cli, PIPE_ATSVC, &nt_pipe_fnum))
		return;

	if (add) /* add job */
	{
		AT_JOB_INFO job;

		job.time = ((((hours * 60) + minutes) * 60) + seconds) * 1000;
		job.monthdays = monthdays;
		job.weekdays = weekdays;
		job.flags = flags;
		job.ptr_command = 1;

		display_at_job_info(out_hnd, ACTION_HEADER   , &job, command);
		display_at_job_info(out_hnd, ACTION_ENUMERATE, &job, command);
		display_at_job_info(out_hnd, ACTION_FOOTER   , &job, command);

		if (at_add_job(smb_cli, nt_pipe_fnum, dest_wks, &job,
				     command, &jobid))
		{
			fprintf(out_hnd, "\tJob ID:      %d\n\n", jobid);
		}
	}
	else if (del) /* delete */
	{
		if (jobid == -1)
		{
			fprintf(out_hnd, "\tDeleting all jobs.\n\n");
			at_del_job(smb_cli, nt_pipe_fnum, dest_wks,
				   0, 0xffffffff);
		}
		else
		{
			fprintf(out_hnd, "\tDeleting job %d.\n\n", jobid);
			at_del_job(smb_cli, nt_pipe_fnum, dest_wks,
				   jobid, jobid);
		}

	}
	else if (jobid == -1) /* enumerate */
	{
		AT_ENUM_INFO jobs[AT_MAX_JOBS];
		fstring commands[AT_MAX_JOBS];
		uint32 num_jobs;

		if (at_enum_jobs(smb_cli, nt_pipe_fnum, dest_wks, &num_jobs,
			     jobs, commands))
		{
			display_at_enum_info(out_hnd, ACTION_HEADER   , num_jobs, jobs, commands);
			display_at_enum_info(out_hnd, ACTION_ENUMERATE, num_jobs, jobs, commands);
			display_at_enum_info(out_hnd, ACTION_FOOTER   , num_jobs, jobs, commands);
		}
	}
	else /* job info */
	{
		AT_JOB_INFO job;
		
		if (at_query_job(smb_cli, nt_pipe_fnum, dest_wks, jobid, &job, command))
		{
			display_at_job_info(out_hnd, ACTION_HEADER   , &job, command);
			display_at_job_info(out_hnd, ACTION_ENUMERATE, &job, command);
			display_at_job_info(out_hnd, ACTION_FOOTER   , &job, command);
		}
	}

	/* close the session */
	cli_nt_session_close(smb_cli, nt_pipe_fnum);
}
