/* 
 *  Unix SMB/Netbios implementation.
 *  Version 1.9.
 *  RPC Pipe client / server routines
 *  Copyright (C) Andrew Tridgell              1992-1998,
 *  Copyright (C) Luke Kenneth Casson Leighton 1996-1998,
 *  Copyright (C) Jean Fran�ois Micouleau      1998-1999.
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
#include "nterr.h"

extern int DEBUGLEVEL;
extern pstring global_myname;

#ifndef MAX_OPEN_PRINTERS
#define MAX_OPEN_PRINTERS 50
#endif

#define PRINTER_HANDLE_IS_PRINTER	0
#define PRINTER_HANDLE_IS_PRINTSERVER	1


/* structure to store the printer handles */
/* and a reference to what it's pointing to */
/* and the notify info asked about */
/* that's the central struct */
static struct
{
  BOOL        open;
  BOOL        document_started;
  BOOL        page_started;
  uint32      current_jobid;
  uint32      document_fd;
  uint32      document_lastwritten;
  pstring     document_name;
  pstring     job_name;
  PRINTER_HND printer_hnd;
  BOOL        printer_type;
  union
  {
  	fstring printername;
	fstring printerservername;
  } dev;
  uint32 type;
  uint32 access;
  uint32 number_of_notify;
  SPOOL_NOTIFY_OPTION_TYPE notify_info[MAX_PRINTER_NOTIFY+MAX_JOB_NOTIFY];
} Printer[MAX_OPEN_PRINTERS];

#define VALID_HANDLE(pnum)   (((pnum) >= 0) && ((pnum) < MAX_OPEN_PRINTERS))
#define OPEN_HANDLE(pnum)    (VALID_HANDLE(pnum) && Printer[pnum].open)

/****************************************************************************
  initialise printer handle states...
****************************************************************************/
void init_printer_hnd(void)
{
	int i;
	for (i = 0; i < MAX_OPEN_PRINTERS; i++)
	{
		Printer[i].open = False;
	}
}


/****************************************************************************
  create a unique printer handle
****************************************************************************/
static void create_printer_hnd(PRINTER_HND *hnd)
{
	static uint32 prt_hnd_low  = 0;
	static uint32 prt_hnd_high = 0;

	if (hnd == NULL) return;

	/* i severely doubt that prt_hnd_high will ever be non-zero... */
	prt_hnd_low++;
	if (prt_hnd_low == 0) prt_hnd_high++;

	SIVAL(hnd->data, 0 , 0x0);          /* first bit must be null */
	SIVAL(hnd->data, 4 , prt_hnd_low ); /* second bit is incrementing */
	SIVAL(hnd->data, 8 , prt_hnd_high); /* second bit is incrementing */
	SIVAL(hnd->data, 12, time(NULL));   /* something random */
	SIVAL(hnd->data, 16, getpid());     /* something more random */
}

/****************************************************************************
  clear an handle
****************************************************************************/
static void clear_handle(PRINTER_HND *hnd)
{
	bzero(hnd->data, PRINTER_HND_SIZE);
}

/****************************************************************************
  find first available printer slot.  creates a printer handle for you.
 ****************************************************************************/
static BOOL open_printer_hnd(PRINTER_HND *hnd)
{
	int i;

	for (i = 0; i < MAX_OPEN_PRINTERS; i++)
	{
		if (!Printer[i].open)
		{
			Printer[i].open = True;				
			create_printer_hnd(hnd);
			memcpy(&(Printer[i].printer_hnd), hnd, sizeof(*hnd));

			DEBUG(4,("Opened printer handle[%x] ", i));
			dump_data(4, hnd->data, sizeof(hnd->data));
			return True;
		}
	}
	DEBUG(1,("ERROR - open_printer_hnd: out of Printers Handles!\n"));
	return False;
}

/****************************************************************************
  find printer index by handle
****************************************************************************/
static int find_printer_index_by_hnd(PRINTER_HND *hnd)
{
	int i;

	for (i = 0; i < MAX_OPEN_PRINTERS; i++)
	{
		if (memcmp(&(Printer[i].printer_hnd), hnd, sizeof(*hnd)) == 0)
		{
			DEBUG(4,("Found printer handle[%x] ", i));
			dump_data(4, hnd->data, sizeof(hnd->data));
			return i;
		}
	}
	DEBUG(3,("Whoops, Printer handle not found: "));
	dump_data(4, hnd->data, sizeof(hnd->data));
	return -1;
}

/****************************************************************************
  set printer handle type.
****************************************************************************/
static BOOL set_printer_hnd_accesstype(PRINTER_HND *hnd, uint32 access_required)
{
	int pnum = find_printer_index_by_hnd(hnd);

	if (OPEN_HANDLE(pnum))
	{
		DEBUG(4,("Setting printer access=%x (pnum=%x)\n",
		          access_required, pnum));



		Printer[pnum].access = access_required;
		return True;		
	}
	else
	{
		DEBUG(4,("Error setting printer type=%x (pnum=%x)",
		          access_required, pnum));
		return False;
	}
	return False;
}

/****************************************************************************
  set printer handle type.
****************************************************************************/
static BOOL set_printer_hnd_printertype(PRINTER_HND *hnd, char *printername)
{
	int pnum = find_printer_index_by_hnd(hnd);
		
	if (OPEN_HANDLE(pnum))
	{
		DEBUG(3,("Setting printer type=%s (pnum=%x)\n", printername, pnum));

		if ( strlen(printername) < 3 )
		{
			DEBUGADD(4,("A print server must have at least 1 char ! %s\n", printername));
			return False;
		}

		/* check if it's \\server or \\server\printer */		
		/* +2 is to skip the leading \\ */
		if (!strchr(printername+2, '\\'))
		{
			/* it's a print server */
			DEBUGADD(4,("Printer is a print server\n"));
			Printer[pnum].printer_type = PRINTER_HANDLE_IS_PRINTSERVER;
			return True;
		}
		else
		{
			/* it's a printer */
			DEBUGADD(4,("Printer is a printer\n"));
			Printer[pnum].printer_type = PRINTER_HANDLE_IS_PRINTER;
			return True;
		}	
	}
	else
	{
		DEBUGADD(4,("Error setting printer name %s (pnum=%x)",
		          printername, pnum));
		return False;
	}
	return False;
}

/****************************************************************************
  set printer handle printername.
****************************************************************************/
static BOOL set_printer_hnd_printername(PRINTER_HND *hnd, char *printername)
{
	int pnum = find_printer_index_by_hnd(hnd);
	char *back;
	NT_PRINTER_INFO_LEVEL printer;
	int snum;
	int n_services=lp_numservices();
	uint32 marche;
	
	if (OPEN_HANDLE(pnum))
	{
		DEBUG(4,("Setting printer name=%s (len=%d) (pnum=%x)\n",
		          printername,strlen(printername), pnum));
			  
		switch (Printer[pnum].printer_type)
		 {
		   case PRINTER_HANDLE_IS_PRINTER:
		   	back=strchr(printername+2, '\\');
			back=back+1;
			DEBUGADD(5,("searching for %s (len=%d)\n", back,strlen(back)));
			/* 
			 * store the Samba share name in it
			 * in back we have the long printer name
			 * need to iterate all the snum and do a 
			 * get_a_printer each time to find the printer
			 * faster to do it here than later.
			 */
			for (snum=0;snum<n_services; snum++)
			{
				if (lp_browseable(snum) && 
				    lp_snum_ok(snum) && 
				    lp_print_ok(snum) )
				{
					DEBUGADD(5,("get_printer_snum, share:%s\n",lp_servicename(snum)));
					
					marche=get_a_printer(&printer, 2, lp_servicename(snum));
					DEBUGADD(6,("get_printer_snum, marche:%d\n",marche));
										
					if ( marche==0 && ( strlen(printer.info_2->printername) == strlen(back) ) 
					     && ( !strncasecmp(printer.info_2->printername, back, strlen(back))) 
					   )
					{
						DEBUGADD(4,("Printer found: %s[%x]\n",lp_servicename(snum),snum));
						strncpy(Printer[pnum].dev.printername, lp_servicename(snum), strlen(lp_servicename(snum)));
						free_a_printer(printer, 2);
						return True;
						break;	
					}
					free_a_printer(printer, 2);
				}
			}

			return False;
			break;		
		   case PRINTER_HANDLE_IS_PRINTSERVER:
			strncpy(Printer[pnum].dev.printerservername, printername, strlen(printername));
			return True;
			break;
		   default:
			return False;
			break;
		 }
	}
	else
	{
		DEBUG(0,("Error setting printer name=%s (pnum=%x)\n",
		         printername , pnum));
		return False;
	}
}

/****************************************************************************
  return the snum of a printer corresponding to an handle
****************************************************************************/
static BOOL get_printer_snum(PRINTER_HND *hnd, int *number)
{
	int snum;
	int pnum = find_printer_index_by_hnd(hnd);
	int n_services=lp_numservices();
		
	if (OPEN_HANDLE(pnum))
	{
		switch (Printer[pnum].printer_type)
		 {
		   case PRINTER_HANDLE_IS_PRINTER:		   
			DEBUG(4,("get_printer_snum, short name:%s\n", Printer[pnum].dev.printername));			
			for (snum=0;snum<n_services; snum++)
			{
				if (lp_browseable(snum) && lp_snum_ok(snum) && lp_print_ok(snum) )
				{
					DEBUG(4,("get_printer_snum, share:%s\n",lp_servicename(snum)));
					if (   ( strlen(lp_servicename(snum)) == strlen( Printer[pnum].dev.printername ) ) 
					    && ( !strncasecmp(lp_servicename(snum), 
					                      Printer[pnum].dev.printername,
							      strlen( lp_servicename(snum) ))) 
					   )
					{
						DEBUG(4,("Printer found: %s[%x]\n",lp_servicename(snum),snum));
						*number=snum;
						return True;
						break;	
					}
				}
			}
			return False;
			break;		
		   case PRINTER_HANDLE_IS_PRINTSERVER:
			return False;
			break;
		   default:
			return False;
			break;
		 }
	}
	else
	{
		DEBUG(3,("Error getting printer - take a nap quickly !\n"));
		return False;
	}
}

/********************************************************************
 ********************************************************************/
static BOOL handle_is_printserver(PRINTER_HND *handle)
{
	int pnum=find_printer_index_by_hnd(handle);

	if (OPEN_HANDLE(pnum))
	{
		switch (Printer[pnum].printer_type)
		{
		case PRINTER_HANDLE_IS_PRINTSERVER:
			return True;
			break;
		case PRINTER_HANDLE_IS_PRINTER:
			return False;
			break;
		}		
	}
	return False;
}

/********************************************************************
 ********************************************************************/
/*
static BOOL handle_is_printer(PRINTER_HND *handle)
{
	return (!handle_is_printserver(handle));
}
*/

/********************************************************************
 * api_spoolss_open_printer
 *
 * called from the spoolss dispatcher
 ********************************************************************/
static void spoolss_reply_open_printer(SPOOL_Q_OPEN_PRINTER *q_u, prs_struct *rdata)                                
{
	SPOOL_R_OPEN_PRINTER r_u;
	BOOL printer_open = False;
	fstring name;
	
	/* some sanity check because you can open a printer or a print server */
	/* aka: \\server\printer or \\server */
	unistr2_to_ascii(name, &(q_u->printername), sizeof(name)-1);

	DEBUGADD(3,("checking name: %s\n",name));

	/* now the response */
	r_u.status=0x00000000;
	
	printer_open = open_printer_hnd(&(r_u.handle));
	set_printer_hnd_printertype(&(r_u.handle), name);
	
	if ( !set_printer_hnd_printername(&(r_u.handle), name) )
	{
		r_u.status=0xC0000000|NT_STATUS_ACCESS_DENIED;
	}
	
	set_printer_hnd_accesstype(&(r_u.handle), q_u->access_required);
	
	spoolss_io_r_open_printer("",&r_u,rdata,0);
}

/********************************************************************
 * api_spoolss_open_printer
 *
 * called from the spoolss dispatcher
 ********************************************************************/
static void api_spoolss_open_printer(pipes_struct *p, prs_struct *data, prs_struct *rdata)
{
	SPOOL_Q_OPEN_PRINTER q_u;

	/* grab the spoolss open policy */
	spoolss_io_q_open_printer("", &q_u, data, 0);

	/* construct reply.  always indicate success */
	spoolss_reply_open_printer(&q_u, rdata);
}

/********************************************************************
 ********************************************************************/
static BOOL getprinterdata_printer_server(fstring value, uint32 size, uint32 *type, 
                                          uint32 *numeric_data, uint8 **data, uint32 *needed)
{		
		
	if (!strcmp(value, "BeepEnabled"))
	{
		*type          = 0x4;
		*data  = (uint8 *)malloc( 4*sizeof(uint8) );
		ZERO_STRUCTP(*data);
		(*data)[0]=0x01;
		(*data)[1]=0x00;
		(*data)[2]=0x00;
		(*data)[3]=0x00;
		*numeric_data  = 0x1; /* beep enabled */	
		*needed        = 0x4;			
		return True;
	}

	if (!strcmp(value, "EventLog"))
	{
		*type          = 0x4;
		*data  = (uint8 *)malloc( 4*sizeof(uint8) );
		ZERO_STRUCTP(*data);
		(*data)[0]=0x1B;
		(*data)[1]=0x00;
		(*data)[2]=0x00;
		(*data)[3]=0x00;
		*numeric_data  = 0x1B; /* Don't know ??? */	
		*needed        = 0x4;			
		return True;
	}

	if (!strcmp(value, "NetPopup"))
	{
		*type          = 0x4;
		*data  = (uint8 *)malloc( 4*sizeof(uint8) );
		ZERO_STRUCTP(*data);
		(*data)[0]=0x01;
		(*data)[1]=0x00;
		(*data)[2]=0x00;
		(*data)[3]=0x00;
		*numeric_data  = 0x1; /* popup enabled */	
		*needed        = 0x4;
		return True;
	}

	if (!strcmp(value, "MajorVersion"))
	{
		*type          = 0x4;
		*data  = (uint8 *)malloc( 4*sizeof(uint8) );
		(*data)[0]=0x02;
		(*data)[1]=0x00;
		(*data)[2]=0x00;
		(*data)[3]=0x00;
		*numeric_data  = 0x2; /* it's 2, period. */	
		*needed        = 0x4;
		return True;
	}

	if (!strcmp(value, "DefaultSpoolDirectory"))
	{
		pstring directory="You are using a Samba server";
		*type = 0x1;			
		*data  = (uint8 *)malloc( size*sizeof(uint8) );
		ZERO_STRUCTP(*data);
		make_unistr((UNISTR *)*data, directory);
		*needed = 2*(strlen(directory)+1);			
		return True;
	}

	if (!strcmp(value, "Architecture"))
	{			
		pstring directory="Windows NT x86";
		*type = 0x1;			
		*data  = (uint8 *)malloc( size*sizeof(uint8) );
		ZERO_STRUCTP(*data);
		make_unistr((UNISTR *)*data, directory);
		*needed = 2*(strlen(directory)+1);	
		return True;
	}
	
	return False;
}

static BOOL getprinterdata_printer(PRINTER_HND *handle, fstring value, uint32 size, uint32 *type, 
                                          uint32 *numeric_data, uint8 **data, uint32 *needed )
{
	NT_PRINTER_INFO_LEVEL printer;
	int pnum=0;
	int snum=0;
	uint8 *idata=NULL;
	uint32 len;
	
	DEBUG(5,("getprinterdata_printer\n"));

	pnum = find_printer_index_by_hnd(handle);
	if (OPEN_HANDLE(pnum))
	{
		get_printer_snum(handle, &snum);		
		get_a_printer(&printer, 2, lp_servicename(snum));
		
		if (get_specific_param(printer, 2, value, &idata, type, &len)) 
		{
			switch (*type)
			{
				case 1:
				case 3:
				case 4:
					*data  = (uint8 *)malloc( size*sizeof(uint8) );
					bzero(*data, sizeof(uint8)*size);
					memcpy(*data, idata, len>size?size:len);
					*needed = len;
					if (idata) free(idata);
					break;
				/*case 4:
					*numeric_data=atoi(idata);
					break;*/
			}
			return (True);
		}
		free_a_printer(printer, 2);
	}

	return (False);
}	

/********************************************************************
 * api_spoolss_reply_getprinterdata
 *
 * called from api_spoolss_getprinterdata
 ********************************************************************/
static void spoolss_reply_getprinterdata(SPOOL_Q_GETPRINTERDATA *q_u, prs_struct *rdata)                                
{
	SPOOL_R_GETPRINTERDATA r_u;
	fstring value;
	BOOL found;
	int pnum = find_printer_index_by_hnd(&(q_u->handle));
	
	/* 
	 * Reminder: when it's a string, the length is in BYTES
	 * even if UNICODE is negociated.
	 *
	 * r_u.type is the kind of data
	 * 1 is a string
	 * 4 is a uint32
	 *
	 * I think it's documented in MSDN somewhere in
	 * the registry data type (yep it's linked ...)
	 * 
	 * JFM, 4/19/1999
	 */

	if (OPEN_HANDLE(pnum))
	{
		r_u.size  = q_u->size;
		r_u.status = 0x0;
		r_u.type   = 0x4;
		r_u.needed = 0x0;
		r_u.data   = NULL;
		r_u.numeric_data=0x0;
		
		unistr2_to_ascii(value, &(q_u->valuename), sizeof(value)-1);
		
		if (handle_is_printserver(&(q_u->handle)))
		{		
			found=getprinterdata_printer_server(value, r_u.size, 
			                                    &(r_u.type), &(r_u.numeric_data),
			                                    &(r_u.data), &(r_u.needed));
		}
		else
		{
			found=getprinterdata_printer(&(q_u->handle), value, r_u.size, 
			                             &(r_u.type), &(r_u.numeric_data),
			                             &(r_u.data), &(r_u.needed));
		}

		if (found==False)
		{
			/* reply this param doesn't exist */
			r_u.type   = 0x4;
			r_u.size   = 0x0;
			r_u.data   = NULL;
			r_u.numeric_data=0x0;
			r_u.needed = 0x0;
			r_u.status = ERROR_INVALID_PARAMETER;
		}
			
		spoolss_io_r_getprinterdata("", &r_u, rdata, 0);
		if (r_u.data) free(r_u.data);
	}	
}

/********************************************************************
 * api_spoolss_getprinterdata
 *
 * called from the spoolss dispatcher
 ********************************************************************/
static void api_spoolss_getprinterdata(pipes_struct *p, prs_struct *data, 
                                        prs_struct *rdata)
{
	SPOOL_Q_GETPRINTERDATA q_u;

	/* read the stream and fill the struct */
	spoolss_io_q_getprinterdata("", &q_u, data, 0);

	spoolss_reply_getprinterdata(&q_u,rdata);
}

/********************************************************************
 * api_spoolss_reply_closeprinter
 *
 * called from api_spoolss_closeprinter
 ********************************************************************/
static void spoolss_reply_closeprinter(SPOOL_Q_CLOSEPRINTER *q_u, prs_struct *rdata)                                
{
	SPOOL_R_CLOSEPRINTER r_u;
	
	int pnum = find_printer_index_by_hnd(&(q_u->handle));

	clear_handle(&(r_u.handle));

	if (OPEN_HANDLE(pnum))
	{
		Printer[pnum].open=False;
		r_u.status=0x0;	
		spoolss_io_r_closeprinter("",&r_u,rdata,0);
	}
	else
	{
		r_u.status= 0xC0000000 | NT_STATUS_INVALID_HANDLE;	
		DEBUG(3,("Error closing printer handle (pnum=%x)\n", pnum));
	}
	
	spoolss_io_r_closeprinter("",&r_u,rdata,0);
}

/********************************************************************
 * api_spoolss_closeprinter
 *
 * called from the spoolss dispatcher
 ********************************************************************/
static void api_spoolss_closeprinter(pipes_struct *p, prs_struct *data, 
                                      prs_struct *rdata)
{
	SPOOL_Q_CLOSEPRINTER q_u;

	spoolss_io_q_closeprinter("", &q_u, data, 0);

	spoolss_reply_closeprinter(&q_u,rdata);
}

/********************************************************************
 * api_spoolss_reply_rffpcnex
 *
 * called from api_spoolss_rffpcnex (see this to understand)
 ********************************************************************/
static void spoolss_reply_rffpcnex(SPOOL_Q_RFFPCNEX *q_u, prs_struct *rdata)
{
	SPOOL_R_RFFPCNEX r_u;
	
	r_u.status = 0x0000;

	spoolss_io_r_rffpcnex("",&r_u,rdata,0);
}

/********************************************************************
 * api_spoolss_rffpcnex
 * ReplyFindFirstPrinterChangeNotifyEx
 * called from the spoolss dispatcher
 *
 * jfmxxxx: before replying OK: status=0
 * should do a rpc call to the workstation asking ReplyOpenPrinter
 * have to code it, later.
 *
 * in fact ReplyOpenPrinter is the changenotify equivalent on the spoolss pipe
 ********************************************************************/
static void api_spoolss_rffpcnex(pipes_struct *p, prs_struct *data, 
                                  prs_struct *rdata)
{
	SPOOL_Q_RFFPCNEX q_u;
	
	int i,j,k;

	spoolss_io_q_rffpcnex("", &q_u, data, 0);

	/* store the notify value in the printer struct */

	i=find_printer_index_by_hnd(&(q_u.handle));

	Printer[i].number_of_notify=q_u.option.count;

	DEBUG(3,("Copying %x notify option info\n",Printer[i].number_of_notify));

	for (j=0;j<Printer[i].number_of_notify;j++)
	{
		Printer[i].notify_info[j].count=q_u.option.type[j].count;
		Printer[i].notify_info[j].type=q_u.option.type[j].type	;
		
		DEBUG(4,("Copying %x info fields of type %x\n",
		         Printer[i].notify_info[j].count,
			 Printer[i].notify_info[j].type));
		for(k=0;k<Printer[i].notify_info[j].count;k++)
		{
			Printer[i].notify_info[j].fields[k]=q_u.option.type[j].fields[k];
		}
	}
	spoolss_reply_rffpcnex(&q_u,rdata);
}

/*******************************************************************
 * fill a notify_info_data with the servername
 ********************************************************************/
static void spoolss_notify_server_name(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	pstring temp_name;

	snprintf(temp_name, sizeof(temp_name), "\\\\%s", global_myname);

	data->notify_data.data.length=strlen(temp_name);
	ascii_to_unistr(data->notify_data.data.string, temp_name, sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with the servicename
 * jfmxxxx: it's incorrect should be long_printername
 ********************************************************************/
static void spoolss_notify_printer_name(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
/*
	data->notify_data.data.length=strlen(lp_servicename(snum));
	ascii_to_unistr(data->notify_data.data.string, lp_servicename(snum), sizeof(data->notify_data.data.string)-1);
*/
	data->notify_data.data.length=strlen(printer->info_2->printername);
	ascii_to_unistr(data->notify_data.data.string, 
	                printer->info_2->printername, 
			sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with the servicename
 ********************************************************************/
static void spoolss_notify_share_name(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=strlen(lp_servicename(snum));
	ascii_to_unistr(data->notify_data.data.string,
	                lp_servicename(snum), 
			sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with the port name
 ********************************************************************/
static void spoolss_notify_port_name(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	/* even if it's strange, that's consistant in all the code */

	data->notify_data.data.length=strlen(lp_servicename(snum));
	ascii_to_unistr(data->notify_data.data.string,
	                lp_servicename(snum), 
			sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with the printername
 * jfmxxxx: it's incorrect, should be lp_printerdrivername()
 * but it doesn't exist, have to see what to do
 ********************************************************************/
static void spoolss_notify_driver_name(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=strlen(printer->info_2->drivername);
	ascii_to_unistr(data->notify_data.data.string, 
	                printer->info_2->drivername, 
			sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with the comment
 ********************************************************************/
static void spoolss_notify_comment(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=strlen(lp_comment(snum));
	ascii_to_unistr(data->notify_data.data.string,
	                lp_comment(snum),
			sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with the comment
 * jfm:xxxx incorrect, have to create a new smb.conf option
 * location = "Room 1, floor 2, building 3"
 ********************************************************************/
static void spoolss_notify_location(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=strlen(printer->info_2->location);
	ascii_to_unistr(data->notify_data.data.string, 
	                printer->info_2->location, 
			sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with the device mode
 * jfm:xxxx don't to it for know but that's a real problem !!!
 ********************************************************************/
static void spoolss_notify_devmode(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
}

/*******************************************************************
 * fill a notify_info_data with the separator file name
 * jfm:xxxx just return no file could add an option to smb.conf
 * separator file = "separator.txt"
 ********************************************************************/
static void spoolss_notify_sepfile(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=strlen(printer->info_2->sepfile);
	ascii_to_unistr(data->notify_data.data.string, 
	                printer->info_2->sepfile, 
			sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with the print processor
 * jfm:xxxx return always winprint to indicate we don't do anything to it
 ********************************************************************/
static void spoolss_notify_print_processor(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=strlen(printer->info_2->printprocessor);
	ascii_to_unistr(data->notify_data.data.string, 
	                printer->info_2->printprocessor, 
			sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with the print processor options
 * jfm:xxxx send an empty string
 ********************************************************************/
static void spoolss_notify_parameters(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=strlen(printer->info_2->parameters);
	ascii_to_unistr(data->notify_data.data.string, 
	                printer->info_2->parameters, 
			sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with the data type
 * jfm:xxxx always send RAW as data type
 ********************************************************************/
static void spoolss_notify_datatype(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=strlen(printer->info_2->datatype);
	ascii_to_unistr(data->notify_data.data.string, 
	                printer->info_2->datatype, 
			sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with the security descriptor
 * jfm:xxxx send an null pointer to say no security desc
 * have to implement security before !
 ********************************************************************/
static void spoolss_notify_security_desc(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=0;
	data->notify_data.data.string[0]=0x00;
}

/*******************************************************************
 * fill a notify_info_data with the attributes
 * jfm:xxxx a samba printer is always shared
 ********************************************************************/
static void spoolss_notify_attributes(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.value[0] =   PRINTER_ATTRIBUTE_SHARED   \
	                             | PRINTER_ATTRIBUTE_NETWORK  \
				     | PRINTER_ATTRIBUTE_RAW_ONLY ;
}

/*******************************************************************
 * fill a notify_info_data with the priority
 ********************************************************************/
static void spoolss_notify_priority(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.value[0] = printer->info_2->priority;
}

/*******************************************************************
 * fill a notify_info_data with the default priority
 ********************************************************************/
static void spoolss_notify_default_priority(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.value[0] = printer->info_2->default_priority;
}

/*******************************************************************
 * fill a notify_info_data with the start time
 ********************************************************************/
static void spoolss_notify_start_time(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.value[0] = printer->info_2->starttime;
}

/*******************************************************************
 * fill a notify_info_data with the until time
 ********************************************************************/
static void spoolss_notify_until_time(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.value[0] = printer->info_2->untiltime;
}

/*******************************************************************
 * fill a notify_info_data with the status
 ********************************************************************/
static void spoolss_notify_status(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	int count;
	print_queue_struct *q=NULL;
	print_status_struct status;

	bzero(&status,sizeof(status));

	count=get_printqueue(snum, conn, &q, &status);

	data->notify_data.value[0]=(uint32) status.status;
	if (q) free(q);
}

/*******************************************************************
 * fill a notify_info_data with the number of jobs queued
 ********************************************************************/
static void spoolss_notify_cjobs(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	print_queue_struct *q=NULL;
	print_status_struct status;

	bzero(&status,sizeof(status));

	data->notify_data.value[0]=get_printqueue(snum, conn, &q, &status);
	if (q) free(q);
}

/*******************************************************************
 * fill a notify_info_data with the average ppm
 ********************************************************************/
static void spoolss_notify_average_ppm(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	/* always respond 8 pages per minutes */
	/* a little hard ! */
	data->notify_data.value[0] = printer->info_2->averageppm;
}

/*******************************************************************
 * fill a notify_info_data with 
 ********************************************************************/
static void spoolss_notify_username(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=strlen(queue->user);
	ascii_to_unistr(data->notify_data.data.string, queue->user, sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with 
 ********************************************************************/
static void spoolss_notify_job_status(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.value[0]=queue->status;
}

/*******************************************************************
 * fill a notify_info_data with 
 ********************************************************************/
static void spoolss_notify_job_name(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=strlen(queue->file);
	ascii_to_unistr(data->notify_data.data.string, queue->file, sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with 
 ********************************************************************/
static void spoolss_notify_job_status_string(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.data.length=strlen("En attente");
	ascii_to_unistr(data->notify_data.data.string, "En attente", sizeof(data->notify_data.data.string)-1);
}

/*******************************************************************
 * fill a notify_info_data with 
 ********************************************************************/
static void spoolss_notify_job_time(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.value[0]=0x0;
}

/*******************************************************************
 * fill a notify_info_data with 
 ********************************************************************/
static void spoolss_notify_job_size(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.value[0]=queue->size;
}

/*******************************************************************
 * fill a notify_info_data with 
 ********************************************************************/
static void spoolss_notify_job_position(connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer)
{
	data->notify_data.value[0]=queue->job;
}

#define END 65535

struct s_notify_info_data_table notify_info_data_table[] =
{
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_SERVER_NAME,         "PRINTER_NOTIFY_SERVER_NAME",         POINTER,   spoolss_notify_server_name },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_PRINTER_NAME,        "PRINTER_NOTIFY_PRINTER_NAME",        POINTER,   spoolss_notify_printer_name },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_SHARE_NAME,          "PRINTER_NOTIFY_SHARE_NAME",          POINTER,   spoolss_notify_share_name },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_PORT_NAME,           "PRINTER_NOTIFY_PORT_NAME",           POINTER,   spoolss_notify_port_name },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_DRIVER_NAME,         "PRINTER_NOTIFY_DRIVER_NAME",         POINTER,   spoolss_notify_driver_name },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_COMMENT,             "PRINTER_NOTIFY_COMMENT",             POINTER,   spoolss_notify_comment },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_LOCATION,            "PRINTER_NOTIFY_LOCATION",            POINTER,   spoolss_notify_location },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_DEVMODE,             "PRINTER_NOTIFY_DEVMODE",             POINTER,   spoolss_notify_devmode },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_SEPFILE,             "PRINTER_NOTIFY_SEPFILE",             POINTER,   spoolss_notify_sepfile },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_PRINT_PROCESSOR,     "PRINTER_NOTIFY_PRINT_PROCESSOR",     POINTER,   spoolss_notify_print_processor },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_PARAMETERS,          "PRINTER_NOTIFY_PARAMETERS",          POINTER,   spoolss_notify_parameters },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_DATATYPE,            "PRINTER_NOTIFY_DATATYPE",            POINTER,   spoolss_notify_datatype },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_SECURITY_DESCRIPTOR, "PRINTER_NOTIFY_SECURITY_DESCRIPTOR", POINTER,   spoolss_notify_security_desc },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_ATTRIBUTES,          "PRINTER_NOTIFY_ATTRIBUTES",          ONE_VALUE, spoolss_notify_attributes },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_PRIORITY,            "PRINTER_NOTIFY_PRIORITY",            ONE_VALUE, spoolss_notify_priority },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_DEFAULT_PRIORITY,    "PRINTER_NOTIFY_DEFAULT_PRIORITY",    ONE_VALUE, spoolss_notify_default_priority },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_START_TIME,          "PRINTER_NOTIFY_START_TIME",          ONE_VALUE, spoolss_notify_start_time },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_UNTIL_TIME,          "PRINTER_NOTIFY_UNTIL_TIME",          ONE_VALUE, spoolss_notify_until_time },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_STATUS,              "PRINTER_NOTIFY_STATUS",              ONE_VALUE, spoolss_notify_status },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_STATUS_STRING,       "PRINTER_NOTIFY_STATUS_STRING",       POINTER,   NULL },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_CJOBS,               "PRINTER_NOTIFY_CJOBS",               ONE_VALUE, spoolss_notify_cjobs },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_AVERAGE_PPM,         "PRINTER_NOTIFY_AVERAGE_PPM",         ONE_VALUE, spoolss_notify_average_ppm },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_TOTAL_PAGES,         "PRINTER_NOTIFY_TOTAL_PAGES",         POINTER,   NULL },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_PAGES_PRINTED,       "PRINTER_NOTIFY_PAGES_PRINTED",       POINTER,   NULL },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_TOTAL_BYTES,         "PRINTER_NOTIFY_TOTAL_BYTES",         POINTER,   NULL },
{ PRINTER_NOTIFY_TYPE, PRINTER_NOTIFY_BYTES_PRINTED,       "PRINTER_NOTIFY_BYTES_PRINTED",       POINTER,   NULL },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_PRINTER_NAME,            "JOB_NOTIFY_PRINTER_NAME",            POINTER,   spoolss_notify_printer_name },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_MACHINE_NAME,            "JOB_NOTIFY_MACHINE_NAME",            POINTER,   spoolss_notify_server_name },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_PORT_NAME,               "JOB_NOTIFY_PORT_NAME",               POINTER,   spoolss_notify_port_name },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_USER_NAME,               "JOB_NOTIFY_USER_NAME",               POINTER,   spoolss_notify_username },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_NOTIFY_NAME,             "JOB_NOTIFY_NOTIFY_NAME",             POINTER,   spoolss_notify_username },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_DATATYPE,                "JOB_NOTIFY_DATATYPE",                POINTER,   spoolss_notify_datatype },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_PRINT_PROCESSOR,         "JOB_NOTIFY_PRINT_PROCESSOR",         POINTER,   spoolss_notify_print_processor },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_PARAMETERS,              "JOB_NOTIFY_PARAMETERS",              POINTER,   spoolss_notify_parameters },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_DRIVER_NAME,             "JOB_NOTIFY_DRIVER_NAME",             POINTER,   spoolss_notify_driver_name },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_DEVMODE,                 "JOB_NOTIFY_DEVMODE",                 POINTER,   spoolss_notify_devmode },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_STATUS,                  "JOB_NOTIFY_STATUS",                  ONE_VALUE, spoolss_notify_job_status },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_STATUS_STRING,           "JOB_NOTIFY_STATUS_STRING",           POINTER,   spoolss_notify_job_status_string },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_SECURITY_DESCRIPTOR,     "JOB_NOTIFY_SECURITY_DESCRIPTOR",     POINTER,   NULL },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_DOCUMENT,                "JOB_NOTIFY_DOCUMENT",                POINTER,   spoolss_notify_job_name },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_PRIORITY,                "JOB_NOTIFY_PRIORITY",                ONE_VALUE, spoolss_notify_priority },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_POSITION,                "JOB_NOTIFY_POSITION",                ONE_VALUE, spoolss_notify_job_position },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_SUBMITTED,               "JOB_NOTIFY_SUBMITTED",               POINTER,   NULL },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_START_TIME,              "JOB_NOTIFY_START_TIME",              ONE_VALUE, spoolss_notify_start_time },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_UNTIL_TIME,              "JOB_NOTIFY_UNTIL_TIME",              ONE_VALUE, spoolss_notify_until_time },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_TIME,                    "JOB_NOTIFY_TIME",                    ONE_VALUE, spoolss_notify_job_time },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_TOTAL_PAGES,             "JOB_NOTIFY_TOTAL_PAGES",             ONE_VALUE, NULL },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_PAGES_PRINTED,           "JOB_NOTIFY_PAGES_PRINTED",           ONE_VALUE, NULL },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_TOTAL_BYTES,             "JOB_NOTIFY_TOTAL_BYTES",             ONE_VALUE, spoolss_notify_job_size },
{ JOB_NOTIFY_TYPE,     JOB_NOTIFY_BYTES_PRINTED,           "JOB_NOTIFY_BYTES_PRINTED",           ONE_VALUE, NULL },
{ END,                 END,                                "",                                   END,       NULL }
};

/*******************************************************************
return the size of info_data structure
********************************************************************/  
uint32 size_of_notify_info_data(uint16 type, uint16 field)
{
	int i=0;

	while (notify_info_data_table[i].type != END)
	{
		if ( (notify_info_data_table[i].type == type ) &&
		     (notify_info_data_table[i].field == field ) )
		{
			return (notify_info_data_table[i].size);
			continue;
		}
		i++;
	}
	return (65535);
}

/*******************************************************************
return the type of notify_info_data
********************************************************************/  
BOOL type_of_notify_info_data(uint16 type, uint16 field)
{
	int i=0;

	while (notify_info_data_table[i].type != END)
	{
		if ( (notify_info_data_table[i].type == type ) &&
		     (notify_info_data_table[i].field == field ) )
		{
			if (notify_info_data_table[i].size == POINTER)
			{
				return (False);
			}
			else
			{
				return (True);
			}
			continue;
		}
		i++;
	}
	return (False);
}

/****************************************************************************
****************************************************************************/
static int search_notify(uint16 type, uint16 field, int *value)
{	
	int j;
	BOOL found;

	DEBUG(4,("\tsearch_notify: in\n"));	
	for (j=0, found=False; found==False && notify_info_data_table[j].type != END ; j++)
	{
		if ( (notify_info_data_table[j].type  == type  ) &&
		     (notify_info_data_table[j].field == field ) )
		{
			found=True;
		}
	}
	*value=--j;

	if ( found && (notify_info_data_table[j].fn != NULL) )
	{
		DEBUG(4,("\tsearch_notify: out TRUE\n"));
		return (True);
	}
	else
	{
		DEBUG(4,("\tsearch_notify: out FALSE\n"));
		return (False);	
	}
}

/****************************************************************************
****************************************************************************/
static void construct_info_data(SPOOL_NOTIFY_INFO_DATA *info_data, uint16 type, uint16 field, int id)
{
	DEBUG(4,("\tconstruct_info_data: in\n"));
	info_data->type     = type;
	info_data->field    = field;
	info_data->id       = id;
	info_data->size     = size_of_notify_info_data(type, field);
	info_data->enc_type = type_of_notify_info_data(type, field);
	DEBUG(4,("\tconstruct_info_data: out\n"));
}


/*******************************************************************
 *
 * fill a notify_info struct with info asked
 * 
 ********************************************************************/
static void construct_notify_printer_info(SPOOL_NOTIFY_INFO *info, int pnum, 
					  int snum, int i, uint32 id, connection_struct *conn)
{

	int k,j;
	uint16 type;
	uint16 field;

	SPOOL_NOTIFY_INFO_DATA *info_data;
	print_queue_struct *queue=NULL;
	NT_PRINTER_INFO_LEVEL printer;
	
	DEBUG(4,("construct_notify_printer_info\n"));
	
	info_data=&(info->data[info->count]);
	
	type = Printer[pnum].notify_info[i].type;

	DEBUGADD(4,("Notify number %d -> number of notify info: %d\n",i,Printer[pnum].notify_info[i].count));
	
	if (!get_a_printer(&printer, 2, lp_servicename(snum)))
	{
		
		for(k=0; k<Printer[pnum].notify_info[i].count; k++)
		{
			field = Printer[pnum].notify_info[i].fields[k];
			DEBUGADD(4,("notify [%d]: type [%x], field [%x]\n", k, type, field));

			if (search_notify(type, field, &j) )
			{
				DEBUGADD(4,("j=[%d]:%s\n", j, notify_info_data_table[j].name));
				construct_info_data(info_data, type, field, id);
			
				DEBUGADD(4,("notify_info_data_table: in\n"));
				notify_info_data_table[j].fn(conn,snum, info_data, queue, &printer);
				DEBUGADD(4,("notify_info_data_table: out\n"));
				info->count++;
				info_data=&(info->data[info->count]);
			}
		}
	
		free_a_printer(printer, 2);
	}
}

/*******************************************************************
 *
 * fill a notify_info struct with info asked
 * 
 ********************************************************************/
static void construct_notify_jobs_info(print_queue_struct *queue, SPOOL_NOTIFY_INFO *info,
                                       int pnum, int snum, int i, uint32 id, connection_struct *conn)
{

	int k,j;
	uint16 type;
	uint16 field;

	SPOOL_NOTIFY_INFO_DATA *info_data;
	NT_PRINTER_INFO_LEVEL printer;
	
	DEBUG(4,("construct_notify_jobs_info\n"));
	info_data=&(info->data[info->count]);
	
	type = Printer[pnum].notify_info[i].type;

	DEBUGADD(4,("Notify number %d -> number of notify info: %d\n",i,Printer[pnum].notify_info[i].count));

	if (!get_a_printer(&printer, 2, lp_servicename(snum)))
	{	
		for(k=0; k<Printer[pnum].notify_info[i].count; k++)
		{
			field = Printer[pnum].notify_info[i].fields[k];
			DEBUGADD(4,("notify [%d]: type [%x], field [%x]\n",k, type, field));

			if (search_notify(type, field, &j) )
			{
				DEBUGADD(4,("j=[%d]:%s\n", j, notify_info_data_table[j].name));
				construct_info_data(info_data, type, field, id);
				DEBUGADD(4,("notify_info_data_table: in\n"));
				notify_info_data_table[j].fn(conn, snum, info_data, queue, &printer);
				DEBUGADD(4,("notify_info_data_table: out\n"));
				info->count++;
				info_data=&(info->data[info->count]);
			}
		}
		free_a_printer(printer, 2);
	}
}


/*******************************************************************
 *
 * enumerate all printers on the printserver
 * fill a notify_info struct with info asked
 * 
 ********************************************************************/
static void printserver_notify_info(PRINTER_HND *hnd, SPOOL_NOTIFY_INFO *info, connection_struct *conn)
{
	int snum;
	int pnum=find_printer_index_by_hnd(hnd);
	int n_services=lp_numservices();
	int i=0;
	uint32 id=1;
	info->count=0;

	DEBUG(4,("Enumerating printers\n"));

	for (i=0; i<Printer[pnum].number_of_notify; i++)
	{
	 if ( Printer[pnum].notify_info[i].type == PRINTER_NOTIFY_TYPE )
	 {
	  for (snum=0; snum<n_services; snum++)
	  {
	   if ( lp_browseable(snum) && lp_snum_ok(snum) && lp_print_ok(snum) )
	   {
		construct_notify_printer_info(info, pnum, snum, i, id, conn);
		id++;
	   }
	  }
	 }
	}
	DEBUG(4,("All printers enumerated\n"));
}

/*******************************************************************
 *
 * fill a notify_info struct with info asked
 * 
 ********************************************************************/
static void printer_notify_info(PRINTER_HND *hnd, SPOOL_NOTIFY_INFO *info, connection_struct *conn)
{
	int snum;
	int pnum=find_printer_index_by_hnd(hnd);
	int i=0, j;
	uint32 id=0xFFFF;
	
	info->count=0;

	if (get_printer_snum(hnd, &snum) )
	{
		for (i=0; i<Printer[pnum].number_of_notify; i++)
		{
	 	 switch ( Printer[pnum].notify_info[i].type )
		 {
		  case PRINTER_NOTIFY_TYPE:
		   {
		  	construct_notify_printer_info(info, pnum, snum, i, id, conn);
			id--;
			break;
		   }
		  case JOB_NOTIFY_TYPE:
		   {
		   	int count;
			print_queue_struct *queue=NULL;
			print_status_struct status;
			bzero(&status, sizeof(status));	
			count=get_printqueue(snum, conn, &queue, &status);
			for (j=0; j<count; j++)
			{
			   	construct_notify_jobs_info(&(queue[j]), info, pnum, snum, i, queue[j].job, conn);
			}
			if (queue) free(queue);
			break;
		   }
		 }
		}
	}
}

/********************************************************************
 * api_spoolss_reply_rfnpcnex
 *
 * called from api_spoolss_rfnpcnex (see this to understand)
 ********************************************************************/
static void spoolss_reply_rfnpcnex(SPOOL_Q_RFNPCNEX *q_u, prs_struct *rdata, connection_struct *conn)
{
	SPOOL_R_RFNPCNEX r_u;
	int pnum=find_printer_index_by_hnd(&(q_u->handle));

	if (OPEN_HANDLE(pnum))
	{
		DEBUG(4,("Printer %x of type %x\n",pnum,Printer[pnum].printer_type));
		switch (Printer[pnum].printer_type)
		{
		case PRINTER_HANDLE_IS_PRINTSERVER:
			printserver_notify_info(&(q_u->handle), &(r_u.info), conn);
			break;
		case PRINTER_HANDLE_IS_PRINTER:
			printer_notify_info(&(q_u->handle), &(r_u.info), conn);
			break;
		}
		
		spoolss_io_r_rfnpcnex("", &r_u, rdata, 0);
	}
}

/********************************************************************
 * api_spoolss_rfnpcnex
 * ReplyFindNextPrinterChangeNotifyEx
 * called from the spoolss dispatcher
 *
 ********************************************************************/
static void api_spoolss_rfnpcnex(pipes_struct *p, prs_struct *data, 
                                  prs_struct *rdata)
{
	SPOOL_Q_RFNPCNEX q_u;

	spoolss_io_q_rfnpcnex("", &q_u, data, 0);

	spoolss_reply_rfnpcnex(&q_u, rdata, p->conn);
}

/********************************************************************
 * construct_printer_info_0
 * fill a printer_info_1 struct
 ********************************************************************/
static void construct_printer_info_0(PRINTER_INFO_0 *printer,int snum, pstring servername, connection_struct *conn)
{
	pstring chaine;
	int count;
	
	print_queue_struct *queue=NULL;
	print_status_struct status;
	bzero(&status,sizeof(status));	
	count=get_printqueue(snum, conn ,&queue,&status);
	
	/* the description and the name are of the form \\server\share */
	slprintf(chaine,sizeof(chaine)-1,"\\\\%s\\%s",servername, lp_servicename(snum));
							    
	make_unistr(&(printer->printername), chaine);
	
	slprintf(chaine,sizeof(chaine)-1,"\\\\%s", servername);
	make_unistr(&(printer->servername), chaine);
	
	printer->cjobs = count;
	printer->attributes =   PRINTER_ATTRIBUTE_SHARED   \
	                      | PRINTER_ATTRIBUTE_NETWORK  \
			      | PRINTER_ATTRIBUTE_RAW_ONLY ;
	printer->unknown0     = 0x1; /* pointer */
	printer->unknown1     = 0x000A07CE; /* don't known */
	printer->unknown2     = 0x00020005;
	printer->unknown3     = 0x0006000D;
	printer->unknown4     = 0x02180026;
	printer->unknown5     = 0x09;
	printer->unknown6     = 0x36;
	printer->majorversion = 0x0004; /* NT 4 */
	printer->buildversion = 0x0565; /* build 1381 */
	printer->unknown7     = 0x1;
	printer->unknown8     = 0x0;
	printer->unknown9     = 0x2;
	printer->unknown10    = 0x3;
	printer->unknown11    = 0x0;
	printer->unknown12    = 0x0;
	printer->unknown13    = 0x0;
	printer->unknown14    = 0x1;
	printer->unknown15    = 0x024a; /*586 Pentium ? */
	printer->unknown16    = 0x0;
	printer->unknown17    = 0x423ed444;
	printer->unknown18    = 0x0;
	printer->status       = status.status;
	printer->unknown20    = 0x0;
	printer->unknown21    = 0x0648;
	printer->unknown22    = 0x0;
	printer->unknown23    = 0x5;
	if (queue) free(queue);
	
}

/********************************************************************
 * construct_printer_info_1
 * fill a printer_info_1 struct
 ********************************************************************/
static BOOL construct_printer_info_1(PRINTER_INFO_1 *printer,int snum, pstring servername, connection_struct *conn)
{
	pstring chaine;
	NT_PRINTER_INFO_LEVEL ntprinter;
	
	if (get_a_printer(&ntprinter, 2, lp_servicename(snum)) != 0)
	{
		return (False);
	}
	
	printer->flags=PRINTER_ENUM_NAME;

	/* the description and the name are of the form \\server\share */
	slprintf(chaine,sizeof(chaine)-1,"\\\\%s\\%s,%s,%s",servername,
							    ntprinter.info_2->printername,
							    ntprinter.info_2->drivername,
							    lp_comment(snum));
	make_unistr(&(printer->description), chaine);
	
	slprintf(chaine,sizeof(chaine)-1,"\\\\%s\\%s", servername, ntprinter.info_2->printername);
	make_unistr(&(printer->name), chaine);
	
	make_unistr(&(printer->comment), lp_comment(snum));
	
	free_a_printer(ntprinter, 2);
	return (True);
}

/****************************************************************************
****************************************************************************/
static void construct_dev_mode(DEVICEMODE *devmode, int snum, char *servername)
{
	char adevice[32];
	char aform[32];
	NT_PRINTER_INFO_LEVEL printer;	
	NT_DEVICEMODE *ntdevmode;

	DEBUG(7,("construct_dev_mode\n"));
	
	bzero(&(devmode->devicename), 2*sizeof(adevice));
	bzero(&(devmode->formname), 2*sizeof(aform));

	DEBUGADD(8,("getting printer characteristics\n"));

	get_a_printer(&printer, 2, lp_servicename(snum));
	ntdevmode=(printer.info_2)->devmode;

	DEBUGADD(8,("loading DEVICEMODE\n"));
	snprintf(adevice, sizeof(adevice), "\\\\%s\\%s", global_myname, 
	                                                 printer.info_2->printername);
	make_unistr(&(devmode->devicename), adevice);

	snprintf(aform, sizeof(aform), ntdevmode->formname);
	make_unistr(&(devmode->formname), aform);

	devmode->specversion      = ntdevmode->specversion;
	devmode->driverversion    = ntdevmode->driverversion;
	devmode->size             = ntdevmode->size;
	devmode->driverextra      = ntdevmode->driverextra;
	devmode->fields           = ntdevmode->fields;
				    
	devmode->orientation      = ntdevmode->orientation;	
	devmode->papersize        = ntdevmode->papersize;
	devmode->paperlength      = ntdevmode->paperlength;
	devmode->paperwidth       = ntdevmode->paperwidth;
	devmode->scale            = ntdevmode->scale;
	devmode->copies           = ntdevmode->copies;
	devmode->defaultsource    = ntdevmode->defaultsource;
	devmode->printquality     = ntdevmode->printquality;
	devmode->color            = ntdevmode->color;
	devmode->duplex           = ntdevmode->duplex;
	devmode->yresolution      = ntdevmode->yresolution;
	devmode->ttoption         = ntdevmode->ttoption;
	devmode->collate          = ntdevmode->collate;
	devmode->icmmethod        = ntdevmode->icmmethod;
	devmode->icmintent        = ntdevmode->icmintent;
	devmode->mediatype        = ntdevmode->mediatype;
	devmode->dithertype       = ntdevmode->dithertype;

	if (ntdevmode->private != NULL)
	{
		devmode->private=(uint8 *)malloc(devmode->driverextra*sizeof(uint8));
		memcpy(devmode->private, ntdevmode->private, devmode->driverextra);
	}

	free_a_printer(printer, 2);
}

/********************************************************************
 * construct_printer_info_2
 * fill a printer_info_2 struct
 ********************************************************************/
static BOOL construct_printer_info_2(PRINTER_INFO_2 *printer, int snum, pstring servername, connection_struct *conn)
{
	pstring chaine;
	int count;
	DEVICEMODE *devmode;
	NT_PRINTER_INFO_LEVEL ntprinter;
	
	print_queue_struct *queue=NULL;
	print_status_struct status;
	bzero(&status, sizeof(status));	
	count=get_printqueue(snum, conn, &queue, &status);

	if (get_a_printer(&ntprinter, 2, lp_servicename(snum)) !=0 )
	{
		return (False);
	}	

	snprintf(chaine, sizeof(chaine)-1, "\\\\%s", servername);
	make_unistr(&(printer->servername), chaine);			/* servername*/
	
	snprintf(chaine, sizeof(chaine)-1, "\\\\%s\\%s", servername, ntprinter.info_2->printername);
	make_unistr(&(printer->printername), chaine);			/* printername*/

	make_unistr(&(printer->sharename),      lp_servicename(snum));	/* sharename */

	make_unistr(&(printer->portname),       lp_servicename(snum));		/* port */	
	make_unistr(&(printer->drivername),     ntprinter.info_2->drivername);	/* drivername */
		
	make_unistr(&(printer->comment),        ntprinter.info_2->comment);	/* comment */	
	make_unistr(&(printer->location),       ntprinter.info_2->location);	/* location */	
	make_unistr(&(printer->sepfile),        ntprinter.info_2->sepfile);	/* separator file */
	make_unistr(&(printer->printprocessor), ntprinter.info_2->printprocessor);/* print processor */
	make_unistr(&(printer->datatype),       ntprinter.info_2->datatype);	/* datatype */	
	make_unistr(&(printer->parameters),     ntprinter.info_2->parameters);	/* parameters (of print processor) */	

	printer->attributes =   PRINTER_ATTRIBUTE_SHARED   \
	                      | PRINTER_ATTRIBUTE_NETWORK  \
			      | PRINTER_ATTRIBUTE_RAW_ONLY ;		/* attributes */

	printer->priority        = ntprinter.info_2->priority;		/* priority */	
	printer->defaultpriority = ntprinter.info_2->default_priority;	/* default priority */
	printer->starttime       = ntprinter.info_2->starttime;		/* starttime */
	printer->untiltime       = ntprinter.info_2->untiltime;		/* untiltime */
	printer->status          = status.status;			/* status */
	printer->cjobs           = count;				/* jobs */
	printer->averageppm      = ntprinter.info_2->averageppm;	/* average pages per minute */
			
	devmode=(DEVICEMODE *)malloc(sizeof(DEVICEMODE));
	ZERO_STRUCTP(devmode);	
	construct_dev_mode(devmode, snum, servername);			
	printer->devmode=devmode;
	
	if (queue) free(queue);
	free_a_printer(ntprinter, 2);
	return (True);
}

/********************************************************************
 * enum_printer_info_1
 * glue between spoolss_reply_enumprinters and construct_printer_info_1
 ********************************************************************/
static BOOL enum_printer_info_1(PRINTER_INFO_1 **printer, int snum, int number, connection_struct *conn)
{
	pstring servername;

	*printer=(PRINTER_INFO_1 *)malloc(sizeof(PRINTER_INFO_1));
	DEBUG(4,("Allocated memory for ONE PRINTER_INFO_1 at [%p]\n", *printer));	
	pstrcpy(servername, global_myname);
	if (!construct_printer_info_1(*printer, snum, servername, conn))
	{
		free(*printer);
		return (False);
	}
	else
	{
		return (True);
	}
}

/********************************************************************
 * enum_printer_info_2
 * glue between spoolss_reply_enumprinters and construct_printer_info_2
 ********************************************************************/
static BOOL enum_printer_info_2(PRINTER_INFO_2 **printer, int snum, int number, connection_struct *conn)
{
	pstring servername;

	*printer=(PRINTER_INFO_2 *)malloc(sizeof(PRINTER_INFO_2));
	DEBUG(4,("Allocated memory for ONE PRINTER_INFO_2 at [%p]\n", *printer));	
	pstrcpy(servername, global_myname);
	if (!construct_printer_info_2(*printer, snum, servername, conn))
	{
		free(*printer);
		return (False);
	}
	else
	{
		return (True);
	}
}

/********************************************************************
 * api_spoolss_reply_enumprinters
 *
 * called from api_spoolss_enumprinters (see this to understand)
 ********************************************************************/
static void enum_all_printers_info_1(PRINTER_INFO_1 ***printers, uint32 *number, connection_struct *conn)
{
	int snum;
	int n_services=lp_numservices();
	*printers=NULL;
	*number=0;

	for (snum=0;snum<n_services; snum++)
	{
		if (lp_browseable(snum) && lp_snum_ok(snum) && lp_print_ok(snum) )
		{
			DEBUG(4,("Found a printer: %s[%x]\n",lp_servicename(snum),snum));
			*printers=Realloc(*printers, (*number+1)*sizeof(PRINTER_INFO_1 *));			
			DEBUG(4,("ReAlloced memory for [%d] PRINTER_INFO_1 pointers at [%p]\n", *number+1, *printers));		
			if (enum_printer_info_1( &((*printers)[*number]), snum, *number, conn) )
			{			
				(*number)++;
			}
		}
	}
}

/********************************************************************
 * api_spoolss_reply_enumprinters
 *
 * called from api_spoolss_enumprinters (see this to understand)
 ********************************************************************/
static void enum_all_printers_info_2(PRINTER_INFO_2 ***printers, uint32 *number, connection_struct *conn)
{
	int snum;
	int n_services=lp_numservices();
	*printers=NULL;
	*number=0;

	for (snum=0;snum<n_services; snum++)
	{
		if (lp_browseable(snum) && lp_snum_ok(snum) && lp_print_ok(snum) )
		{
			DEBUG(4,("Found a printer: %s[%x]\n",lp_servicename(snum),snum));
			*printers=Realloc(*printers, (*number+1)*sizeof(PRINTER_INFO_2 *));			
			DEBUG(4,("ReAlloced memory for [%d] PRINTER_INFO_1 pointers at [%p]\n", *number+1, *printers));			
			if (enum_printer_info_2( &((*printers)[*number]), snum, *number, conn) )
			{			
				(*number)++;
			}
		}
	}
}

/****************************************************************************
****************************************************************************/
static void free_enum_printers_info_1(PRINTER_INFO_1 **printers, uint32 total)
{
	int number=0;	
	if (printers != NULL)
	{			
		for (number=0; number<total; number++)
		{
			if (printers[number] != NULL)
			{
				free(printers[number]);
			}
		}
		free(printers);
	}
}

/****************************************************************************
****************************************************************************/
static void free_enum_printers_info_2(PRINTER_INFO_2 **printers, uint32 total)
{
	int number=0;		
	if (printers != NULL)
	{	
		for (number=0; number<total; number++)
		{		
			if (printers[number] != NULL)
			{
				free(printers[number]->devmode);
				free(printers[number]);
			}
		}
		free(printers);
	}
}

/****************************************************************************
****************************************************************************/
static void free_enum_printers_info(SPOOL_R_ENUMPRINTERS *r_u)
{	
	DEBUG(4,("free_enum_printers_info: [%d] structs to free at level [%d]\n", r_u->returned, r_u->level));
	switch (r_u->level)
	{
		case 1:			
			free_enum_printers_info_1(r_u->printer.printers_1, r_u->returned);
			break;
		case 2:
			free_enum_printers_info_2(r_u->printer.printers_2, r_u->returned);
			break;
	}
}

/********************************************************************
 * api_spoolss_reply_enumprinters
 *
 * called from api_spoolss_enumprinters (see this to understand)
 ********************************************************************/
static void spoolss_reply_enumprinters(SPOOL_Q_ENUMPRINTERS *q_u, prs_struct *rdata, connection_struct *conn)
{
	SPOOL_R_ENUMPRINTERS r_u;
	
	DEBUG(4,("Enumerating printers\n"));

	memcpy(r_u.servername.buffer,q_u->servername.buffer,2*q_u->servername.uni_str_len);
	r_u.servername.buffer[q_u->servername.uni_str_len]=0x0000;
	r_u.returned=0;

	switch (q_u->level)
	{
		case 1:
			if ( (q_u->flags==PRINTER_ENUM_NAME) || (q_u->flags==PRINTER_ENUM_NETWORK) )
				/*if (is_a_printerserver(q_u->servername))*/
					enum_all_printers_info_1(&(r_u.printer.printers_1), &(r_u.returned), conn );
				/*else	
					enum_one_printer_info_1(&r_u);*/
			break;
		case 2:
			if ( (q_u->flags==PRINTER_ENUM_NAME) || (q_u->flags==PRINTER_ENUM_NETWORK) )
				/*if (is_a_printerserver(q_u->servername))*/
					enum_all_printers_info_2(&(r_u.printer.printers_2), &(r_u.returned), conn );
				/*else	
					enum_one_printer_info_2(&r_u);*/
			break;
		case 3:		/* doesn't exist */
			break;
		case 4:		/* can't, always on local machine */
			break;
		case 5:
			break;
			
	}
	DEBUG(4,("%d printers enumerated\n", r_u.returned));
	r_u.offered=q_u->buffer.size;
	r_u.level=q_u->level;
	r_u.status=0x0000;

	spoolss_io_r_enumprinters("",&r_u,rdata,0);
	free_enum_printers_info(&r_u);
}

/********************************************************************
 * api_spoolss_enumprinters
 * called from the spoolss dispatcher
 *
 ********************************************************************/
static void api_spoolss_enumprinters(pipes_struct *p, prs_struct *data, 
                                     prs_struct *rdata)
{
	SPOOL_Q_ENUMPRINTERS q_u;

	spoolss_io_q_enumprinters("", &q_u, data, 0);

	spoolss_reply_enumprinters(&q_u, rdata, p->conn);
}


/****************************************************************************
****************************************************************************/
static void spoolss_reply_getprinter(SPOOL_Q_GETPRINTER *q_u, prs_struct *rdata, connection_struct *conn)
{
	SPOOL_R_GETPRINTER r_u;
	int snum;
	pstring servername;
	
	pstrcpy(servername, global_myname);

	get_printer_snum(&(q_u->handle),&snum);
	
	switch (q_u->level)
	{
		case 0:
		{ 
			PRINTER_INFO_0 *printer;
			
			printer=(PRINTER_INFO_0 *)malloc(sizeof(PRINTER_INFO_0));
			
			construct_printer_info_0(printer, snum, servername, conn);
			r_u.printer.info0=printer;
			r_u.status=0x0000;
			r_u.offered=q_u->offered;
			r_u.level=q_u->level;
			
			spoolss_io_r_getprinter("",&r_u,rdata,0);
			
			free(printer);
			
			break;
		}
		case 1:
		{
			PRINTER_INFO_1 *printer;
			
			printer=(PRINTER_INFO_1 *)malloc(sizeof(PRINTER_INFO_1));

			construct_printer_info_1(printer, snum, servername, conn);

			r_u.printer.info1=printer;			
			r_u.status=0x0000;
			r_u.offered=q_u->offered;
			r_u.level=q_u->level;
			spoolss_io_r_getprinter("",&r_u,rdata,0);
			
			free(printer);
				
			break;
		}
		case 2:
		{
			PRINTER_INFO_2 *printer;
			
			printer=(PRINTER_INFO_2 *)malloc(sizeof(PRINTER_INFO_2));	
			construct_printer_info_2(printer, snum, servername, conn);
			
			r_u.printer.info2=printer;	
			r_u.status=0x0000;
			r_u.offered=q_u->offered;
			r_u.level=q_u->level;
			spoolss_io_r_getprinter("",&r_u,rdata,0);
			
			free(printer->devmode);
			free(printer);
				
			break;
		}
	}
}

/********************************************************************
 * api_spoolss_getprinter
 * called from the spoolss dispatcher
 *
 ********************************************************************/
static void api_spoolss_getprinter(pipes_struct *p, prs_struct *data, 
                                   prs_struct *rdata)
{
	SPOOL_Q_GETPRINTER q_u;
	
	spoolss_io_q_getprinter("", &q_u, data, 0);

	spoolss_reply_getprinter(&q_u, rdata, p->conn);
}

/********************************************************************
 * construct_printer_driver_info_1
 * fill a construct_printer_driver_info_1 struct
 ********************************************************************/
static void fill_printer_driver_info_1(DRIVER_INFO_1 *info, 
                                       NT_PRINTER_DRIVER_INFO_LEVEL driver, 
				       pstring servername, fstring architecture)
{
	make_unistr( &(info->name), driver.info_3->name);
}

static void construct_printer_driver_info_1(DRIVER_INFO_1 *info, int snum, 
                                            pstring servername, fstring architecture)
{	
	NT_PRINTER_INFO_LEVEL printer;
	NT_PRINTER_DRIVER_INFO_LEVEL driver;

	get_a_printer(&printer, 2, lp_servicename(snum) );
	get_a_printer_driver(&driver, 3, printer.info_2->drivername, architecture);	
	
	fill_printer_driver_info_1(info, driver, servername, architecture);
	
	free_a_printer_driver(driver, 3);
	free_a_printer(printer, 2);
}

/********************************************************************
 * construct_printer_driver_info_2
 * fill a printer_info_2 struct
 ********************************************************************/
static void fill_printer_driver_info_2(DRIVER_INFO_2 *info, 
                                       NT_PRINTER_DRIVER_INFO_LEVEL driver, 
				       pstring servername, fstring architecture)
{
	pstring where;
	pstring temp_driverpath;
	pstring temp_datafile;
	pstring temp_configfile;
	fstring short_archi;

	get_short_archi(short_archi,architecture);
	
	snprintf(where,sizeof(where)-1,"\\\\%s\\print$\\%s\\", servername, short_archi);

	info->version=driver.info_3->cversion;

	make_unistr( &(info->name),         driver.info_3->name );
	make_unistr( &(info->architecture), architecture );
	
	snprintf(temp_driverpath, sizeof(temp_driverpath)-1, "%s%s", where, 
	         driver.info_3->driverpath);
	make_unistr( &(info->driverpath),   temp_driverpath );

	snprintf(temp_datafile,   sizeof(temp_driverpath)-1, "%s%s", where, 
	         driver.info_3->datafile);
	make_unistr( &(info->datafile),     temp_datafile );

	snprintf(temp_configfile, sizeof(temp_driverpath)-1, "%s%s", where, 
	         driver.info_3->configfile);
	make_unistr( &(info->configfile),   temp_configfile );	
}

/********************************************************************
 * construct_printer_driver_info_2
 * fill a printer_info_2 struct
 ********************************************************************/
static void construct_printer_driver_info_2(DRIVER_INFO_2 *info, int snum, 
                                            pstring servername, fstring architecture)
{
	NT_PRINTER_INFO_LEVEL printer;
	NT_PRINTER_DRIVER_INFO_LEVEL driver;
	
	get_a_printer(&printer, 2, lp_servicename(snum) );
	get_a_printer_driver(&driver, 3, printer.info_2->drivername, architecture);	

	fill_printer_driver_info_2(info, driver, servername, architecture);

	free_a_printer_driver(driver, 3);
	free_a_printer(printer, 2);
}

/********************************************************************
 * construct_printer_info_3
 * fill a printer_info_3 struct
 ********************************************************************/
static void fill_printer_driver_info_3(DRIVER_INFO_3 *info, 
                                       NT_PRINTER_DRIVER_INFO_LEVEL driver, 
				       pstring servername, fstring architecture)
{
	pstring where;
	pstring temp_driverpath;
	pstring temp_datafile;
	pstring temp_configfile;
	pstring temp_helpfile;
	fstring short_archi;
	
	get_short_archi(short_archi, architecture);
	
	snprintf(where,sizeof(where)-1,"\\\\%s\\print$\\%s\\", servername, short_archi);
	
	info->version=driver.info_3->cversion;

	make_unistr( &(info->name),         driver.info_3->name );	
	make_unistr( &(info->architecture), architecture );
	
	snprintf(temp_driverpath, sizeof(temp_driverpath)-1, "%s%s", where, driver.info_3->driverpath);		 
	make_unistr( &(info->driverpath), temp_driverpath );
	
	snprintf(temp_datafile,   sizeof(temp_datafile)-1,   "%s%s", where, driver.info_3->datafile); 
	make_unistr( &(info->datafile), temp_datafile );
	
	snprintf(temp_configfile, sizeof(temp_configfile)-1, "%s%s", where, driver.info_3->configfile);
	make_unistr( &(info->configfile), temp_configfile );	
	
	snprintf(temp_helpfile,   sizeof(temp_helpfile)-1,   "%s%s", where, driver.info_3->helpfile);
	make_unistr( &(info->helpfile), temp_helpfile );

	make_unistr( &(info->monitorname), driver.info_3->monitorname );	
	make_unistr( &(info->defaultdatatype), driver.info_3->defaultdatatype );

	make_unistr( &(info->dependentfiles), "" );
}

/********************************************************************
 * construct_printer_info_3
 * fill a printer_info_3 struct
 ********************************************************************/
static void construct_printer_driver_info_3(DRIVER_INFO_3 *info, int snum, 
                                            pstring servername, fstring architecture)
{	
	NT_PRINTER_INFO_LEVEL printer;
	NT_PRINTER_DRIVER_INFO_LEVEL driver;
	
	get_a_printer(&printer, 2, lp_servicename(snum) );	
	get_a_printer_driver(&driver, 3, printer.info_2->drivername, architecture);	

	fill_printer_driver_info_3(info, driver, servername, architecture);

	free_a_printer_driver(driver, 3);
	free_a_printer(printer, 2);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_getprinterdriver2(SPOOL_Q_GETPRINTERDRIVER2 *q_u, prs_struct *rdata)
{
	SPOOL_R_GETPRINTERDRIVER2 r_u;
	pstring servername;
	fstring architecture;
	int snum;
	DRIVER_INFO_1 *info1=NULL;
	DRIVER_INFO_2 *info2=NULL;
	DRIVER_INFO_3 *info3=NULL;

	pstrcpy(servername, global_myname);
	get_printer_snum(&(q_u->handle),&snum);

	r_u.offered=q_u->buf_size;
	r_u.level=q_u->level;
	r_u.status=0x0000;	
	
	unistr2_to_ascii(architecture, &(q_u->architecture), sizeof(architecture) );
	
	DEBUG(1,("spoolss_reply_getprinterdriver2:[%d]\n", q_u->level));
	
	switch (q_u->level)
	{
		case 1:
		{			
			info1=(DRIVER_INFO_1 *)malloc(sizeof(DRIVER_INFO_1));
			construct_printer_driver_info_1(info1, snum, servername, architecture);
			r_u.printer.info1=info1;			
			break;			
		}
		case 2:
		{
			info2=(DRIVER_INFO_2 *)malloc(sizeof(DRIVER_INFO_2));
			construct_printer_driver_info_2(info2, snum, servername, architecture);
			r_u.printer.info2=info2;			
			break;
		}
		case 3:
		{
			info3=(DRIVER_INFO_3 *)malloc(sizeof(DRIVER_INFO_3));
			construct_printer_driver_info_3(info3, snum, servername, architecture);
			r_u.printer.info3=info3;
			break;
		}
	}
	
	spoolss_io_r_getprinterdriver2("",&r_u,rdata,0);
	
	if (info1!=NULL) free(info1);
	if (info2!=NULL) free(info2);
	if (info3!=NULL) free(info3);
	
}

/********************************************************************
 * api_spoolss_getprinter
 * called from the spoolss dispatcher
 *
 ********************************************************************/
static void api_spoolss_getprinterdriver2(pipes_struct *p, prs_struct *data,
                                          prs_struct *rdata)
{
	SPOOL_Q_GETPRINTERDRIVER2 q_u;
	
	spoolss_io_q_getprinterdriver2("", &q_u, data, 0);
	
	spoolss_reply_getprinterdriver2(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_startpageprinter(SPOOL_Q_STARTPAGEPRINTER *q_u, prs_struct *rdata)
{
	SPOOL_R_STARTPAGEPRINTER r_u;
	int pnum = find_printer_index_by_hnd(&(q_u->handle));

	if (OPEN_HANDLE(pnum))
	{
		Printer[pnum].page_started=True;
		r_u.status=0x0;

		spoolss_io_r_startpageprinter("",&r_u,rdata,0);		
	}
	else
	{
		DEBUG(3,("Error in startpageprinter printer handle (pnum=%x)\n",pnum));
	}
}

/********************************************************************
 * api_spoolss_getprinter
 * called from the spoolss dispatcher
 *
 ********************************************************************/
static void api_spoolss_startpageprinter(pipes_struct *p, prs_struct *data,
                                          prs_struct *rdata)
{
	SPOOL_Q_STARTPAGEPRINTER q_u;
	
	spoolss_io_q_startpageprinter("", &q_u, data, 0);
	
	spoolss_reply_startpageprinter(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_endpageprinter(SPOOL_Q_ENDPAGEPRINTER *q_u, prs_struct *rdata)
{
	SPOOL_R_ENDPAGEPRINTER r_u;
	int pnum = find_printer_index_by_hnd(&(q_u->handle));

	if (OPEN_HANDLE(pnum))
	{
		Printer[pnum].page_started=False;
		r_u.status=0x0;

		spoolss_io_r_endpageprinter("",&r_u,rdata,0);		
	}
	else
	{
		DEBUG(3,("Error in endpageprinter printer handle (pnum=%x)\n",pnum));
	}
}

/********************************************************************
 * api_spoolss_getprinter
 * called from the spoolss dispatcher
 *
 ********************************************************************/
static void api_spoolss_endpageprinter(pipes_struct *p, prs_struct *data,
                                          prs_struct *rdata)
{
	SPOOL_Q_ENDPAGEPRINTER q_u;
	
	spoolss_io_q_endpageprinter("", &q_u, data, 0);
	
	spoolss_reply_endpageprinter(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_startdocprinter(SPOOL_Q_STARTDOCPRINTER *q_u, prs_struct *rdata)
{
	SPOOL_R_STARTDOCPRINTER r_u;
	int pnum = find_printer_index_by_hnd(&(q_u->handle));

	if (OPEN_HANDLE(pnum))
	{		
		r_u.jobid=Printer[pnum].current_jobid;
		r_u.status=0x0;

		spoolss_io_r_startdocprinter("",&r_u,rdata,0);		
	}
	else
	{
		DEBUG(3,("Error in startdocprinter printer handle (pnum=%x)\n",pnum));
	}
}

/********************************************************************
 * api_spoolss_getprinter
 * called from the spoolss dispatcher
 *
 ********************************************************************/
static void api_spoolss_startdocprinter(pipes_struct *p, prs_struct *data,
                                          prs_struct *rdata)
{
	SPOOL_Q_STARTDOCPRINTER q_u;
	pstring fname;
	pstring tempname;
	int fd = -1;
	int snum;
	int pnum;

	/* decode the stream and fill the struct */
	spoolss_io_q_startdocprinter("", &q_u, data, 0);
	
	pnum = find_printer_index_by_hnd(&(q_u.handle));

	if (OPEN_HANDLE(pnum))
	{
		/* get the share number of the printer */
		get_printer_snum(&(q_u.handle),&snum);

		/* Create a temporary file in the printer spool directory
		 * and open it
		 */

		slprintf(tempname,sizeof(tempname)-1, "%s/smb_print.XXXXXX",lp_pathname(snum));  
		pstrcpy(fname, (char *)mktemp(tempname));

		fd=open(fname, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR );
		DEBUG(4,("Temp spool file created: [%s]\n", fname));

		Printer[pnum].current_jobid=fd;
		pstrcpy(Printer[pnum].document_name,fname);
		
		unistr2_to_ascii(Printer[pnum].job_name, 
		                 &(q_u.doc_info_container.docinfo.doc_info_1.docname), 
		                 sizeof(Printer[pnum].job_name));
		
 		Printer[pnum].document_fd=fd;
		Printer[pnum].document_started=True;
	}	
	spoolss_reply_startdocprinter(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_enddocprinter(SPOOL_Q_ENDDOCPRINTER *q_u, prs_struct *rdata)
{
	SPOOL_R_ENDDOCPRINTER r_u;
	int pnum = find_printer_index_by_hnd(&(q_u->handle));

	if (OPEN_HANDLE(pnum))
	{
		r_u.status=0x0;

		spoolss_io_r_enddocprinter("",&r_u,rdata,0);		
	}
	else
	{
		DEBUG(3,("Error in enddocprinter printer handle (pnum=%x)\n",pnum));
	}
}

/********************************************************************
 * api_spoolss_getprinter
 * called from the spoolss dispatcher
 *
 ********************************************************************/
static void api_spoolss_enddocprinter(pipes_struct *p, prs_struct *data,
                                          prs_struct *rdata)
{
	SPOOL_Q_ENDDOCPRINTER q_u;
	int pnum;
	int snum;
	pstring filename;
	pstring filename1;
	pstring job_name;
	pstring syscmd;
	char *tstr;
	
	spoolss_io_q_enddocprinter("", &q_u, data, 0);
	
	*syscmd=0;
	
	pnum = find_printer_index_by_hnd(&(q_u.handle));
	
	if (OPEN_HANDLE(pnum))
	{
		Printer[pnum].document_started=False;
		close(Printer[pnum].document_fd);
		DEBUG(4,("Temp spool file closed, printing now ...\n"));

		pstrcpy(filename1, Printer[pnum].document_name);
		pstrcpy(job_name, Printer[pnum].job_name);
		
		get_printer_snum(&(q_u.handle),&snum);
		
		/* copy the command into the buffer for extensive meddling. */
		StrnCpy(syscmd, lp_printcommand(snum), sizeof(pstring) - 1);

		/* look for "%s" in the string. If there is no %s, we cannot print. */   
		if (!strstr(syscmd, "%s") && !strstr(syscmd, "%f"))
		{
			DEBUG(2,("WARNING! No placeholder for the filename in the print command for service %s!\n", SERVICE(snum)));
		}

		if (strstr(syscmd,"%s"))
		{
	 		pstrcpy(filename,filename1);
			string_sub(syscmd, "%s", filename);
		}

		string_sub(syscmd, "%f", filename1);

		/* Does the service have a printername? If not, make a fake and empty	 */
		/* printer name. That way a %p is treated sanely if no printer */
		/* name was specified to replace it. This eventuality is logged.	 */
		tstr = lp_printername(snum);
		if (tstr == NULL || tstr[0] == '\0')
		{
			DEBUG(3,( "No printer name - using %s.\n", SERVICE(snum)));
			tstr = SERVICE(snum);
		}

		string_sub(syscmd, "%p", tstr);

		/* If the lpr command support the 'Job' option replace here */
		string_sub(syscmd, "%j", job_name);

		if ( *syscmd != '\0')
	  	{
	  	  int ret = smbrun(syscmd, NULL, False);
	  	  DEBUG(3,("Running the command `%s' gave %d\n", syscmd, ret));
	  	}
		else
		  DEBUG(0,("Null print command?\n"));

		lpq_reset(snum);
	}
	
	spoolss_reply_enddocprinter(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_writeprinter(SPOOL_Q_WRITEPRINTER *q_u, prs_struct *rdata)
{
	SPOOL_R_WRITEPRINTER r_u;
	int pnum = find_printer_index_by_hnd(&(q_u->handle));

	if (OPEN_HANDLE(pnum))
	{
		r_u.buffer_written=Printer[pnum].document_lastwritten;
		r_u.status=0x0;

		spoolss_io_r_writeprinter("",&r_u,rdata,0);		
	}
	else
	{
		DEBUG(3,("Error in writeprinter printer handle (pnum=%x)\n",pnum));
	}
}

/********************************************************************
 * api_spoolss_getprinter
 * called from the spoolss dispatcher
 *
 ********************************************************************/
static void api_spoolss_writeprinter(pipes_struct *p, prs_struct *data,
                                          prs_struct *rdata)
{
	SPOOL_Q_WRITEPRINTER q_u;
	int pnum;
	int fd;
	int size;
	spoolss_io_q_writeprinter("", &q_u, data, 0);
	
	pnum = find_printer_index_by_hnd(&(q_u.handle));
	
	if (OPEN_HANDLE(pnum))
	{
		fd=Printer[pnum].document_fd;
		size=write(fd, q_u.buffer, q_u.buffer_size);
		if (q_u.buffer) free(q_u.buffer);
		Printer[pnum].document_lastwritten=size;
	}
	
	spoolss_reply_writeprinter(&q_u, rdata);
}

/********************************************************************
 * api_spoolss_getprinter
 * called from the spoolss dispatcher
 *
 ********************************************************************/
static void control_printer(PRINTER_HND handle, uint32 command, connection_struct *conn)
{
	int pnum;
	int snum;
	pnum = find_printer_index_by_hnd(&(handle));

	if ( get_printer_snum(&handle, &snum) )
	{		 
		switch (command)
		{
			case PRINTER_CONTROL_PAUSE:
				/* pause the printer here */
				status_printqueue(conn, snum, LPSTAT_STOPPED);
				break;

			case PRINTER_CONTROL_RESUME:
			case PRINTER_CONTROL_UNPAUSE:
				/* UN-pause the printer here */
				status_printqueue(conn, snum, LPSTAT_OK);
				break;
			case PRINTER_CONTROL_PURGE:
				/* Envoi des drag�es FUCA dans l'imprimante */
				break;
		}
	}
}

/********************************************************************
 * called by spoolss_api_setprinter
 * when updating a printer description
 ********************************************************************/
static void update_printer(PRINTER_HND handle, uint32 level,
                           SPOOL_PRINTER_INFO_LEVEL info, DEVICEMODE *devmode)
{
	int pnum;
	int snum;
	NT_PRINTER_INFO_LEVEL printer;
	NT_DEVICEMODE *nt_devmode;

	nt_devmode=NULL;
	
	DEBUG(8,("update_printer\n"));
	
	if (level!=2)
	{
		DEBUG(0,("Send a mail to samba-bugs@samba.org\n"));
		DEBUGADD(0,("with the following message: update_printer: level!=2\n"));
		return;
	}

	pnum = find_printer_index_by_hnd(&handle);
	
	if ( get_printer_snum(&handle, &snum) )
	{
		get_a_printer(&printer, level, lp_servicename(snum));

		DEBUGADD(8,("Converting info_2 struct\n"));
		convert_printer_info(info, &printer, level);
		
		if ((info.info_2)->devmode_ptr != 0)
		{
			/* we have a valid devmode
			   convert it and link it*/
			
			/* the nt_devmode memory is already alloced
			 * while doing the get_a_printer call
			 * but the devmode private part is not
			 * it's done by convert_devicemode
			 */
			DEBUGADD(8,("Converting the devicemode struct\n"));
			nt_devmode=printer.info_2->devmode;
			
			init_devicemode(nt_devmode);
					
			convert_devicemode(*devmode, nt_devmode);
			
			/* now clear the memory used in 
			 * the RPC parsing routine
			 */
			if (devmode->private != NULL)
				free(devmode->private);
			free(devmode);
		}
		else
		{
			if (printer.info_2->devmode != NULL)
			{
				free(printer.info_2->devmode);
			}
			printer.info_2->devmode=NULL;
		}
				
		add_a_printer(printer, level);
		free_a_printer(printer, level);
	}	
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_setprinter(SPOOL_Q_SETPRINTER *q_u, prs_struct *rdata)
{
	SPOOL_R_SETPRINTER r_u;

	/*
	  Let's the sun shine !!!
	  Always respond everything is alright
	*/
	
	r_u.status=0x0;

	spoolss_io_r_setprinter("",&r_u,rdata,0);
}

/****************************************************************************
****************************************************************************/
static void api_spoolss_setprinter(pipes_struct *p, prs_struct *data,
                                   prs_struct *rdata)
{
	SPOOL_Q_SETPRINTER q_u;
	int pnum;
	spoolss_io_q_setprinter("", &q_u, data, 0);
	
	pnum = find_printer_index_by_hnd(&(q_u.handle));
	
	if (OPEN_HANDLE(pnum))
	{
		/* check the level */	
		switch (q_u.level)
		{
			case 0:
				control_printer(q_u.handle, q_u.command, p->conn);
				break;
			case 2:
				update_printer(q_u.handle, q_u.level, q_u.info, q_u.devmode);
				break;
		}
	}
	spoolss_reply_setprinter(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_fcpn(SPOOL_Q_FCPN *q_u, prs_struct *rdata)
{
	SPOOL_R_FCPN r_u;
	
	r_u.status=0x0;

	spoolss_io_r_fcpn("",&r_u,rdata,0);		
}

/****************************************************************************
****************************************************************************/
static void api_spoolss_fcpn(pipes_struct *p, prs_struct *data,
                                   prs_struct *rdata)
{
	SPOOL_Q_FCPN q_u;
	
	spoolss_io_q_fcpn("", &q_u, data, 0);

	spoolss_reply_fcpn(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_addjob(SPOOL_Q_ADDJOB *q_u, prs_struct *rdata)
{
	SPOOL_R_ADDJOB r_u;
	
	r_u.status=0x0;

	spoolss_io_r_addjob("",&r_u,rdata,0);		
}

/****************************************************************************
****************************************************************************/
static void api_spoolss_addjob(pipes_struct *p, prs_struct *data,
                                   prs_struct *rdata)
{
	SPOOL_Q_ADDJOB q_u;
	
	spoolss_io_q_addjob("", &q_u, data, 0);

	spoolss_reply_addjob(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void fill_job_info_1(JOB_INFO_1 *job_info, print_queue_struct *queue,
                            int position, int snum)
{
	pstring temp_name;
	
	struct tm *t;
	time_t unixdate = time(NULL);
	
	t=gmtime(&unixdate);
	snprintf(temp_name, sizeof(temp_name), "\\\\%s", global_myname);

	job_info->jobid=queue->job;	
	make_unistr(&(job_info->printername), lp_servicename(snum));
	make_unistr(&(job_info->machinename), temp_name);
	make_unistr(&(job_info->username), queue->user);
	make_unistr(&(job_info->document), queue->file);
	make_unistr(&(job_info->datatype), "RAW");
	make_unistr(&(job_info->text_status), "");
	job_info->status=queue->status;
	job_info->priority=queue->priority;
	job_info->position=position;
	job_info->totalpages=0;
	job_info->pagesprinted=0;

	make_systemtime(&(job_info->submitted), t);
}

/****************************************************************************
****************************************************************************/
static void fill_job_info_2(JOB_INFO_2 *job_info, print_queue_struct *queue,
                            int position, int snum)
{
	pstring temp_name;

	struct tm *t;
	time_t unixdate = time(NULL);
	
	t=gmtime(&unixdate);
	snprintf(temp_name, sizeof(temp_name), "\\\\%s", global_myname);

	job_info->jobid=queue->job;
	make_unistr(&(job_info->printername), lp_servicename(snum));
	make_unistr(&(job_info->machinename), temp_name);
	make_unistr(&(job_info->username), queue->user);
	make_unistr(&(job_info->document), queue->file);
	make_unistr(&(job_info->notifyname), queue->user);
	make_unistr(&(job_info->datatype), "RAW");
	make_unistr(&(job_info->printprocessor), "winprint");
	make_unistr(&(job_info->parameters), "");

/* here the devicemode should be filled up */

	make_unistr(&(job_info->text_status), "");
	
/* and here the security descriptor */

	job_info->status=queue->status;
	job_info->priority=queue->priority;
	job_info->position=position;
	job_info->starttime=0;
	job_info->untiltime=0;
	job_info->totalpages=0;
	job_info->size=queue->size;
	make_systemtime(&(job_info->submitted), t);
	job_info->timeelapsed=0;
	job_info->pagesprinted=0;
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_enumjobs(SPOOL_Q_ENUMJOBS *q_u, prs_struct *rdata, connection_struct *conn)
{
	SPOOL_R_ENUMJOBS r_u;
	int snum;
	int count;
	int i;
	print_queue_struct *queue=NULL;
	print_status_struct status;
	JOB_INFO_1 *job_info_1=NULL;
	JOB_INFO_2 *job_info_2=NULL;

	DEBUG(4,("spoolss_reply_enumjobs\n"));
	
	bzero(&status,sizeof(status));

	r_u.offered=q_u->buf_size;


	if (get_printer_snum(&(q_u->handle), &snum))
	{
		count=get_printqueue(snum, conn, &queue, &status);
		r_u.numofjobs=count;
		
		r_u.level=q_u->level;
		
		DEBUG(4,("count:[%d], status:[%d], [%s]\n", count, status.status, status.message));
		
		switch (r_u.level)
		{
			case 1:
			{
				job_info_1=(JOB_INFO_1 *)malloc(count*sizeof(JOB_INFO_1));

				for (i=0; i<count; i++)
				{
					fill_job_info_1(&(job_info_1[i]), &(queue[i]), i, snum);
				}
				r_u.job.job_info_1=job_info_1;
				break;
			}
			case 2:
			{
				job_info_2=(JOB_INFO_2 *)malloc(count*sizeof(JOB_INFO_2));

				for (i=0; i<count; i++)
				{
					fill_job_info_2(&(job_info_2[i]), &(queue[i]), i, snum);
				}
				r_u.job.job_info_2=job_info_2;
				break;
			}
		}
	}

	r_u.status=0x0;

	spoolss_io_r_enumjobs("",&r_u,rdata,0);
	switch (r_u.level)
	{
		case 1:
		{
			free(job_info_1);
			break;
		}
		case 2:
		{
			free(job_info_2);
			break;
		}
	}
	if (queue) free(queue);

}

/****************************************************************************
****************************************************************************/
static void api_spoolss_enumjobs(pipes_struct *p, prs_struct *data,
                                   prs_struct *rdata)
{
	SPOOL_Q_ENUMJOBS q_u;
	
	spoolss_io_q_enumjobs("", &q_u, data, 0);

	spoolss_reply_enumjobs(&q_u, rdata, p->conn);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_schedulejob(SPOOL_Q_SCHEDULEJOB *q_u, prs_struct *rdata)
{
	SPOOL_R_SCHEDULEJOB r_u;
	
	r_u.status=0x0;

	spoolss_io_r_schedulejob("",&r_u,rdata,0);		
}

/****************************************************************************
****************************************************************************/
static void api_spoolss_schedulejob(pipes_struct *p, prs_struct *data,
                                   prs_struct *rdata)
{
	SPOOL_Q_SCHEDULEJOB q_u;
	
	spoolss_io_q_schedulejob("", &q_u, data, 0);

	spoolss_reply_schedulejob(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_setjob(SPOOL_Q_SETJOB *q_u, prs_struct *rdata, connection_struct *conn)
{
	SPOOL_R_SETJOB r_u;
	int snum;
	print_queue_struct *queue=NULL;
	print_status_struct status;
	int i=0;
	BOOL found=False;
	int count;
		
	bzero(&status,sizeof(status));

	if (get_printer_snum(&(q_u->handle), &snum))
	{
		count=get_printqueue(snum, conn, &queue, &status);		
		while ( (i<count) && found==False )
		{
			if ( q_u->jobid == queue[i].job )
			{
				found=True;
			}
			i++;
		}
		
		if (found==True)
		{
			switch (q_u->command)
			{
				case JOB_CONTROL_CANCEL:
				case JOB_CONTROL_DELETE:
				{
					del_printqueue(conn, snum, q_u->jobid);
					break;
				}
				case JOB_CONTROL_PAUSE:
				{
					status_printjob(conn, snum, q_u->jobid, LPQ_PAUSED);
					break;
				}
				case JOB_CONTROL_RESUME:
				{
					status_printjob(conn, snum, q_u->jobid, LPQ_QUEUED);
					break;
				}
			}
		}
	}
	r_u.status=0x0;
	spoolss_io_r_setjob("",&r_u,rdata,0);
	if (queue) free(queue);

}

/****************************************************************************
****************************************************************************/
static void api_spoolss_setjob(pipes_struct *p, prs_struct *data,
                                   prs_struct *rdata)
{
	SPOOL_Q_SETJOB q_u;
	
	spoolss_io_q_setjob("", &q_u, data, 0);

	spoolss_reply_setjob(&q_u, rdata, p->conn);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_enumprinterdrivers(SPOOL_Q_ENUMPRINTERDRIVERS *q_u, prs_struct *rdata, connection_struct *conn)
{
	SPOOL_R_ENUMPRINTERDRIVERS r_u;
	NT_PRINTER_DRIVER_INFO_LEVEL driver;
	int count;
	int i;
	fstring *list;
	DRIVER_INFO_1 *driver_info_1=NULL;
	DRIVER_INFO_2 *driver_info_2=NULL;
	DRIVER_INFO_3 *driver_info_3=NULL;
	fstring servername;
	fstring architecture;

	DEBUG(4,("spoolss_reply_enumdrivers\n"));
	fstrcpy(servername, global_myname);

	unistr2_to_ascii(architecture, &(q_u->environment), sizeof(architecture));
	count=get_ntdrivers(conn, &list, architecture);

	DEBUGADD(4,("we have: [%d] drivers on archi [%s]\n",count, architecture));
	for (i=0; i<count; i++)
	{
		DEBUGADD(5,("driver [%s]\n",list[i]));
	}
	
	r_u.offered=q_u->buf_size;
	r_u.numofdrivers=count;
	r_u.level=q_u->level;
	
	switch (r_u.level)
	{
		case 1:
		{
			driver_info_1=(DRIVER_INFO_1 *)malloc(count*sizeof(DRIVER_INFO_1));

			for (i=0; i<count; i++)
			{
				get_a_printer_driver(&driver, 3, list[i], architecture);
				fill_printer_driver_info_1(&(driver_info_1[i]), driver, servername, architecture );
				free_a_printer_driver(driver, 3);
			}
   			r_u.driver.driver_info_1=driver_info_1;
   			break;
   		}
   		case 2:
   		{
   			driver_info_2=(DRIVER_INFO_2 *)malloc(count*sizeof(DRIVER_INFO_2));

   			for (i=0; i<count; i++)
   			{
				get_a_printer_driver(&driver, 3, list[i], architecture);
   				fill_printer_driver_info_2(&(driver_info_2[i]), driver, servername, architecture );
				free_a_printer_driver(driver, 3);
   			}
   			r_u.driver.driver_info_2=driver_info_2;
   			break;
   		}
   		case 3:
   		{
   			driver_info_3=(DRIVER_INFO_3 *)malloc(count*sizeof(DRIVER_INFO_3));

   			for (i=0; i<count; i++)
   			{
				get_a_printer_driver(&driver, 3, list[i], architecture);
   				fill_printer_driver_info_3(&(driver_info_3[i]), driver, servername, architecture );
				free_a_printer_driver(driver, 3);
   			}
   			r_u.driver.driver_info_3=driver_info_3;
   			break;
   		}
	}

	r_u.status=0x0;

	spoolss_io_r_enumdrivers("",&r_u,rdata,0);

	switch (r_u.level)
	{
		case 1:
		{
			free(driver_info_1);
			break;
		}
		case 2:
		{
			free(driver_info_2);
			break;
		}
		case 3:
		{
			free(driver_info_3);
			break;
		}
	}
}

/****************************************************************************
****************************************************************************/

static void api_spoolss_enumprinterdrivers(pipes_struct *p, prs_struct *data,
                                   prs_struct *rdata)
{
	SPOOL_Q_ENUMPRINTERDRIVERS q_u;
	
	spoolss_io_q_enumprinterdrivers("", &q_u, data, 0);

	spoolss_reply_enumprinterdrivers(&q_u, rdata, p->conn);
}


/****************************************************************************
****************************************************************************/
static void fill_form_1(FORM_1 *form, nt_forms_struct *list, int position)
{
	form->flag=list->flag;
	make_unistr(&(form->name), list->name);
	form->width=list->width;
	form->length=list->length;
	form->left=list->left;
	form->top=list->top;
	form->right=list->right;
	form->bottom=list->bottom;	
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_enumforms(SPOOL_Q_ENUMFORMS *q_u, prs_struct *rdata)
{
	SPOOL_R_ENUMFORMS r_u;
	int count;
	int i;
	nt_forms_struct *list=NULL;
	FORM_1 *forms_1=NULL;

	DEBUG(4,("spoolss_reply_enumforms\n"));
	
	count=get_ntforms(&list);
	r_u.offered=q_u->buf_size;
	r_u.numofforms=count;
	r_u.level=q_u->level;
	r_u.status=0x0;

	DEBUGADD(5,("Offered buffer size [%d]\n", r_u.offered));
	DEBUGADD(5,("Number of forms [%d]\n",     r_u.numofforms));
	DEBUGADD(5,("Info level [%d]\n",          r_u.level));
		
	switch (r_u.level)
	{
		case 1:
		{
			forms_1=(FORM_1 *)malloc(count*sizeof(FORM_1));
			for (i=0; i<count; i++)
			{
				DEBUGADD(6,("Filling form number [%d]\n",i));
				fill_form_1(&(forms_1[i]), &(list[i]), i);
			}
   			r_u.forms_1=forms_1;
   			break;
   		}
	}
	spoolss_io_r_enumforms("",&r_u,rdata,0);
	switch (r_u.level)
	{
		case 1:
		{
			free(forms_1);
			break;
		}
	}
	free(list);
}

/****************************************************************************
****************************************************************************/
static void api_spoolss_enumforms(pipes_struct *p, prs_struct *data,
                                   prs_struct *rdata)
{
	SPOOL_Q_ENUMFORMS q_u;
	
	spoolss_io_q_enumforms("", &q_u, data, 0);

	spoolss_reply_enumforms(&q_u, rdata);
}

static void fill_port_2(PORT_INFO_2 *port, char *name)
{
	make_unistr(&(port->port_name), name);
	make_unistr(&(port->monitor_name), "Moniteur Local");
	make_unistr(&(port->description), "Local Port");
#define PORT_TYPE_WRITE 1
	port->port_type=PORT_TYPE_WRITE;
	port->reserved=0x0;	
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_enumports(SPOOL_Q_ENUMPORTS *q_u, prs_struct *rdata)
{
	SPOOL_R_ENUMPORTS r_u;
	int i=0;
	PORT_INFO_2 *ports_2=NULL;
	int n_services=lp_numservices();
	int snum;

	DEBUG(4,("spoolss_reply_enumports\n"));
	
	r_u.offered=q_u->buf_size;
	r_u.level=q_u->level;
	r_u.status=0x0;
		
	switch (r_u.level)
	{
		case 2:
		{
			ports_2=(PORT_INFO_2 *)malloc(n_services*sizeof(PORT_INFO_2));
			for (snum=0; snum<n_services; snum++)
			{
				if ( lp_browseable(snum) && lp_snum_ok(snum) && lp_print_ok(snum) )
				{
					DEBUGADD(6,("Filling port number [%d]\n",i));
					fill_port_2(&(ports_2[i]), lp_servicename(snum));
					i++;
				}
			}
   			r_u.port.port_info_2=ports_2;
   			break;
   		}
	}
	r_u.numofports=i;
	spoolss_io_r_enumports("",&r_u,rdata,0);
	switch (r_u.level)
	{
		case 2:
		{
			free(ports_2);
			break;
		}
	}
}

/****************************************************************************
****************************************************************************/
static void api_spoolss_enumports(pipes_struct *p, prs_struct *data,
                                   prs_struct *rdata)
{
	SPOOL_Q_ENUMPORTS q_u;
	
	spoolss_io_q_enumports("", &q_u, data, 0);

	spoolss_reply_enumports(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_addprinterex(SPOOL_Q_ADDPRINTEREX *q_u, prs_struct *rdata)
{
	SPOOL_R_ADDPRINTEREX r_u;
	BOOL printer_open = False;
	fstring ascii_name;
	fstring server_name;
	fstring share_name;
	UNISTR2 *portname;
	SPOOL_PRINTER_INFO_LEVEL_2 *info2;
	SPOOL_PRINTER_INFO_LEVEL *info;
	
	info=&(q_u->info);
	info2=info->info_2;
	portname=&(info2->portname);

	r_u.status=0x0; /* everything is always nice in this world */

	StrnCpy(server_name, global_myname, strlen(global_myname) );
	unistr2_to_ascii(share_name, portname, sizeof(share_name)-1);
	
	slprintf(ascii_name, sizeof(ascii_name)-1, "\\\\%s\\%s", 
	         server_name, share_name);
		
	printer_open = open_printer_hnd(&(r_u.handle));
	set_printer_hnd_printertype(&(r_u.handle), ascii_name);
	set_printer_hnd_printername(&(r_u.handle), ascii_name);

	spoolss_io_r_addprinterex("", &r_u, rdata, 0);
}

/****************************************************************************
****************************************************************************/
static void api_spoolss_addprinterex(pipes_struct *p, prs_struct *data, prs_struct *rdata)
{
	SPOOL_Q_ADDPRINTEREX q_u;
	NT_PRINTER_INFO_LEVEL printer;	
	
	/* read the stream and decode */
	spoolss_io_q_addprinterex("", &q_u, data, 0);

	/* NULLify info_2 here */
	/* don't put it in convert_printer_info as it's used also with non-NULL values */
	printer.info_2=NULL;

	/* convert from UNICODE to ASCII */
	convert_printer_info(q_u.info, &printer, q_u.level);

	/* write the ASCII on disk */
	add_a_printer(printer, q_u.level);

	spoolss_reply_addprinterex(&q_u, rdata);
	/* free mem used in q_u and r_u */
	
	/* free_add_printer(q_u, r_u); */
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_addprinterdriver(SPOOL_Q_ADDPRINTERDRIVER *q_u, prs_struct *rdata)
{
	SPOOL_R_ADDPRINTERDRIVER r_u;

	r_u.status=0x0; /* everything is always nice in this world */

	spoolss_io_r_addprinterdriver("", &r_u, rdata, 0);
}

/****************************************************************************
****************************************************************************/
static void api_spoolss_addprinterdriver(pipes_struct *p, prs_struct *data,
                                         prs_struct *rdata)
{
	SPOOL_Q_ADDPRINTERDRIVER q_u;
	NT_PRINTER_DRIVER_INFO_LEVEL driver;
	
	spoolss_io_q_addprinterdriver("", &q_u, data, 0);

	convert_printer_driver_info(q_u.info, &driver, q_u.level);

	add_a_printer_driver(driver, q_u.level);

	spoolss_reply_addprinterdriver(&q_u, rdata);
	/* free mem used in q_u and r_u */
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_getprinterdriverdirectory(SPOOL_Q_GETPRINTERDRIVERDIR *q_u, prs_struct *rdata)
{
	SPOOL_R_GETPRINTERDRIVERDIR r_u;
	pstring chaine;
	pstring long_archi;
	pstring archi;

	r_u.offered=q_u->buf_size;
	r_u.level=q_u->level;
	r_u.status=0x0;
	
	unistr2_to_ascii(long_archi, &(q_u->environment), sizeof(long_archi)-1);
	get_short_archi(archi, long_archi);
		
	slprintf(chaine,sizeof(chaine)-1,"\\\\%s\\print$\\%s", global_myname, archi);

	DEBUG(4,("printer driver directory: [%s]\n", chaine));
							    
	make_unistr(&(r_u.driver.driver_info_1.name), chaine);

	spoolss_io_r_getprinterdriverdir("", &r_u, rdata, 0);
}

/****************************************************************************
****************************************************************************/
static void api_spoolss_getprinterdriverdirectory(pipes_struct *p, prs_struct *data,
                                                  prs_struct *rdata)
{
	SPOOL_Q_GETPRINTERDRIVERDIR q_u;
	
	spoolss_io_q_getprinterdriverdir("", &q_u, data, 0);
	
	spoolss_reply_getprinterdriverdirectory(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_enumprinterdata(SPOOL_Q_ENUMPRINTERDATA *q_u, prs_struct *rdata)
{
	SPOOL_R_ENUMPRINTERDATA r_u;
	NT_PRINTER_INFO_LEVEL printer;
	
	uint32 type;
	fstring value;
	uint8 *data;
	
	uint32 param_index;
	uint32 biggest_valuesize;
	uint32 biggest_datasize;
	uint32 data_len;
	
	int pnum = find_printer_index_by_hnd(&(q_u->handle));
	int snum;
	
	DEBUG(5,("spoolss_reply_enumprinterdata\n"));

	if (OPEN_HANDLE(pnum))
	{
		get_printer_snum(&(q_u->handle), &snum);
		get_a_printer(&printer, 2, lp_servicename(snum));

		/* The NT machine wants to know the biggest size of value and data */	
		if ( (q_u->valuesize==0) && (q_u->datasize==0) )
		{
			DEBUGADD(6,("Activating NT mega-hack to find sizes\n"));
			
			r_u.valuesize=0;
			r_u.realvaluesize=0;
			r_u.type=0;
			r_u.datasize=0;
			r_u.realdatasize=0;
			
			param_index=0;
			biggest_valuesize=0;
			biggest_datasize=0;
			
			while (get_specific_param_by_index(printer, 2, param_index, value, &data, &type, &data_len))
			{
				if (strlen(value) > biggest_valuesize) biggest_valuesize=strlen(value);
				if (data_len  > biggest_datasize)  biggest_datasize=data_len;

				param_index++;
			}
			
			/* I wrote it, I didn't designed the protocol */
			SIVAL(&(r_u.value),0, 2*(biggest_valuesize+1) );
			r_u.data=(uint8 *)malloc(4*sizeof(uint8));
			SIVAL(r_u.data, 0, biggest_datasize );
		}
		else
		{
			/* 
			 * the value len is wrong in NT sp3
			 * that's the number of bytes not the number of unicode chars
			 */
			 
			r_u.valuesize=q_u->valuesize;
			r_u.datasize=q_u->datasize;

			if (get_specific_param_by_index(printer, 2, q_u->index, value, &data, &type, &data_len))
			{
				make_unistr(&(r_u.value), value);
				r_u.data=data;
				
				r_u.type=type;

				/* the length are in bytes including leading NULL */
				r_u.realvaluesize=2*(strlen(value)+1);
				r_u.realdatasize=data_len;
				
				r_u.status=0;
			}
			else
			{
				r_u.valuesize=0;
				r_u.realvaluesize=0;
				r_u.datasize=0;
				r_u.realdatasize=0;
				r_u.type=0;
				r_u.status=0x0103; /* ERROR_NO_MORE_ITEMS */
			}		
		}
		
		free_a_printer(printer, 2);
	}
	spoolss_io_r_enumprinterdata("", &r_u, rdata, 0);
	free(r_u.data);
}

/****************************************************************************
****************************************************************************/
static void api_spoolss_enumprinterdata(pipes_struct *p, prs_struct *data,
                                                  prs_struct *rdata)
{
	SPOOL_Q_ENUMPRINTERDATA q_u;
	
	spoolss_io_q_enumprinterdata("", &q_u, data, 0);
	
	spoolss_reply_enumprinterdata(&q_u, rdata);
}

/****************************************************************************
****************************************************************************/
static void spoolss_reply_setprinterdata(SPOOL_Q_SETPRINTERDATA *q_u, prs_struct *rdata)
{
	SPOOL_R_SETPRINTERDATA r_u;	
	NT_PRINTER_INFO_LEVEL printer;
	NT_PRINTER_PARAM *param = NULL;
		
	int pnum=0;
	int snum=0;
	
	DEBUG(5,("spoolss_reply_setprinterdata\n"));

	pnum = find_printer_index_by_hnd(&(q_u->handle));
	
	if (OPEN_HANDLE(pnum))
	{
		get_printer_snum(&(q_u->handle), &snum);		
		get_a_printer(&printer, 2, lp_servicename(snum));
		convert_specific_param(&param, q_u->value , q_u->type, q_u->data, q_u->real_len);

		unlink_specific_param_if_exist(printer.info_2, param);
		
		add_a_specific_param(printer.info_2, param);
		
		add_a_printer(printer, 2);
		
		free_a_printer(printer, 2);
	}	
	
	r_u.status = 0x0;
	spoolss_io_r_setprinterdata("", &r_u, rdata, 0);
}

/****************************************************************************
****************************************************************************/
static void api_spoolss_setprinterdata(pipes_struct *p, prs_struct *data,
                                       prs_struct *rdata)
{
	SPOOL_Q_SETPRINTERDATA q_u;
	
	spoolss_io_q_setprinterdata("", &q_u, data, 0);
	
	spoolss_reply_setprinterdata(&q_u, rdata);
	
	free(q_u.data);
}

/*******************************************************************
\pipe\spoolss commands
********************************************************************/
struct api_struct api_spoolss_cmds[] =
{
 {"SPOOLSS_OPENPRINTEREX",             SPOOLSS_OPENPRINTEREX,             api_spoolss_open_printer              },
 {"SPOOLSS_GETPRINTERDATA",            SPOOLSS_GETPRINTERDATA,            api_spoolss_getprinterdata            },
 {"SPOOLSS_CLOSEPRINTER",              SPOOLSS_CLOSEPRINTER,              api_spoolss_closeprinter              },
 {"SPOOLSS_RFFPCNEX",                  SPOOLSS_RFFPCNEX,                  api_spoolss_rffpcnex                  },
 {"SPOOLSS_RFNPCNEX",                  SPOOLSS_RFNPCNEX,                  api_spoolss_rfnpcnex                  },
 {"SPOOLSS_ENUMPRINTERS",              SPOOLSS_ENUMPRINTERS,              api_spoolss_enumprinters              },
 {"SPOOLSS_GETPRINTER",                SPOOLSS_GETPRINTER,                api_spoolss_getprinter                },
 {"SPOOLSS_GETPRINTERDRIVER2",         SPOOLSS_GETPRINTERDRIVER2,         api_spoolss_getprinterdriver2         }, 
 {"SPOOLSS_STARTPAGEPRINTER",          SPOOLSS_STARTPAGEPRINTER,          api_spoolss_startpageprinter          },
 {"SPOOLSS_ENDPAGEPRINTER",            SPOOLSS_ENDPAGEPRINTER,            api_spoolss_endpageprinter            }, 
 {"SPOOLSS_STARTDOCPRINTER",           SPOOLSS_STARTDOCPRINTER,           api_spoolss_startdocprinter           },
 {"SPOOLSS_ENDDOCPRINTER",             SPOOLSS_ENDDOCPRINTER,             api_spoolss_enddocprinter             },
 {"SPOOLSS_WRITEPRINTER",              SPOOLSS_WRITEPRINTER,              api_spoolss_writeprinter              },
 {"SPOOLSS_SETPRINTER",                SPOOLSS_SETPRINTER,                api_spoolss_setprinter                },
 {"SPOOLSS_FCPN",                      SPOOLSS_FCPN,                      api_spoolss_fcpn		        },
 {"SPOOLSS_ADDJOB",                    SPOOLSS_ADDJOB,                    api_spoolss_addjob                    },
 {"SPOOLSS_ENUMJOBS",                  SPOOLSS_ENUMJOBS,                  api_spoolss_enumjobs                  },
 {"SPOOLSS_SCHEDULEJOB",               SPOOLSS_SCHEDULEJOB,               api_spoolss_schedulejob               },
 {"SPOOLSS_SETJOB",                    SPOOLSS_SETJOB,                    api_spoolss_setjob                    },
 {"SPOOLSS_ENUMFORMS",                 SPOOLSS_ENUMFORMS,                 api_spoolss_enumforms                 },
 {"SPOOLSS_ENUMPORTS",                 SPOOLSS_ENUMPORTS,                 api_spoolss_enumports                 },
 {"SPOOLSS_ENUMPRINTERDRIVERS",        SPOOLSS_ENUMPRINTERDRIVERS,        api_spoolss_enumprinterdrivers        },
 {"SPOOLSS_ADDPRINTEREX",              SPOOLSS_ADDPRINTEREX,              api_spoolss_addprinterex              },
 {"SPOOLSS_ADDPRINTERDRIVER",          SPOOLSS_ADDPRINTERDRIVER,          api_spoolss_addprinterdriver          },
 {"SPOOLSS_GETPRINTERDRIVERDIRECTORY", SPOOLSS_GETPRINTERDRIVERDIRECTORY, api_spoolss_getprinterdriverdirectory },
 {"SPOOLSS_ENUMPRINTERDATA",           SPOOLSS_ENUMPRINTERDATA,           api_spoolss_enumprinterdata           },
 {"SPOOLSS_SETPRINTERDATA",            SPOOLSS_SETPRINTERDATA,            api_spoolss_setprinterdata            },
 { NULL,                               0,                                 NULL                                  }
};

/*******************************************************************
receives a spoolss pipe and responds.
********************************************************************/
BOOL api_spoolss_rpc(pipes_struct *p, prs_struct *data)
{
	return api_rpcTNP(p, "api_spoolss_rpc", api_spoolss_cmds, data);
}

