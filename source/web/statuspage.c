/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   web status page
   Copyright (C) Andrew Tridgell 1997-1998
   
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


static char *tstring(time_t t)
{
	static pstring buf;
	pstrcpy(buf, asctime(LocalTime(&t)));
	string_sub(buf," ","&nbsp;");
	return buf;
}

static void print_share_mode(share_mode_entry *e, char *fname)
{
	printf("<tr><td>%d</td>",e->pid);
	printf("<td>");
	switch ((e->share_mode>>4)&0xF) {
	case DENY_NONE: printf("DENY_NONE"); break;
	case DENY_ALL:  printf("DENY_ALL   "); break;
	case DENY_DOS:  printf("DENY_DOS   "); break;
	case DENY_READ: printf("DENY_READ  "); break;
	case DENY_WRITE:printf("DENY_WRITE "); break;
	}
	printf("</td>");

	printf("<td>");
	switch (e->share_mode&0xF) {
	case 0: printf("RDONLY     "); break;
	case 1: printf("WRONLY     "); break;
	case 2: printf("RDWR       "); break;
	}
	printf("</td>");

	printf("<td>");
	if((e->op_type & 
	    (EXCLUSIVE_OPLOCK|BATCH_OPLOCK)) == 
	   (EXCLUSIVE_OPLOCK|BATCH_OPLOCK))
		printf("EXCLUSIVE+BATCH ");
	else if (e->op_type & EXCLUSIVE_OPLOCK)
		printf("EXCLUSIVE       ");
	else if (e->op_type & BATCH_OPLOCK)
		printf("BATCH           ");
	else
		printf("NONE            ");
	printf("</td>");

	printf("<td>%s</td><td>%s</td></tr>\n",
	       fname,tstring(e->time.tv_sec));
}


/* show the current server status */
void status_page(void)
{
	struct connect_record crec;
	pstring fname;
	FILE *f;
	char *v;
	int autorefresh=0;
	int refresh_interval=30;

	if (cgi_variable("smbd_start")) {
		start_smbd();
	}

	if (cgi_variable("smbd_stop")) {
		stop_smbd();
	}

	if (cgi_variable("nmbd_start")) {
		start_nmbd();
	}

	if (cgi_variable("nmbd_stop")) {
		stop_nmbd();
	}

	if (cgi_variable("autorefresh")) {
		autorefresh = 1;
	} else if (cgi_variable("norefresh")) {
		autorefresh = 0;
	} else if (cgi_variable("refresh")) {
		autorefresh = 1;
	}

	if ((v=cgi_variable("refresh_interval"))) {
		refresh_interval = atoi(v);
	}

	if (autorefresh) {
		printf("<META HTTP-EQUIV=refresh CONTENT=\"%d;URL=%s/status?refresh=1&refresh_interval=%d\">\n", 
		       refresh_interval, cgi_baseurl(), refresh_interval);
	}

	pstrcpy(fname,lp_lockdir());
	standard_sub_basic(fname);
	trim_string(fname,"","/");
	strcat(fname,"/STATUS..LCK");


	f = fopen(fname,"r");
	if (f) {
		while (!feof(f)) {
			if (fread(&crec,sizeof(crec),1,f) != 1)	break;
			if (crec.magic == 0x280267 && crec.cnum == -1 &&
			    process_exists(crec.pid)) {
				char buf[30];
				sprintf(buf,"kill_%d", crec.pid);
				if (cgi_variable(buf)) {
					kill_pid(crec.pid);
				}
			}
		}
		fclose(f);
	}

	printf("<H2>Server Status</H2>\n");

	printf("<FORM method=post>\n");

	if (!autorefresh) {
		printf("<input type=submit value=\"Auto Refresh\" name=autorefresh>\n");
		printf("<br>Refresh Interval: ");
		printf("<input type=text size=2 name=\"refresh_interval\" value=%d>\n", 
		       refresh_interval);
	} else {
		printf("<input type=submit value=\"Stop Refreshing\" name=norefresh>\n");
		printf("<br>Refresh Interval: %d\n", refresh_interval);
		printf("<input type=hidden name=refresh value=1>\n");
	}

	printf("<p>\n");

	f = fopen(fname,"r");
	if (!f) {
		printf("Couldn't open status file %s\n",fname);
		if (!lp_status(-1))
			printf("You need to have status=yes in your smb config file\n");
		return;
	}


	printf("<table>\n");

	printf("<tr><td>version:</td><td>%s</td></tr>",VERSION);

	fflush(stdout);
	if (smbd_running()) {
		printf("<tr><td>smbd:</td><td>running</td><td><input type=submit name=\"smbd_stop\" value=\"Stop smbd\"></td></tr>\n");
	} else {
		printf("<tr><td>smbd:</td><td>not running</td><td><input type=submit name=\"smbd_start\" value=\"Start smbd\"></td></tr>\n");
	}

	fflush(stdout);
	if (nmbd_running()) {
		printf("<tr><td>nmbd:</td><td>running</td><td><input type=submit name=\"nmbd_stop\" value=\"Stop nmbd\"></td></tr>\n");
	} else {
		printf("<tr><td>nmbd:</td><td>not running</td><td><input type=submit name=\"nmbd_start\" value=\"Start nmbd\"></td></tr>\n");
	}

	printf("</table>\n");
	fflush(stdout);


	if (geteuid() != 0)
		printf("<b>NOTE: You are not logged in as root and won't be able to start/stop the server</b><p>\n");

	printf("<p><h3>Active Connections</h3>\n");
	printf("<table border=1>\n");
	printf("<tr><th>PID</th><th>Client</th><th>IP address</th><th>Date</th><th>Kill</th></tr>\n");

	while (!feof(f)) {
		if (fread(&crec,sizeof(crec),1,f) != 1)
			break;
		if (crec.magic == 0x280267 && 
		    crec.cnum == -1 &&
		    process_exists(crec.pid)) {
			printf("<tr><td>%d</td><td>%s</td><td>%s</td><td>%s</td><td><input type=submit value=\"X\" name=\"kill_%d\"></td></tr>\n",
			       crec.pid,
			       crec.machine,crec.addr,
			       tstring(crec.start),
			       crec.pid);
		}
	}

	printf("</table><p>\n");

	fseek(f, 0, SEEK_SET);
	
	printf("<p><h3>Active Shares</h3>\n");
	printf("<table border=1>\n");
	printf("<tr><th>Share</th><th>User</th><th>Group</th><th>PID</th><th>Client</th><th>Date</th></tr>\n\n");

	while (!feof(f)) {
		if (fread(&crec,sizeof(crec),1,f) != 1)
			break;
		if (crec.cnum == -1) continue;
		if (crec.magic == 0x280267 && process_exists(crec.pid)) {
			printf("<tr><td>%s</td><td>%s</td><td>%s</td><td>%d</td><td>%s</td><td>%s</td></tr>\n",
			       crec.name,uidtoname(crec.uid),
			       gidtoname(crec.gid),crec.pid,
			       crec.machine,
			       tstring(crec.start));
		}
	}

	printf("</table><p>\n");

	printf("<h3>Open Files</h3>\n");
	printf("<table border=1>\n");
	printf("<tr><th>PID</th><th>Sharing</th><th>R/W</th><th>Oplock</th><th>File</th><th>Date</th></tr>\n");

	locking_init(1);
	share_mode_forall(print_share_mode);
	locking_end();
	printf("</table>\n");

	fclose(f);

	printf("</FORM>\n");
}

