/*
   Unix SMB/Netbios implementation.
   Version 3.0
   Samba utility functions
   Copyright (C) Simo Sorce 2001

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

static void print_grent_list(struct sys_grent *glist)
{
	DEBUG(100, ("print_grent_list: %x\n", glist ));
	while (glist) {
		DEBUG(100,("glist: %x ", glist));
		if (glist->gr_name)
			DEBUG(100,(": gr_name = (%x) %s ", glist->gr_name, glist->gr_name));
		if (glist->gr_passwd)
			DEBUG(100,(": gr_passwd = (%x) %s ", glist->gr_passwd, glist->gr_passwd));
		if (glist->gr_mem) {
			int i;
			for (i = 0; glist->gr_mem[i]; i++)
				DEBUG(100,(" : gr_mem[%d] = (%x) %s ", i, glist->gr_mem[i], glist->gr_mem[i]));
		}
		DEBUG(100,(": gr_next = %x\n", glist->next ));
		glist = glist->next;
	}
	DEBUG(100,("FINISHED !\n\n"));
}

/****************************************************************
 Returns a single linked list of group entries.
 Use grent_free() to free it after use.
****************************************************************/

struct sys_grent * getgrent_list(void)
{
	struct sys_grent *glist;
	struct sys_grent *gent;
	struct group *grp;
	
	gent = (struct sys_grent *) malloc(sizeof(struct sys_grent));
	if (gent == NULL) {
		DEBUG (0, ("Out of memory in getgrent_list!\n"));
		return NULL;
	}
	memset(gent, '\0', sizeof(struct sys_grent));
	glist = gent;
	
	setgrent();
	grp = getgrent();
	if (grp == NULL) {
		endgrent();
		free(glist);
		return NULL;
	}

	while (grp != NULL) {
		int i,num;
		
		if (grp->gr_name) {
			if ((gent->gr_name = strdup(grp->gr_name)) == NULL)
				goto err;
		}
		if (grp->gr_passwd) {
			if ((gent->gr_passwd = strdup(grp->gr_passwd)) == NULL)
				goto err;
		}
		gent->gr_gid = grp->gr_gid;
		
		/* number of strings in gr_mem */
		for (num = 0; grp->gr_mem[num];	num++)
			;
		
		/* alloc space for gr_mem string pointers */
		if ((gent->gr_mem = (char **) malloc((num+1) * sizeof(char *))) == NULL)
			goto err;

		memset(gent->gr_mem, '\0', (num+1) * sizeof(char *));

		for (i=0; i < num; i++) {
			if ((gent->gr_mem[i] = strdup(grp->gr_mem[i])) == NULL)
				goto err;
		}
		gent->gr_mem[num] = NULL;
		
		grp = getgrent();
		if (grp) {
			gent->next = (struct sys_grent *) malloc(sizeof(struct sys_grent));
			if (gent->next == NULL)
				goto err;
			gent = gent->next;
			memset(gent, '\0', sizeof(struct sys_grent));
		}
	}
	
	endgrent();
	print_grent_list(glist);
	DEBUG(100,("getgrent_list returned %x\n", glist ));
	return glist;

  err:

	endgrent();
	DEBUG(0, ("Out of memory in getgrent_list!\n"));
	grent_free(glist);
	return NULL;
}

/****************************************************************
 Free the single linked list of group entries made by
 getgrent_list()
****************************************************************/

void grent_free (struct sys_grent *glist)
{
	DEBUG(100,("getgrent_free %x\n", glist ));
	while (glist) {
		struct sys_grent *prev;
		
		print_grent_list(glist);

		if (glist->gr_name)
			free(glist->gr_name);
		if (glist->gr_passwd)
			free(glist->gr_passwd);
		if (glist->gr_mem) {
			int i;
			for (i = 0; glist->gr_mem[i]; i++)
				free(glist->gr_mem[i]);
			free(glist->gr_mem);
		}
		prev = glist;
		glist = glist->next;
		free(prev);
	}
}

/****************************************************************
 Returns a single linked list of passwd entries.
 Use pwent_free() to free it after use.
****************************************************************/

struct sys_pwent * getpwent_list(void)
{
	struct sys_pwent *plist;
	struct sys_pwent *pent;
	struct passwd *pwd;
	
	pent = (struct sys_pwent *) malloc(sizeof(struct sys_pwent));
	if (pent == NULL) {
		DEBUG (0, ("Out of memory in getpwent_list!\n"));
		return NULL;
	}
	plist = pent;
	
	setpwent();
	pwd = getpwent();
	while (pwd != NULL) {
		memset(pent, '\0', sizeof(struct sys_pwent));
		if (pwd->pw_name) {
			if ((pent->pw_name = strdup(pwd->pw_name)) == NULL)
				goto err;
		}
		if (pwd->pw_passwd) {
			if ((pent->pw_passwd = strdup(pwd->pw_passwd)) == NULL)
				goto err;
		}
		pent->pw_uid = pwd->pw_uid;
		pent->pw_gid = pwd->pw_gid;
		if (pwd->pw_gecos) {
			if ((pent->pw_name = strdup(pwd->pw_gecos)) == NULL)
				goto err;
		}
		if (pwd->pw_dir) {
			if ((pent->pw_name = strdup(pwd->pw_dir)) == NULL)
				goto err;
		}
		if (pwd->pw_shell) {
			if ((pent->pw_name = strdup(pwd->pw_shell)) == NULL)
				goto err;
		}

		pwd = getpwent();
		if (pwd) {
			pent->next = (struct sys_pwent *) malloc(sizeof(struct sys_pwent));
			if (pent->next == NULL)
				goto err;
			pent = pent->next;
		}
	}
	
	endpwent();
	return plist;

  err:

	endpwent();
	DEBUG(0, ("Out of memory in getpwent_list!\n"));
	pwent_free(plist);
	return NULL;
}

/****************************************************************
 Free the single linked list of passwd entries made by
 getpwent_list()
****************************************************************/

void pwent_free (struct sys_pwent *plist)
{
	while (plist) {
		struct sys_pwent *prev;
		
		if (plist->pw_name)
			free(plist->pw_name);
		if (plist->pw_passwd)
			free(plist->pw_passwd);
		if (plist->pw_gecos)
			free(plist->pw_gecos);
		if (plist->pw_dir)
			free(plist->pw_dir);
		if (plist->pw_shell)
			free(plist->pw_shell);

		prev = plist;
		plist = plist->next;
		free(prev);
	}
}
