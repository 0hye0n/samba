/* 
 *  Unix SMB/CIFS implementation.
 *  Service Control API Implementation
 *  Copyright (C) Gerald Carter                   2005.
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "includes.h"

/* Implementation for internal netlogon service */

/*********************************************************************
*********************************************************************/

static WERROR netlogon_stop( SERVICE_STATUS *service_status )
{
	return WERR_ACCESS_DENIED;
}

/*********************************************************************
*********************************************************************/

static WERROR netlogon_start( void )
{
	return WERR_ACCESS_DENIED;
}

/*********************************************************************
*********************************************************************/

static WERROR netlogon_status( SERVICE_STATUS *service_status )
{
	ZERO_STRUCTP( service_status );

	service_status->type              = 0x20;
	if ( lp_servicenumber("NETLOGON") != -1 ) 
		service_status->state              = SVCCTL_RUNNING;
	else
		service_status->state              = SVCCTL_STOPPED;
	
	return WERR_OK;
}

/*********************************************************************
*********************************************************************/

/* struct for svcctl control to manipulate netlogon service */

SERVICE_CONTROL_OPS netlogon_svc_ops = {
	netlogon_stop,
	netlogon_start,
	netlogon_status
};
