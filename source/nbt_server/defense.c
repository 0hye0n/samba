/* 
   Unix SMB/CIFS implementation.

   defend our names against name registration requests

   Copyright (C) Andrew Tridgell	2005
   
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
#include "dlinklist.h"
#include "system/network.h"
#include "nbt_server/nbt_server.h"


/*
  defend our registered names against registration or name refresh
  requests
*/
void nbtd_request_defense(struct nbt_name_socket *nbtsock, 
			  struct nbt_name_packet *packet, 
			  const char *src_address, int src_port)
{
	struct nbtd_iface_name *iname;
	struct nbt_name *name;
	struct nbtd_interface *iface = talloc_get_type(nbtsock->incoming.private, 
						       struct nbtd_interface);

	NBTD_ASSERT_PACKET(packet, src_address, packet->qdcount == 1);
	NBTD_ASSERT_PACKET(packet, src_address, packet->arcount == 1);
	NBTD_ASSERT_PACKET(packet, src_address, 
			   packet->questions[0].question_type == NBT_QTYPE_NETBIOS);
	NBTD_ASSERT_PACKET(packet, src_address, 
			   packet->questions[0].question_class == NBT_QCLASS_IP);
	NBTD_ASSERT_PACKET(packet, src_address, 
			  packet->additional[0].rr_type == NBT_QTYPE_NETBIOS);
	NBTD_ASSERT_PACKET(packet, src_address, 
			  packet->additional[0].rr_class == NBT_QCLASS_IP);
	NBTD_ASSERT_PACKET(packet, src_address, 
			  packet->additional[0].rdata.netbios.length == 6);

	/* see if we have the requested name on this interface */
	name = &packet->questions[0].name;

	iname = nbtd_find_iname(iface, name, NBT_NM_ACTIVE);
	if (iname != NULL && 
	    !IS_GROUP_NAME(name, iname->nb_flags)) {
		DEBUG(2,("Defending name %s on %s against %s\n",
			 nbt_name_string(packet, name), 
			 iface->bcast_address, src_address));
		nbtd_name_registration_reply(nbtsock, packet, 
					     src_address, src_port, NBT_RCODE_ACT);
	} else {
		nbtd_winsserver_request(nbtsock, packet, src_address, src_port);
	}
}
