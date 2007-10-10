/* 
   Unix SMB/CIFS implementation.
   Samba system utilities
   Copyright (C) Andrew Tridgell 1992-1998
   Copyright (C) Jeremy Allison 1998-2002
   
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
#include "system/network.h"
#include "system/wait.h"
#include "system/filesys.h"

/*
   The idea is that this file will eventually have wrappers around all
   important system calls in samba. The aims are:

   - to enable easier porting by putting OS dependent stuff in here

   - to allow for hooks into other "pseudo-filesystems"

   - to allow easier integration of things like the japanese extensions

   - to support the philosophy of Samba to expose the features of
     the OS within the SMB model. In general whatever file/printer/variable
     expansions/etc make sense to the OS should be acceptable to Samba.
*/



/*******************************************************************
 A wrapper for usleep in case we don't have one.
********************************************************************/

int sys_usleep(long usecs)
{
#ifndef HAVE_USLEEP
	struct timeval tval;
#endif

	/*
	 * We need this braindamage as the glibc usleep
	 * is not SPEC1170 complient... grumble... JRA.
	 */

	if(usecs < 0 || usecs > 1000000) {
		errno = EINVAL;
		return -1;
	}

#if HAVE_USLEEP
	usleep(usecs);
	return 0;
#else /* HAVE_USLEEP */
	/*
	 * Fake it with select...
	 */
	tval.tv_sec = 0;
	tval.tv_usec = usecs/1000;
	select(0,NULL,NULL,NULL,&tval);
	return 0;
#endif /* HAVE_USLEEP */
}


/*******************************************************************
 System wrapper for getwd
********************************************************************/
char *sys_getwd(char *s)
{
	char *wd;
#ifdef HAVE_GETCWD
	wd = (char *)getcwd(s, sizeof (pstring));
#else
	wd = (char *)getwd(s);
#endif
	return wd;
}

/*******************************************************************
A read wrapper that will deal with EINTR.
********************************************************************/

ssize_t sys_read(int fd, void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = read(fd, buf, count);
	} while (ret == -1 && errno == EINTR);
	return ret;
}

/*******************************************************************
A write wrapper that will deal with EINTR.
********************************************************************/

ssize_t sys_write(int fd, const void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = write(fd, buf, count);
	} while (ret == -1 && errno == EINTR);
	return ret;
}



/*******************************************************************
os/2 also doesn't have chroot
********************************************************************/
int sys_chroot(const char *dname)
{
#ifndef HAVE_CHROOT
	static int done;
	if (!done) {
		DEBUG(1,("WARNING: no chroot!\n"));
		done=1;
	}
	errno = ENOSYS;
	return -1;
#else
	return(chroot(dname));
#endif
}

/**************************************************************************
A wrapper for gethostbyname() that tries avoids looking up hostnames 
in the root domain, which can cause dial-on-demand links to come up for no
apparent reason.
****************************************************************************/

struct hostent *sys_gethostbyname(const char *name)
{
#ifdef REDUCE_ROOT_DNS_LOOKUPS
	char query[256], hostname[256];
	char *domain;

	/* Does this name have any dots in it? If so, make no change */

	if (strchr_m(name, '.'))
		return(gethostbyname(name));

	/* Get my hostname, which should have domain name 
		attached. If not, just do the gethostname on the
		original string. 
	*/

	gethostname(hostname, sizeof(hostname) - 1);
	hostname[sizeof(hostname) - 1] = 0;
	if ((domain = strchr_m(hostname, '.')) == NULL)
		return(gethostbyname(name));

	/* Attach domain name to query and do modified query.
		If names too large, just do gethostname on the
		original string.
	*/

	if((strlen(name) + strlen(domain)) >= sizeof(query))
		return(gethostbyname(name));

	slprintf(query, sizeof(query)-1, "%s%s", name, domain);
	return(gethostbyname(query));
#else /* REDUCE_ROOT_DNS_LOOKUPS */
	return(gethostbyname(name));
#endif /* REDUCE_ROOT_DNS_LOOKUPS */
}



/**************************************************************************
 Wrappers for dlopen, dlsym, dlclose.
****************************************************************************/

void *sys_dlopen(const char *name, int flags)
{
#if defined(HAVE_DLOPEN)
	return dlopen(name, flags);
#else
	return NULL;
#endif
}

void *sys_dlsym(void *handle, const char *symbol)
{
#if defined(HAVE_DLSYM)
    return dlsym(handle, symbol);
#else
    return NULL;
#endif
}

const char *sys_dlerror(void)
{
#if defined(HAVE_DLERROR)
	return dlerror();
#else
	return NULL;
#endif
}

const char *sys_inet_ntoa(struct ipv4_addr in)
{
	struct in_addr in2;
	in2.s_addr = in.addr;
	return inet_ntoa(in2);
}

uint32_t sys_inet_addr(const char *s)
{
	return inet_addr(s);
}

struct ipv4_addr sys_inet_makeaddr(int net, int host)
{
	struct in_addr in;
	struct ipv4_addr in2;
	in = inet_makeaddr(net, host);
	in2.addr = in.s_addr;
	return in2;
}

