/* 
   Unix SMB/CIFS implementation.

   multiple interface handling

   Copyright (C) Andrew Tridgell 1992-2005
   
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
#include "lib/netif/netif.h"
#include "dlinklist.h"

static struct iface_struct *probed_ifaces;
static int total_probed;

static struct ipv4_addr allones_ip;
struct ipv4_addr loopback_ip;

/* used for network interfaces */
struct interface {
	struct interface *next, *prev;
	struct ipv4_addr ip;
	struct ipv4_addr bcast;
	struct ipv4_addr nmask;
};

static struct interface *local_interfaces;

#define ALLONES  ((uint32_t)0xFFFFFFFF)
#define MKBCADDR(_IP, _NM) ((_IP & _NM) | (_NM ^ ALLONES))
#define MKNETADDR(_IP, _NM) (_IP & _NM)

static struct ipv4_addr tov4(struct in_addr in)
{
	struct ipv4_addr in2;
	in2.addr = in.s_addr;
	return in2;
}

/****************************************************************************
Try and find an interface that matches an ip. If we cannot, return NULL
  **************************************************************************/
static struct interface *iface_find(struct in_addr ip, BOOL CheckMask)
{
	struct interface *i;
	if (is_zero_ip(tov4(ip))) return local_interfaces;

	for (i=local_interfaces;i;i=i->next)
		if (CheckMask) {
			if (same_net(i->ip,tov4(ip),i->nmask)) return i;
		} else if (i->ip.addr == ip.s_addr) return i;

	return NULL;
}


/****************************************************************************
add an interface to the linked list of interfaces
****************************************************************************/
static void add_interface(struct in_addr ip, struct in_addr nmask)
{
	struct interface *iface;
	if (iface_find(ip, False)) {
		DEBUG(3,("not adding duplicate interface %s\n",inet_ntoa(ip)));
		return;
	}

	if (nmask.s_addr == allones_ip.addr) {
		DEBUG(3,("not adding non-broadcast interface %s\n",inet_ntoa(ip)));
		return;
	}

	iface = malloc_p(struct interface);
	if (!iface) return;
	
	ZERO_STRUCTPN(iface);

	iface->ip = tov4(ip);
	iface->nmask = tov4(nmask);
	iface->bcast.addr = MKBCADDR(iface->ip.addr, iface->nmask.addr);

	DLIST_ADD_END(local_interfaces, iface, struct interface *);

	DEBUG(2,("added interface ip=%s ",sys_inet_ntoa(iface->ip)));
	DEBUG(2,("bcast=%s ",sys_inet_ntoa(iface->bcast)));
	DEBUG(2,("nmask=%s\n",sys_inet_ntoa(iface->nmask)));	     
}



/****************************************************************************
interpret a single element from a interfaces= config line 

This handles the following different forms:

1) wildcard interface name
2) DNS name
3) IP/masklen
4) ip/mask
5) bcast/mask
****************************************************************************/
static void interpret_interface(TALLOC_CTX *mem_ctx, const char *token)
{
	struct in_addr ip, nmask;
	char *p;
	int i, added=0;

	ip.s_addr = 0;
	nmask.s_addr = 0;
	
	/* first check if it is an interface name */
	for (i=0;i<total_probed;i++) {
		if (gen_fnmatch(token, probed_ifaces[i].name) == 0) {
			add_interface(probed_ifaces[i].ip,
				      probed_ifaces[i].netmask);
			added = 1;
		}
	}
	if (added) return;

	/* maybe it is a DNS name */
	p = strchr_m(token,'/');
	if (!p) {
		ip.s_addr = interpret_addr2(token).addr;
		for (i=0;i<total_probed;i++) {
			if (ip.s_addr == probed_ifaces[i].ip.s_addr &&
			    allones_ip.addr != probed_ifaces[i].netmask.s_addr) {
				add_interface(probed_ifaces[i].ip,
					      probed_ifaces[i].netmask);
				return;
			}
		}
		DEBUG(2,("can't determine netmask for %s\n", token));
		return;
	}

	/* parse it into an IP address/netmasklength pair */
	*p++ = 0;

	ip.s_addr = interpret_addr2(token).addr;

	if (strlen(p) > 2) {
		nmask.s_addr = interpret_addr2(p).addr;
	} else {
		nmask.s_addr = htonl(((ALLONES >> atoi(p)) ^ ALLONES));
	}

	/* maybe the first component was a broadcast address */
	if (ip.s_addr == MKBCADDR(ip.s_addr, nmask.s_addr) ||
	    ip.s_addr == MKNETADDR(ip.s_addr, nmask.s_addr)) {
		for (i=0;i<total_probed;i++) {
			if (same_net(tov4(ip), tov4(probed_ifaces[i].ip), tov4(nmask))) {
				add_interface(probed_ifaces[i].ip, nmask);
				return;
			}
		}
		DEBUG(2,("Can't determine ip for broadcast address %s\n", token));
		return;
	}

	add_interface(ip, nmask);
}


/****************************************************************************
load the list of network interfaces
****************************************************************************/
void load_interfaces(void)
{
	const char **ptr;
	int i;
	struct iface_struct ifaces[MAX_INTERFACES];
	TALLOC_CTX *mem_ctx;

	ptr = lp_interfaces();
	mem_ctx = talloc_init("load_interfaces");
	if (!mem_ctx) {
		DEBUG(2,("no memory to load interfaces \n"));
		return;
	}

	allones_ip = interpret_addr2("255.255.255.255");
	loopback_ip = interpret_addr2("127.0.0.1");

	SAFE_FREE(probed_ifaces);

	/* dump the current interfaces if any */
	while (local_interfaces) {
		struct interface *iface = local_interfaces;
		DLIST_REMOVE(local_interfaces, local_interfaces);
		ZERO_STRUCTPN(iface);
		SAFE_FREE(iface);
	}

	/* probe the kernel for interfaces */
	total_probed = get_interfaces(ifaces, MAX_INTERFACES);

	if (total_probed > 0) {
		probed_ifaces = memdup(ifaces, sizeof(ifaces[0])*total_probed);
	}

	/* if we don't have a interfaces line then use all broadcast capable 
	   interfaces except loopback */
	if (!ptr || !*ptr || !**ptr) {
		if (total_probed <= 0) {
			DEBUG(0,("ERROR: Could not determine network interfaces, you must use a interfaces config line\n"));
		}
		for (i=0;i<total_probed;i++) {
			if (probed_ifaces[i].netmask.s_addr != allones_ip.addr &&
			    probed_ifaces[i].ip.s_addr != loopback_ip.addr) {
				add_interface(probed_ifaces[i].ip, 
					      probed_ifaces[i].netmask);
			}
		}
		goto exit;
	}

	if (ptr) {
		while (*ptr) {
			interpret_interface(mem_ctx, *ptr);
			ptr++;
		}
	}

	if (!local_interfaces) {
		DEBUG(0,("WARNING: no network interfaces found\n"));
	}
	
exit:
	talloc_free(mem_ctx);
}


/****************************************************************************
return True if the list of probed interfaces has changed
****************************************************************************/
BOOL interfaces_changed(void)
{
	int n;
	struct iface_struct ifaces[MAX_INTERFACES];

	n = get_interfaces(ifaces, MAX_INTERFACES);

	if ((n > 0 )&& (n != total_probed ||
	    memcmp(ifaces, probed_ifaces, sizeof(ifaces[0])*n))) {
		return True;
	}
	
	return False;
}


/****************************************************************************
  check if an IP is one of mine
  **************************************************************************/
BOOL ismyip(struct ipv4_addr ip)
{
	struct interface *i;
	for (i=local_interfaces;i;i=i->next) {
		if (i->ip.addr == ip.addr) return True;
	}
	return False;
}

/****************************************************************************
  how many interfaces do we have
  **************************************************************************/
int iface_count(void)
{
	int ret = 0;
	struct interface *i;

	for (i=local_interfaces;i;i=i->next)
		ret++;
	return ret;
}

/****************************************************************************
  return IP of the Nth interface
  **************************************************************************/
const char *iface_n_ip(int n)
{
	struct interface *i;
  
	for (i=local_interfaces;i && n;i=i->next)
		n--;

	if (i) {
		return sys_inet_ntoa(i->ip);
	}
	return NULL;
}

/****************************************************************************
  return bcast of the Nth interface
  **************************************************************************/
const char *iface_n_bcast(int n)
{
	struct interface *i;
  
	for (i=local_interfaces;i && n;i=i->next)
		n--;

	if (i) {
		return sys_inet_ntoa(i->bcast);
	}
	return NULL;
}

/****************************************************************************
  return netmask of the Nth interface
  **************************************************************************/
const char *iface_n_netmask(int n)
{
	struct interface *i;
  
	for (i=local_interfaces;i && n;i=i->next)
		n--;

	if (i) {
		return sys_inet_ntoa(i->nmask);
	}
	return NULL;
}

/*
  return the local IP address that best matches a destination IP, or
  our first interface if none match
*/
const char *iface_best_ip(const char *dest)
{
	struct interface *iface;
	struct in_addr ip;
	ip.s_addr = interpret_addr(dest);
	iface = iface_find(ip, True);
	if (iface) {
		return sys_inet_ntoa(iface->ip);
	}
	return iface_n_ip(0);
}

/*
  return True if an IP is one one of our local networks
*/
BOOL iface_is_local(const char *dest)
{
	struct in_addr ip;
	ip.s_addr = interpret_addr(dest);
	if (iface_find(ip, True)) {
		return True;
	}
	return False;
}
