/* 
   Unix SMB/Netbios implementation.
   Version 3.0
   printing command routines
   Copyright (C) Andrew Tridgell 1992-2000
   
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

#include "printing.h"


/*
 * Generic printing interface definitions...
 */

static int generic_job_delete(int snum, struct printjob *pjob);
static int generic_job_pause(int snum, struct printjob *pjob);
static int generic_job_resume(int snum, struct printjob *pjob);
static int generic_job_submit(int snum, struct printjob *pjob);
static int generic_queue_get(int snum, print_queue_struct **q,
                             print_status_struct *status);
static int generic_queue_pause(int snum);
static int generic_queue_resume(int snum);


struct printif	generic_printif =
		{
		  generic_queue_get,
		  generic_queue_pause,
		  generic_queue_resume,
		  generic_job_delete,
		  generic_job_pause,
		  generic_job_resume,
		  generic_job_submit,
		};

extern int DEBUGLEVEL;

/****************************************************************************
run a given print command 
a null terminated list of value/substitute pairs is provided
for local substitution strings
****************************************************************************/
static int print_run_command(int snum,char *command, 
			     char *outfile,
			     ...)
{
	pstring syscmd;
	char *p, *arg;
	int ret;
	va_list ap;

	if (!command || !*command) return -1;

	if (!VALID_SNUM(snum)) {
		DEBUG(0,("Invalid snum %d for command %s\n", snum, command));
		return -1;
	}

	pstrcpy(syscmd, command);

	va_start(ap, outfile);
	while ((arg = va_arg(ap, char *))) {
		char *value = va_arg(ap,char *);
		pstring_sub(syscmd, arg, value);
	}
	va_end(ap);
  
	p = PRINTERNAME(snum);
  
	pstring_sub(syscmd, "%p", p);
	standard_sub_snum(snum,syscmd);

	/* Convert script args to unix-codepage */
	dos_to_unix(syscmd, True);
	ret = smbrun(syscmd,outfile,False);

	DEBUG(3,("Running the command `%s' gave %d\n",syscmd,ret));

	return ret;
}


/****************************************************************************
delete a print job
****************************************************************************/
static int generic_job_delete(int snum, struct printjob *pjob)
{
	fstring jobstr;

	/* need to delete the spooled entry */
	slprintf(jobstr, sizeof(jobstr)-1, "%d", pjob->sysjob);
	return print_run_command(
		   snum, 
		   lp_lprmcommand(snum), NULL,
		   "%j", jobstr,
		   "%T", http_timestring(pjob->starttime),
		   NULL);
}

/****************************************************************************
pause a job
****************************************************************************/
static int generic_job_pause(int snum, struct printjob *pjob)
{
	fstring jobstr;
	
	/* need to pause the spooled entry */
	slprintf(jobstr, sizeof(jobstr)-1, "%d", pjob->sysjob);
	return print_run_command(snum, 
				 lp_lppausecommand(snum), NULL,
				 "%j", jobstr,
				 NULL);
}

/****************************************************************************
resume a job
****************************************************************************/
static int generic_job_resume(int snum, struct printjob *pjob)
{
	fstring jobstr;
	
	/* need to pause the spooled entry */
	slprintf(jobstr, sizeof(jobstr)-1, "%d", pjob->sysjob);
	return print_run_command(snum, 
				 lp_lpresumecommand(snum), NULL,
				 "%j", jobstr,
				 NULL);
}

/****************************************************************************
 Submit a file for printing - called from print_job_end()
****************************************************************************/

static int generic_job_submit(int snum, struct printjob *pjob)
{
	int ret;
	pstring current_directory;
	pstring print_directory;
	char *wd, *p;
	pstring jobname;

	/* we print from the directory path to give the best chance of
           parsing the lpq output */
	wd = sys_getwd(current_directory);
	if (!wd)
		return 0;

	pstrcpy(print_directory, pjob->filename);
	p = strrchr(print_directory,'/');
	if (!p)
		return 0;
	*p++ = 0;

	if (chdir(print_directory) != 0)
		return 0;

	pstrcpy(jobname, pjob->jobname);
	pstring_sub(jobname, "'", "_");

	/* send it to the system spooler */
	ret = print_run_command(snum, 
			  lp_printcommand(snum), NULL,
			  "%s", p,
  			  "%J", jobname,
			  "%f", p,
			  NULL);

	chdir(wd);

        return ret;
}


/****************************************************************************
get the current list of queued jobs
****************************************************************************/
static int generic_queue_get(int snum, print_queue_struct **q, print_status_struct *status)
{
	char *path = lp_pathname(snum);
	char *cmd = lp_lpqcommand(snum);
	char **qlines;
	pstring tmp_file;
	int numlines, i, qcount;
	print_queue_struct *queue;
	fstring printer_name;
              
	/* Convert printer name (i.e. share name) to unix-codepage */
	fstrcpy(printer_name, lp_servicename(snum));
	dos_to_unix(printer_name, True);
	
	slprintf(tmp_file, sizeof(tmp_file)-1, "%s/smblpq.%d", path, sys_getpid());

	unlink(tmp_file);
	print_run_command(snum, cmd, tmp_file, NULL);

	numlines = 0;
	qlines = file_lines_load(tmp_file, &numlines, True);
	unlink(tmp_file);

	/* turn the lpq output into a series of job structures */
	qcount = 0;
	ZERO_STRUCT(status);
	if (numlines)
		queue = (print_queue_struct *)malloc(sizeof(print_queue_struct)*(numlines+1));

	if (queue) {
		for (i=0; i<numlines; i++) {
			/* parse the line */
			if (parse_lpq_entry(snum,qlines[i],
					    &queue[qcount],status,qcount==0)) {
				qcount++;
			}
		}		
	}
	file_lines_free(qlines);

        *q = queue;
	return qcount;
}

/****************************************************************************
 pause a queue
****************************************************************************/
static int generic_queue_pause(int snum)
{
	return print_run_command(snum, lp_queuepausecommand(snum), NULL, 
				 NULL);
}

/****************************************************************************
 resume a queue
****************************************************************************/
static int generic_queue_resume(int snum)
{
	return print_run_command(snum, lp_queueresumecommand(snum), NULL, 
				 NULL);
}
