/* 
   Samba Unix/Linux SMB client library 
   Distributed SMB/CIFS Server Management Utility 

   Copyright (C) 2004 Stefan Metzmacher <metze@samba.org>
   Copyright (C) 2005 Andrew Bartlett <abartlet@samba.org>

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
#include "utils/net/net.h"
#include "libnet/libnet.h"
#include "librpc/gen_ndr/ndr_samr.h"

int net_join(struct net_context *ctx, int argc, const char **argv) 
{
	NTSTATUS status;
	struct libnet_context *libnetctx;
	union libnet_Join r;
	char *tmp;
	const char *domain_name;
	enum netr_SchannelType secure_channel_type = SEC_CHAN_WKSTA;

	switch (argc) {
		case 0: /* no args -> fail */
			return net_join_usage(ctx, argc, argv);
		case 1: /* only DOMAIN */
			tmp = talloc_strdup(ctx->mem_ctx, argv[0]);
			break;
		case 2: /* DOMAIN and role */
			tmp = talloc_strdup(ctx->mem_ctx, argv[0]);
			if (strcasecmp(argv[1], "BDC") == 0) {
				secure_channel_type = SEC_CHAN_BDC;
			} else if (strcasecmp(argv[1], "MEMBER") == 0) {
				secure_channel_type = SEC_CHAN_WKSTA;
			} else {
				DEBUG(0, ("net_join: 2nd argument must be MEMBER or BDC\n"));
				return net_join_usage(ctx, argc, argv);
			}
			break;
		default: /* too many args -> fail */
			return net_join_usage(ctx, argc, argv);
	}

	domain_name = tmp;

	libnetctx = libnet_context_init();
	if (!libnetctx) {
		return -1;	
	}
	libnetctx->credentials = ctx->credentials;

	/* prepare password change */
	r.generic.level			 = LIBNET_JOIN_GENERIC;
	r.generic.in.domain_name	 = domain_name;
	r.generic.in.secure_channel_type = secure_channel_type;
	r.generic.out.error_string       = NULL;

	/* do the domain join */
	status = libnet_Join(libnetctx, ctx->mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0,("libnet_Join returned %s: %s\n",
			 nt_errstr(status),
			 r.generic.out.error_string));
		return -1;
	}

	libnet_context_destroy(&libnetctx);

	return 0;
}

int net_join_usage(struct net_context *ctx, int argc, const char **argv)
{
	d_printf("net join <domain> [BDC | MEMBER] [options]\n");
	return 0;	
}

int net_join_help(struct net_context *ctx, int argc, const char **argv)
{
	d_printf("Joins domain as either member or backup domain controller.\n");
	return 0;	
}
