/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   SMB parameters and setup
   Copyright (C) Andrew Tridgell 1992-1998
   Copyright (C) Luke Kenneth Casson Leighton 1996-1998
   Copyright (C) Jean Francois Micouleau 1998-1999
   
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

#ifndef _RPC_SPOOLSS_H /* _RPC_SPOOLSS_H */
#define _RPC_SPOOLSS_H 

#define INTEGER 1
#define STRING 2

/* spoolss pipe: this are the calls which are not implemented ...
#define SPOOLSS_OPENPRINTER				0x01
#define SPOOLSS_DELETEPRINTER				0x06
#define SPOOLSS_GETPRINTERDRIVER			0x0b
#define SPOOLSS_DELETEPRINTERDRIVER			0x0d
#define SPOOLSS_ADDPRINTPROCESSOR			0x0e
#define SPOOLSS_GETPRINTPROCESSORDIRECTORY		0x10
#define SPOOLSS_ABORTPRINTER				0x15
#define SPOOLSS_READPRINTER				0x16
#define SPOOLSS_WAITFORPRINTERCHANGE			0x1c
#define SPOOLSS_DELETEFORM				0x1f
#define SPOOLSS_GETFORM					0x20
#define SPOOLSS_ADDPORT					0x25
#define SPOOLSS_CONFIGUREPORT				0x26
#define SPOOLSS_DELETEPORT				0x27
#define SPOOLSS_CREATEPRINTERIC				0x28
#define SPOOLSS_PLAYGDISCRIPTONPRINTERIC		0x29
#define SPOOLSS_DELETEPRINTERIC				0x2a
#define SPOOLSS_ADDPRINTERCONNECTION			0x2b
#define SPOOLSS_DELETEPRINTERCONNECTION			0x2c
#define SPOOLSS_PRINTERMESSAGEBOX			0x2d
#define SPOOLSS_ADDMONITOR				0x2e
#define SPOOLSS_DELETEMONITOR				0x2f
#define SPOOLSS_DELETEPRINTPROCESSOR			0x30
#define SPOOLSS_ADDPRINTPROVIDOR			0x31
#define SPOOLSS_DELETEPRINTPROVIDOR			0x32
#define SPOOLSS_RESETPRINTER				0x34
#define SPOOLSS_FINDFIRSTPRINTERCHANGENOTIFICATION	0x36
#define SPOOLSS_FINDNEXTPRINTERCHANGENOTIFICATION	0x37
#define SPOOLSS_ROUTERFINDFIRSTPRINTERNOTIFICATIONOLD	0x39
#define SPOOLSS_REPLYOPENPRINTER			0x3a
#define SPOOLSS_ROUTERREPLYPRINTER			0x3b
#define SPOOLSS_REPLYCLOSEPRINTER			0x3c
#define SPOOLSS_ADDPORTEX				0x3d
#define SPOOLSS_REMOTEFINDFIRSTPRINTERCHANGENOTIFICATION0x3e
#define SPOOLSS_SPOOLERINIT				0x3f
#define SPOOLSS_RESETPRINTEREX				0x40
#define SPOOLSS_ROUTERREFRESHPRINTERCHANGENOTIFICATION	0x42
*/

/* those are implemented */

#define SPOOLSS_ENUMPRINTERS				0x00
#define SPOOLSS_SETJOB					0x02
#define SPOOLSS_GETJOB					0x03
#define SPOOLSS_ENUMJOBS				0x04
#define SPOOLSS_ADDPRINTER				0x05
#define SPOOLSS_SETPRINTER				0x07
#define SPOOLSS_GETPRINTER				0x08
#define SPOOLSS_ADDPRINTERDRIVER			0x09
#define SPOOLSS_ENUMPRINTERDRIVERS			0x0a
#define SPOOLSS_GETPRINTERDRIVERDIRECTORY		0x0c
#define SPOOLSS_ENUMPRINTPROCESSORS			0x0f
#define SPOOLSS_STARTDOCPRINTER				0x11
#define SPOOLSS_STARTPAGEPRINTER			0x12
#define SPOOLSS_WRITEPRINTER				0x13
#define SPOOLSS_ENDPAGEPRINTER				0x14
#define SPOOLSS_ENDDOCPRINTER				0x17
#define SPOOLSS_ADDJOB					0x18
#define SPOOLSS_SCHEDULEJOB				0x19
#define SPOOLSS_GETPRINTERDATA				0x1a
#define SPOOLSS_SETPRINTERDATA				0x1b
#define SPOOLSS_CLOSEPRINTER				0x1d
#define SPOOLSS_ADDFORM					0x1e
#define SPOOLSS_SETFORM					0x21
#define SPOOLSS_ENUMFORMS				0x22
#define SPOOLSS_ENUMPORTS				0x23
#define SPOOLSS_ENUMMONITORS				0x24
#define SPOOLSS_ENUMPRINTPROCESSORDATATYPES		0x33
#define SPOOLSS_GETPRINTERDRIVER2			0x35
/* find close printer notification */
#define SPOOLSS_FCPN					0x38
/* remote find first printer change notifyEx */
#define SPOOLSS_RFFPCNEX				0x41
/* remote find next printer change notifyEx */
#define SPOOLSS_RFNPCNEX				0x43
#define SPOOLSS_OPENPRINTEREX				0x45
#define SPOOLSS_ADDPRINTEREX				0x46
#define SPOOLSS_ENUMPRINTERDATA				0x48


#define SERVER_ACCESS_ADMINISTER	0x00000001
#define SERVER_ACCESS_ENUMERATE		0x00000002

#define PRINTER_ACCESS_ADMINISTER	0x00000004
#define PRINTER_ACCESS_USE		0x00000008

#define PRINTER_CONTROL_UNPAUSE		0x00000000
#define PRINTER_CONTROL_PAUSE		0x00000001
#define PRINTER_CONTROL_RESUME		0x00000002
#define PRINTER_CONTROL_PURGE		0x00000003
#define PRINTER_CONTROL_SET_STATUS	0x00000004

#define PRINTER_STATUS_PAUSED		0x00000001
#define PRINTER_STATUS_ERROR		0x00000002
#define PRINTER_STATUS_PENDING_DELETION	0x00000004
#define PRINTER_STATUS_PAPER_JAM	0x00000008

#define PRINTER_STATUS_PAPER_OUT	0x00000010
#define PRINTER_STATUS_MANUAL_FEED	0x00000020
#define PRINTER_STATUS_PAPER_PROBLEM	0x00000040
#define PRINTER_STATUS_OFFLINE		0x00000080

#define PRINTER_STATUS_IO_ACTIVE	0x00000100
#define PRINTER_STATUS_BUSY		0x00000200
#define PRINTER_STATUS_PRINTING		0x00000400
#define PRINTER_STATUS_OUTPUT_BIN_FULL	0x00000800

#define PRINTER_STATUS_NOT_AVAILABLE	0x00001000
#define PRINTER_STATUS_WAITING		0x00002000
#define PRINTER_STATUS_PROCESSING	0x00004000
#define PRINTER_STATUS_INITIALIZING	0x00008000

#define PRINTER_STATUS_WARMING_UP	0x00010000
#define PRINTER_STATUS_TONER_LOW	0x00020000
#define PRINTER_STATUS_NO_TONER		0x00040000
#define PRINTER_STATUS_PAGE_PUNT	0x00080000

#define PRINTER_STATUS_USER_INTERVENTION	0x00100000
#define PRINTER_STATUS_OUT_OF_MEMORY	0x00200000
#define PRINTER_STATUS_DOOR_OPEN	0x00400000
#define PRINTER_STATUS_SERVER_UNKNOWN	0x00800000

#define PRINTER_STATUS_POWER_SAVE	0x01000000

#define JOB_ACCESS_ADMINISTER		0x00000010

#define STANDARD_RIGHTS_READ		0x00020000
#define STANDARD_RIGHTS_WRITE		STANDARD_RIGHTS_READ
#define STANDARD_RIGHTS_EXECUTE		STANDARD_RIGHTS_READ
#define STANDARD_RIGHTS_REQUIRED	0x000F0000

/* Access rights for print servers */
#define SERVER_ALL_ACCESS	STANDARD_RIGHTS_REQUIRED|SERVER_ACCESS_ADMINISTER|SERVER_ACCESS_ENUMERATE
#define SERVER_READ		STANDARD_RIGHTS_READ|SERVER_ACCESS_ENUMERATE
#define SERVER_WRITE		STANDARD_RIGHTS_WRITE|SERVER_ACCESS_ADMINISTER|SERVER_ACCESS_ENUMERATE
#define SERVER_EXECUTE		STANDARD_RIGHTS_EXECUTE|SERVER_ACCESS_ENUMERATE

/* Access rights for printers */
#define PRINTER_ALL_ACCESS	STANDARD_RIGHTS_REQUIRED|PRINTER_ACCESS_ADMINISTER|PRINTER_ACCESS_USE
#define PRINTER_READ          STANDARD_RIGHTS_READ|PRINTER_ACCESS_USE
#define PRINTER_WRITE         STANDARD_RIGHTS_WRITE|PRINTER_ACCESS_USE
#define PRINTER_EXECUTE       STANDARD_RIGHTS_EXECUTE|PRINTER_ACCESS_USE

/* Access rights for jobs */
#define JOB_ALL_ACCESS	STANDARD_RIGHTS_REQUIRED|JOB_ACCESS_ADMINISTER
#define JOB_READ	STANDARD_RIGHTS_READ|JOB_ACCESS_ADMINISTER
#define JOB_WRITE	STANDARD_RIGHTS_WRITE|JOB_ACCESS_ADMINISTER
#define JOB_EXECUTE	STANDARD_RIGHTS_EXECUTE|JOB_ACCESS_ADMINISTER

#define PRINTER_HND_SIZE 20

#define ONE_VALUE 01
#define TWO_VALUE 02
#define POINTER   03

#define PRINTER_NOTIFY_TYPE 0x00
#define JOB_NOTIFY_TYPE     0x01

#define MAX_PRINTER_NOTIFY 26

#define PRINTER_NOTIFY_SERVER_NAME		0x00
#define PRINTER_NOTIFY_PRINTER_NAME		0x01
#define PRINTER_NOTIFY_SHARE_NAME		0x02
#define PRINTER_NOTIFY_PORT_NAME		0x03
#define PRINTER_NOTIFY_DRIVER_NAME		0x04
#define PRINTER_NOTIFY_COMMENT			0x05
#define PRINTER_NOTIFY_LOCATION			0x06
#define PRINTER_NOTIFY_DEVMODE			0x07
#define PRINTER_NOTIFY_SEPFILE			0x08
#define PRINTER_NOTIFY_PRINT_PROCESSOR		0x09
#define PRINTER_NOTIFY_PARAMETERS		0x0A
#define PRINTER_NOTIFY_DATATYPE			0x0B
#define PRINTER_NOTIFY_SECURITY_DESCRIPTOR	0x0C
#define PRINTER_NOTIFY_ATTRIBUTES		0x0D
#define PRINTER_NOTIFY_PRIORITY			0x0E
#define PRINTER_NOTIFY_DEFAULT_PRIORITY		0x0F
#define PRINTER_NOTIFY_START_TIME		0x10
#define PRINTER_NOTIFY_UNTIL_TIME		0x11
#define PRINTER_NOTIFY_STATUS			0x12
#define PRINTER_NOTIFY_STATUS_STRING		0x13
#define PRINTER_NOTIFY_CJOBS			0x14
#define PRINTER_NOTIFY_AVERAGE_PPM		0x15
#define PRINTER_NOTIFY_TOTAL_PAGES		0x16
#define PRINTER_NOTIFY_PAGES_PRINTED		0x17
#define PRINTER_NOTIFY_TOTAL_BYTES		0x18
#define PRINTER_NOTIFY_BYTES_PRINTED		0x19

#define MAX_JOB_NOTIFY 24

#define JOB_NOTIFY_PRINTER_NAME			0x00
#define JOB_NOTIFY_MACHINE_NAME			0x01
#define JOB_NOTIFY_PORT_NAME			0x02
#define JOB_NOTIFY_USER_NAME			0x03
#define JOB_NOTIFY_NOTIFY_NAME			0x04
#define JOB_NOTIFY_DATATYPE			0x05
#define JOB_NOTIFY_PRINT_PROCESSOR		0x06
#define JOB_NOTIFY_PARAMETERS			0x07
#define JOB_NOTIFY_DRIVER_NAME			0x08
#define JOB_NOTIFY_DEVMODE			0x09
#define JOB_NOTIFY_STATUS			0x0A
#define JOB_NOTIFY_STATUS_STRING		0x0B
#define JOB_NOTIFY_SECURITY_DESCRIPTOR		0x0C
#define JOB_NOTIFY_DOCUMENT			0x0D
#define JOB_NOTIFY_PRIORITY			0x0E
#define JOB_NOTIFY_POSITION			0x0F
#define JOB_NOTIFY_SUBMITTED			0x10
#define JOB_NOTIFY_START_TIME			0x11
#define JOB_NOTIFY_UNTIL_TIME			0x12
#define JOB_NOTIFY_TIME				0x13
#define JOB_NOTIFY_TOTAL_PAGES			0x14
#define JOB_NOTIFY_PAGES_PRINTED		0x15
#define JOB_NOTIFY_TOTAL_BYTES			0x16
#define JOB_NOTIFY_BYTES_PRINTED		0x17

/*
 * The printer attributes.
 * I #defined all of them (grabbed form MSDN)
 * I'm only using:
 * ( SHARED | NETWORK | RAW_ONLY )
 * RAW_ONLY _MUST_ be present otherwise NT will send an EMF file
 */
 
#define PRINTER_ATTRIBUTE_QUEUED		0x00000001
#define PRINTER_ATTRIBUTE_DIRECT		0x00000002
#define PRINTER_ATTRIBUTE_DEFAULT		0x00000004
#define PRINTER_ATTRIBUTE_SHARED		0x00000008

#define PRINTER_ATTRIBUTE_NETWORK		0x00000010
#define PRINTER_ATTRIBUTE_HIDDEN		0x00000020
#define PRINTER_ATTRIBUTE_LOCAL			0x00000040
#define PRINTER_ATTRIBUTE_ENABLE_DEVQ		0x00000080

#define PRINTER_ATTRIBUTE_KEEPPRINTEDJOBS	0x00000100
#define PRINTER_ATTRIBUTE_DO_COMPLETE_FIRST	0x00000200
#define PRINTER_ATTRIBUTE_WORK_OFFLINE		0x00000400
#define PRINTER_ATTRIBUTE_ENABLE_BIDI		0x00000800

#define PRINTER_ATTRIBUTE_RAW_ONLY		0x00001000

#define NO_PRIORITY	 0
#define MAX_PRIORITY	99
#define MIN_PRIORITY	 1
#define DEF_PRIORITY	 1

#define PRINTER_ENUM_DEFAULT		0x00000001
#define PRINTER_ENUM_LOCAL		0x00000002
#define PRINTER_ENUM_CONNECTIONS	0x00000004
#define PRINTER_ENUM_FAVORITE		0x00000004
#define PRINTER_ENUM_NAME		0x00000008
#define PRINTER_ENUM_REMOTE		0x00000010
#define PRINTER_ENUM_SHARED		0x00000020
#define PRINTER_ENUM_NETWORK		0x00000040

#define PRINTER_ENUM_EXPAND		0x00004000
#define PRINTER_ENUM_CONTAINER		0x00008000

#define PRINTER_ENUM_ICONMASK		0x00ff0000
#define PRINTER_ENUM_ICON1		0x00010000
#define PRINTER_ENUM_ICON2		0x00020000
#define PRINTER_ENUM_ICON3		0x00040000
#define PRINTER_ENUM_ICON4		0x00080000
#define PRINTER_ENUM_ICON5		0x00100000
#define PRINTER_ENUM_ICON6		0x00200000
#define PRINTER_ENUM_ICON7		0x00400000
#define PRINTER_ENUM_ICON8		0x00800000

typedef struct
{
	char name[100];
	uint32 flag;
	uint32 width;
	uint32 length;
	uint32 left;
	uint32 top;
	uint32 right;
	uint32 bottom;
} nt_forms_struct;

typedef struct 
{
	char name[100];
	char architecture[100];
	uint32 version;
	char default_form[30];
	uint32 color_flag;
	char driver[100];
	char datafile[100];
	char configfile[100];
	char helpfile[100];
	char monitor[100];
	char monitor_name[100];
	char **dependant;
} nt_drivers_struct;

typedef struct devicemode
{
	UNISTR	devicename;
	uint16	specversion;
	uint16	driverversion;
	uint16	size;
	uint16	driverextra;
	uint32	fields;
	uint16	orientation;
	uint16	papersize;
	uint16	paperlength;
	uint16	paperwidth;
	uint16	scale;
	uint16	copies;
	uint16	defaultsource;
	uint16	printquality;
	uint16	color;
	uint16	duplex;
	uint16	yresolution;
	uint16	ttoption;
	uint16	collate;
	UNISTR	formname;
	uint16	logpixels;
	uint32	bitsperpel;
	uint32	pelswidth;
	uint32	pelsheight;
	uint32	displayflags;
	uint32	displayfrequency;
	uint32	icmmethod;
	uint32	icmintent;
	uint32	mediatype;
	uint32	dithertype;
	uint32	reserved1;
	uint32	reserved2;
	uint32	panningwidth;
	uint32	panningheight;
	uint8	*private;
} DEVICEMODE; 

typedef struct devicemode_container
{
	DEVICEMODE *dm;
	uint8 *buffer;
	uint32 size_of_buffer;
} DEVICEMODE_CONTAINER;

#define ORIENTATION      0x00000001L
#define PAPERSIZE        0x00000002L
#define PAPERLENGTH      0x00000004L
#define PAPERWIDTH       0x00000008L
#define SCALE            0x00000010L
#define COPIES           0x00000100L
#define DEFAULTSOURCE    0x00000200L
#define PRINTQUALITY     0x00000400L
#define COLOR            0x00000800L
#define DUPLEX           0x00001000L
#define YRESOLUTION      0x00002000L
#define TTOPTION         0x00004000L
#define COLLATE          0x00008000L
#define FORMNAME         0x00010000L
#define LOGPIXELS        0x00020000L
#define BITSPERPEL       0x00040000L
#define PELSWIDTH        0x00080000L
#define PELSHEIGHT       0x00100000L
#define DISPLAYFLAGS     0x00200000L
#define DISPLAYFREQUENCY 0x00400000L
#define PANNINGWIDTH     0x00800000L
#define PANNINGHEIGHT    0x01000000L

#define ORIENT_PORTRAIT   1
#define ORIENT_LANDSCAPE  2

#define PAPER_FIRST                PAPER_LETTER
#define PAPER_LETTER               1  /* Letter 8 1/2 x 11 in               */
#define PAPER_LETTERSMALL          2  /* Letter Small 8 1/2 x 11 in         */
#define PAPER_TABLOID              3  /* Tabloid 11 x 17 in                 */
#define PAPER_LEDGER               4  /* Ledger 17 x 11 in                  */
#define PAPER_LEGAL                5  /* Legal 8 1/2 x 14 in                */
#define PAPER_STATEMENT            6  /* Statement 5 1/2 x 8 1/2 in         */
#define PAPER_EXECUTIVE            7  /* Executive 7 1/4 x 10 1/2 in        */
#define PAPER_A3                   8  /* A3 297 x 420 mm                    */
#define PAPER_A4                   9  /* A4 210 x 297 mm                    */
#define PAPER_A4SMALL             10  /* A4 Small 210 x 297 mm              */
#define PAPER_A5                  11  /* A5 148 x 210 mm                    */
#define PAPER_B4                  12  /* B4 (JIS) 250 x 354                 */
#define PAPER_B5                  13  /* B5 (JIS) 182 x 257 mm              */
#define PAPER_FOLIO               14  /* Folio 8 1/2 x 13 in                */
#define PAPER_QUARTO              15  /* Quarto 215 x 275 mm                */
#define PAPER_10X14               16  /* 10x14 in                           */
#define PAPER_11X17               17  /* 11x17 in                           */
#define PAPER_NOTE                18  /* Note 8 1/2 x 11 in                 */
#define PAPER_ENV_9               19  /* Envelope #9 3 7/8 x 8 7/8          */
#define PAPER_ENV_10              20  /* Envelope #10 4 1/8 x 9 1/2         */
#define PAPER_ENV_11              21  /* Envelope #11 4 1/2 x 10 3/8        */
#define PAPER_ENV_12              22  /* Envelope #12 4 \276 x 11           */
#define PAPER_ENV_14              23  /* Envelope #14 5 x 11 1/2            */
#define PAPER_CSHEET              24  /* C size sheet                       */
#define PAPER_DSHEET              25  /* D size sheet                       */
#define PAPER_ESHEET              26  /* E size sheet                       */
#define PAPER_ENV_DL              27  /* Envelope DL 110 x 220mm            */
#define PAPER_ENV_C5              28  /* Envelope C5 162 x 229 mm           */
#define PAPER_ENV_C3              29  /* Envelope C3  324 x 458 mm          */
#define PAPER_ENV_C4              30  /* Envelope C4  229 x 324 mm          */
#define PAPER_ENV_C6              31  /* Envelope C6  114 x 162 mm          */
#define PAPER_ENV_C65             32  /* Envelope C65 114 x 229 mm          */
#define PAPER_ENV_B4              33  /* Envelope B4  250 x 353 mm          */
#define PAPER_ENV_B5              34  /* Envelope B5  176 x 250 mm          */
#define PAPER_ENV_B6              35  /* Envelope B6  176 x 125 mm          */
#define PAPER_ENV_ITALY           36  /* Envelope 110 x 230 mm              */
#define PAPER_ENV_MONARCH         37  /* Envelope Monarch 3.875 x 7.5 in    */
#define PAPER_ENV_PERSONAL        38  /* 6 3/4 Envelope 3 5/8 x 6 1/2 in    */
#define PAPER_FANFOLD_US          39  /* US Std Fanfold 14 7/8 x 11 in      */
#define PAPER_FANFOLD_STD_GERMAN  40  /* German Std Fanfold 8 1/2 x 12 in   */
#define PAPER_FANFOLD_LGL_GERMAN  41  /* German Legal Fanfold 8 1/2 x 13 in */

#define PAPER_LAST                PAPER_FANFOLD_LGL_GERMAN
#define PAPER_USER                256

#define BIN_FIRST         BIN_UPPER
#define BIN_UPPER         1
#define BIN_ONLYONE       1
#define BIN_LOWER         2
#define BIN_MIDDLE        3
#define BIN_MANUAL        4
#define BIN_ENVELOPE      5
#define BIN_ENVMANUAL     6
#define BIN_AUTO          7
#define BIN_TRACTOR       8
#define BIN_SMALLFMT      9
#define BIN_LARGEFMT      10
#define BIN_LARGECAPACITY 11
#define BIN_CASSETTE      14
#define BIN_FORMSOURCE    15
#define BIN_LAST          BIN_FORMSOURCE

#define BIN_USER          256     /* device specific bins start here */

#define RES_DRAFT         (-1)
#define RES_LOW           (-2)
#define RES_MEDIUM        (-3)
#define RES_HIGH          (-4)

#define COLOR_MONOCHROME  1
#define COLOR_COLOR       2

#define DUP_SIMPLEX    1
#define DUP_VERTICAL   2
#define DUP_HORIZONTAL 3

#define TT_BITMAP     1       /* print TT fonts as graphics */
#define TT_DOWNLOAD   2       /* download TT fonts as soft fonts */
#define TT_SUBDEV     3       /* substitute device fonts for TT fonts */

#define COLLATE_FALSE  0
#define COLLATE_TRUE   1

#define ERROR_INVALID_HANDLE		  6
#define ERROR_INVALID_PARAMETER		 87
#define ERROR_INSUFFICIENT_BUFFER	122

typedef struct s_header_type
{
	uint32 type;
	union
	{
		uint32 value;
		UNISTR string;
	} data;
} HEADER_TYPE;

typedef struct s_buffer
{
	uint32 ptr;
	uint32 size;
	uint32 count;
	uint8 *data;
	HEADER_TYPE *header;
} BUFFER;


/* PRINTER_HND */
typedef struct printer_policy_info
{
  uint8 data[PRINTER_HND_SIZE]; /* printer handle */
} PRINTER_HND;

/* SPOOL_Q_OPEN_PRINTER request to open a printer */
typedef struct spool_q_open_printer
{
	uint32  unknown0;
	UNISTR2 printername;
	uint32  unknown1;
	uint32  cbbuf;
	uint32  devmod;
	uint32  access_required;
	uint32  unknown2;	/* 0x0000 0001 */
	uint32  unknown3;	/* 0x0000 0001 */
	uint32  unknown4;	/* ??? */
	uint32  unknown5;	/* 0x0000 001c */
	uint32  unknown6;	/* ??? */
	uint32  unknown7;	/* ??? */
	uint32  unknown8;	/* 0x0000 0565 */
	uint32  unknown9;	/* 0x0000 0002 */
	uint32  unknown10;	/* 0x0000 0000 */
	uint32  unknown11;	/* ??? */
	UNISTR2 station;
	UNISTR2 username;
} SPOOL_Q_OPEN_PRINTER;

/* SPOOL_Q_OPEN_PRINTER reply to an open printer */ 
typedef struct spool_r_open_printer
{	
	PRINTER_HND handle; /* handle used along all transactions (20*uint8) */
	uint32 status;
} SPOOL_R_OPEN_PRINTER;

typedef struct spool_q_getprinterdata
{
	PRINTER_HND handle;
	UNISTR2     valuename;
	uint32      size;
} SPOOL_Q_GETPRINTERDATA;

typedef struct spool_r_getprinterdata
{
	uint32 type;
	uint32 size;
	uint8 *data;
	uint32 numeric_data;
	uint32 needed;
	uint32 status;
} SPOOL_R_GETPRINTERDATA;

typedef struct spool_q_closeprinter
{
	PRINTER_HND handle;
} SPOOL_Q_CLOSEPRINTER;

typedef struct spool_r_closeprinter
{
	PRINTER_HND handle;
	uint32 status;
} SPOOL_R_CLOSEPRINTER;

typedef struct spool_q_startpageprinter
{
	PRINTER_HND handle;
} SPOOL_Q_STARTPAGEPRINTER;

typedef struct spool_r_startpageprinter
{
	uint32 status;
} SPOOL_R_STARTPAGEPRINTER;

typedef struct spool_q_endpageprinter
{
	PRINTER_HND handle;
} SPOOL_Q_ENDPAGEPRINTER;

typedef struct spool_r_endpageprinter
{
	uint32 status;
} SPOOL_R_ENDPAGEPRINTER;

typedef struct spool_doc_info_1
{
	uint32 p_docname;
	uint32 p_outputfile;
	uint32 p_datatype;
	UNISTR2 docname;
	UNISTR2 outputfile;
	UNISTR2 datatype;
} DOC_INFO_1;

typedef struct spool_doc_info
{
	uint32 switch_value;
	DOC_INFO_1 doc_info_1;
} DOC_INFO;

typedef struct spool_doc_info_container
{
	uint32 level;
	DOC_INFO docinfo;
} DOC_INFO_CONTAINER;

typedef struct spool_q_startdocprinter
{
	PRINTER_HND handle;
	DOC_INFO_CONTAINER doc_info_container;
} SPOOL_Q_STARTDOCPRINTER;

typedef struct spool_r_startdocprinter
{
	uint32 jobid;
	uint32 status;
} SPOOL_R_STARTDOCPRINTER;

typedef struct spool_q_enddocprinter
{
	PRINTER_HND handle;
} SPOOL_Q_ENDDOCPRINTER;

typedef struct spool_r_enddocprinter
{
	uint32 status;
} SPOOL_R_ENDDOCPRINTER;

typedef struct spool_q_writeprinter
{
	PRINTER_HND handle;
	uint32 buffer_size;
	uint8 *buffer;
	uint32 buffer_size2;
} SPOOL_Q_WRITEPRINTER;

typedef struct spool_r_writeprinter
{
	uint32 buffer_written;
	uint32 status;
} SPOOL_R_WRITEPRINTER;

typedef struct spool_notify_option_type
{
	uint16 type;
	uint16 reserved0;
	uint32 reserved1;
	uint32 reserved2;
	uint32 count;
	uint16 fields[16];
} SPOOL_NOTIFY_OPTION_TYPE;

typedef struct spool_notify_option
{
	uint32 version;
	uint32 reserved;
	uint32 count;
	SPOOL_NOTIFY_OPTION_TYPE type[16]; /* totally arbitrary !!! */
} SPOOL_NOTIFY_OPTION;

typedef struct spool_notify_info_data
{
	uint16 type;
	uint16 field;
	uint32 reserved;
	uint32 id;
	union 
	{
		uint32 value[2];
		struct
		{
			uint32 length;
			uint16 string[1024];
		} data;
	} notify_data;
	uint32 size;
	BOOL   enc_type;
} SPOOL_NOTIFY_INFO_DATA;

typedef struct spool_notify_info
{
	uint32 version;
	uint32 flags;
	uint32 count;
	SPOOL_NOTIFY_INFO_DATA data[26*16];
	/* 26 differents data types */
	/* so size it for 16 printers at max */
	/* jfmxxxx: Have to make it dynamic !!!*/
} SPOOL_NOTIFY_INFO;

/* If the struct name looks obscure, yes it is ! */
/* RemoteFindFirstPrinterChangeNotificationEx query struct */
typedef struct spoolss_q_rffpcnex
{
	PRINTER_HND handle;
	uint32 flags;
	uint32 options;
	UNISTR2 localmachine;
	uint32	printerlocal;
        SPOOL_NOTIFY_OPTION option;
} SPOOL_Q_RFFPCNEX;

typedef struct spool_r_rffpcnex
{
	uint32 status;
} SPOOL_R_RFFPCNEX;

/* Remote Find Next Printer Change Notify Ex */
typedef struct spool_q_rfnpcnex
{
	PRINTER_HND handle;
	uint32 change;
	SPOOL_NOTIFY_OPTION option;
} SPOOL_Q_RFNPCNEX;

typedef struct spool_r_rfnpcnex
{
	uint32 count;
	SPOOL_NOTIFY_INFO info;
} SPOOL_R_RFNPCNEX;

/* Find Close Printer Notify */
typedef struct spool_q_fcpn
{
	PRINTER_HND handle;
} SPOOL_Q_FCPN;

typedef struct spool_r_fcpn
{
	uint32 status;
} SPOOL_R_FCPN;


typedef struct printer_info_0
{
	UNISTR printername;
	UNISTR servername;
	uint32 cjobs;
	uint32 attributes;
	uint32 unknown0;
	uint32 unknown1;
	uint32 unknown2;
	uint32 unknown3;
	uint32 unknown4;
	uint32 unknown5;
	uint32 unknown6;
	uint16 majorversion;
	uint16 buildversion;
	uint32 unknown7;
	uint32 unknown8;
	uint32 unknown9;
	uint32 unknown10;
	uint32 unknown11;
	uint32 unknown12;
	uint32 unknown13;
	uint32 unknown14;
	uint32 unknown15;
	uint32 unknown16;
	uint32 unknown17;
	uint32 unknown18;
	uint32 status;
	uint32 unknown20;
	uint32 unknown21;
	uint16 unknown22;
	uint32 unknown23;	
} PRINTER_INFO_0;

typedef struct printer_info_1
{
    uint32 flags;
    UNISTR description;
    UNISTR name;
    UNISTR comment;
} PRINTER_INFO_1;

typedef struct printer_info_2
{
    UNISTR     servername;
    UNISTR     printername;
    UNISTR     sharename;
    UNISTR     portname;
    UNISTR     drivername;
    UNISTR     comment;
    UNISTR     location;
    DEVICEMODE *devmode;
    UNISTR     sepfile;
    UNISTR     printprocessor;
    UNISTR     datatype;
    UNISTR     parameters;
    /*SECURITY_DESCRIPTOR securitydescriptor;*/
    uint32   attributes;
    uint32   priority;
    uint32   defaultpriority;
    uint32   starttime;
    uint32   untiltime;
    uint32   status;
    uint32   cjobs;
    uint32   averageppm;
} PRINTER_INFO_2;

typedef struct spool_q_enumprinters
{
	uint32 flags;
	UNISTR2 servername;
	uint32 level;
	BUFFER buffer;
/*	uint32 buf_size;*/
} SPOOL_Q_ENUMPRINTERS;

typedef struct spool_r_enumprinters
{
	uint32 offered;	 /* number of bytes offered */
	uint32 needed;	 /* bytes needed */
	uint32 returned; /* number of printers */
	uint32 status;
	uint32 level;
	UNISTR servername;
	union {
		PRINTER_INFO_1 **printers_1;
		PRINTER_INFO_2 **printers_2;
	} printer;
} SPOOL_R_ENUMPRINTERS;


typedef struct spool_q_getprinter
{
	PRINTER_HND handle;
	uint32 level;
	uint32 offered;
} SPOOL_Q_GETPRINTER;

typedef struct spool_r_getprinter
{
	PRINTER_HND handle;
	uint32 level;
	
	uint32 offered;
	uint32 needed;
	uint32 status;
	union {
		PRINTER_INFO_0 *info0;
		PRINTER_INFO_1 *info1;
		PRINTER_INFO_2 *info2;
	} printer;
} SPOOL_R_GETPRINTER;

struct s_notify_info_data_table
{
	uint16 type;
	uint16 field;
	char   *name;
	uint32 size;
	void   (*fn) (connection_struct *conn, int snum, SPOOL_NOTIFY_INFO_DATA *data, print_queue_struct *queue, NT_PRINTER_INFO_LEVEL *printer);
};

typedef struct spool_q_getprinterdriver2
{
	PRINTER_HND handle;
	UNISTR2 architecture;
	uint32 level;
	BUFFER buffer;
	uint32 buf_size;
	uint32 status;
} SPOOL_Q_GETPRINTERDRIVER2;

typedef struct driver_info_1
{
	UNISTR name;
} DRIVER_INFO_1;

typedef struct driver_info_2
{
	uint32 version;
	UNISTR name;
	UNISTR architecture;
	UNISTR driverpath;
	UNISTR datafile;
	UNISTR configfile;
} DRIVER_INFO_2;

typedef struct driver_info_3
{
	uint32 version;
	UNISTR name;
	UNISTR architecture;
	UNISTR driverpath;
	UNISTR datafile;
	UNISTR configfile;
	UNISTR helpfile;
	UNISTR **dependentfiles;
	UNISTR monitorname;
	UNISTR defaultdatatype;
} DRIVER_INFO_3;

typedef struct spool_r_getprinterdriver2
{
	uint32 needed;
	uint32 offered;
	uint32 returned;
	uint32 status;
	uint32 level;
	union {
		DRIVER_INFO_1 *info1;
		DRIVER_INFO_2 *info2;
		DRIVER_INFO_3 *info3;
	} printer;
} SPOOL_R_GETPRINTERDRIVER2;

typedef struct add_jobinfo_1
{
	UNISTR path;
	uint32 job_number;
} ADD_JOBINFO_1;


typedef struct spool_q_addjob
{
	PRINTER_HND handle;
	uint32 level;
	BUFFER buffer;
	uint32 buf_size;
} SPOOL_Q_ADDJOB;

typedef struct spool_r_addjob
{
	uint32 status;
} SPOOL_R_ADDJOB;

/*
 * I'm really wondering how many different time formats
 * I will have to cope with
 *
 * JFM, 09/13/98 In a mad mood ;-(
*/
typedef struct systemtime
{
    uint16 year;
    uint16 month;
    uint16 dayofweek;
    uint16 day;
    uint16 hour;
    uint16 minute;
    uint16 second;
    uint16 milliseconds;
} SYSTEMTIME;

typedef struct s_job_info_1
{
	uint32 jobid;
	UNISTR printername;
	UNISTR machinename;
	UNISTR username;
	UNISTR document;
	UNISTR datatype;
	UNISTR text_status;
	uint32 status;
	uint32 priority;
	uint32 position;
	uint32 totalpages;
	uint32 pagesprinted;
	SYSTEMTIME submitted;
} JOB_INFO_1;

typedef struct s_job_info_2
{
	uint32 jobid;
	UNISTR printername;
	UNISTR machinename;
	UNISTR username;
	UNISTR document;
	UNISTR notifyname;
	UNISTR datatype;
	UNISTR printprocessor;
	UNISTR parameters;
	UNISTR drivername;
	DEVICEMODE *devmode;
	UNISTR text_status;
/*	SEC_DESC sec_desc;*/
	uint32 status;
	uint32 priority;
	uint32 position;
	uint32 starttime;
	uint32 untiltime;
	uint32 totalpages;
	uint32 size;
	SYSTEMTIME submitted;
	uint32 timeelapsed;
	uint32 pagesprinted;
} JOB_INFO_2;

typedef struct spool_q_enumjobs
{
	PRINTER_HND handle;
	uint32 firstjob;
	uint32 numofjobs;
	uint32 level;
	BUFFER buffer;
	uint32 buf_size;
} SPOOL_Q_ENUMJOBS;

typedef struct spool_r_enumjobs
{
	uint32 level;
	union {
		JOB_INFO_1 *job_info_1;
		JOB_INFO_2 *job_info_2;
	} job;
	uint32 offered;
	uint32 numofjobs;
	uint32 status;
} SPOOL_R_ENUMJOBS;

typedef struct spool_q_schedulejob
{
	PRINTER_HND handle;
	uint32 jobid;
} SPOOL_Q_SCHEDULEJOB;

typedef struct spool_r_schedulejob
{
	uint32 status;
} SPOOL_R_SCHEDULEJOB;

typedef struct s_port_info_1
{
	UNISTR port_name;
} PORT_INFO_1;

typedef struct s_port_info_2
{
	UNISTR port_name;
	UNISTR monitor_name;
	UNISTR description;
	uint32 port_type;
	uint32 reserved;
} PORT_INFO_2;

typedef struct spool_q_enumports
{
	UNISTR2 name;
	uint32 level;
	BUFFER buffer;
	uint32 buf_size;
} SPOOL_Q_ENUMPORTS;

typedef struct spool_r_enumports
{
	uint32 level;
	union {
		PORT_INFO_1 *port_info_1;
		PORT_INFO_2 *port_info_2;
	} port;
	uint32 offered;
	uint32 numofports;
	uint32 status;
} SPOOL_R_ENUMPORTS;

#define JOB_CONTROL_PAUSE              1
#define JOB_CONTROL_RESUME             2
#define JOB_CONTROL_CANCEL             3
#define JOB_CONTROL_RESTART            4
#define JOB_CONTROL_DELETE             5

typedef struct spool_q_setjob
{
	PRINTER_HND handle;
	uint32 jobid;
	uint32 level;
	union {
		JOB_INFO_1 job_info_1;
		JOB_INFO_2 job_info_2;
	} job;
	uint32 command;
} SPOOL_Q_SETJOB;

typedef struct spool_r_setjob
{
	uint32 status;
} SPOOL_R_SETJOB;

typedef struct spool_q_enumprinterdrivers
{
	UNISTR2 name;
	UNISTR2 environment;
	uint32 level;
	BUFFER buffer;
	uint32 buf_size;
} SPOOL_Q_ENUMPRINTERDRIVERS;

typedef struct spool_r_enumprinterdrivers
{
	uint32 level;
	union {
		DRIVER_INFO_1 *driver_info_1;
		DRIVER_INFO_2 *driver_info_2;
		DRIVER_INFO_3 *driver_info_3;
	} driver;
	uint32 offered;
	uint32 numofdrivers;
	uint32 status;
} SPOOL_R_ENUMPRINTERDRIVERS;

typedef struct spool_form_1
{
	uint32 flag;
	UNISTR name;
	uint32 width;
	uint32 length;
	uint32 left;
	uint32 top;
	uint32 right;
	uint32 bottom;
} FORM_1;

typedef struct spool_q_enumforms
{
	PRINTER_HND handle;
	uint32 level;
	BUFFER buffer;
	uint32 buf_size;
} SPOOL_Q_ENUMFORMS;

typedef struct spool_r_enumforms
{
	uint32 level;
	FORM_1 *forms_1;
	uint32 offered;
	uint32 numofforms;
	uint32 status;
} SPOOL_R_ENUMFORMS;


typedef struct spool_printer_info_level_2
{
	uint32 servername_ptr;
	uint32 printername_ptr;
	uint32 sharename_ptr;
	uint32 portname_ptr;
	uint32 drivername_ptr;
	uint32 comment_ptr;
	uint32 location_ptr;
	uint32 devmode_ptr;
	uint32 sepfile_ptr;
	uint32 printprocessor_ptr;
	uint32 datatype_ptr;
	uint32 parameters_ptr;
	uint32 secdesc_ptr;
	uint32 attributes;
	uint32 priority;
	uint32 default_priority;
	uint32 starttime;
	uint32 untiltime;
	uint32 status;
	uint32 cjobs;
	uint32 averageppm;
	UNISTR2 servername;
	UNISTR2 printername;
	UNISTR2 sharename;
	UNISTR2 portname;
	UNISTR2 drivername;
	UNISTR2 comment;
	UNISTR2 location;
	UNISTR2 sepfile;
	UNISTR2 printprocessor;
	UNISTR2 datatype;
	UNISTR2 parameters;
	SEC_DESC_BUF *secdesc;
} SPOOL_PRINTER_INFO_LEVEL_2;

typedef struct spool_printer_info_level
{
	SPOOL_PRINTER_INFO_LEVEL_2 *info_2;
} SPOOL_PRINTER_INFO_LEVEL;

typedef struct spool_printer_driver_info_level_3
{
	uint32 cversion;
	uint32 name_ptr;
	uint32 environment_ptr;
	uint32 driverpath_ptr;
	uint32 datafile_ptr;
	uint32 configfile_ptr;
	uint32 helpfile_ptr;
	uint32 monitorname_ptr;
	uint32 defaultdatatype_ptr;
	uint32 dependentfilessize;
	uint32 dependentfiles_ptr;

	UNISTR2 name;
	UNISTR2 environment;
	UNISTR2 driverpath;
	UNISTR2 datafile;
	UNISTR2 configfile;
	UNISTR2 helpfile;
	UNISTR2 monitorname;
	UNISTR2 defaultdatatype;
	BUFFER5 dependentfiles;

} SPOOL_PRINTER_DRIVER_INFO_LEVEL_3;

typedef struct spool_printer_driver_info_level
{
	SPOOL_PRINTER_DRIVER_INFO_LEVEL_3 *info_3;
} SPOOL_PRINTER_DRIVER_INFO_LEVEL;


/* this struct is undocumented */
/* thanks to the ddk ... */
typedef struct spool_user_level_1
{
	uint32 size;
	uint32 client_name_ptr;
	uint32 user_name_ptr;
	uint32 build;
	uint32 major;
	uint32 minor;
	uint32 processor;
	UNISTR2 client_name;
	UNISTR2 user_name;
} SPOOL_USER_LEVEL_1;

typedef struct spool_user_level
{
	SPOOL_USER_LEVEL_1 *user_level_1;
} SPOOL_USER_LEVEL;

typedef struct spool_q_setprinter
{
	PRINTER_HND handle;
	uint32 level;
	SPOOL_PRINTER_INFO_LEVEL info;

	DEVICEMODE *devmode;
	struct
	{
		uint32 size_of_buffer;
		uint32 data;
	} security;

	uint32 command;
} SPOOL_Q_SETPRINTER;

typedef struct spool_r_setprinter
{
	uint32 status;
} SPOOL_R_SETPRINTER;

typedef struct spool_q_addprinter
{
	UNISTR2 server_name;
	uint32 level;
	SPOOL_PRINTER_INFO_LEVEL info;
	uint32 unk0;
	uint32 unk1;
	uint32 unk2;
	uint32 unk3;
	uint32 user_level;
	SPOOL_USER_LEVEL user;
} SPOOL_Q_ADDPRINTER;

typedef struct spool_r_addprinter
{
	uint32 status;
} SPOOL_R_ADDPRINTER;

typedef struct spool_q_addprinterex
{
	UNISTR2 server_name;
	uint32 level;
	SPOOL_PRINTER_INFO_LEVEL info;
	uint32 unk0;
	uint32 unk1;
	uint32 unk2;
	uint32 unk3;
	uint32 user_level;
	SPOOL_USER_LEVEL user;
} SPOOL_Q_ADDPRINTEREX;


typedef struct spool_r_addprinterex
{
	PRINTER_HND handle;
	uint32 status;
} SPOOL_R_ADDPRINTEREX;

typedef struct spool_q_addprinterdriver
{
	UNISTR2 server_name;
	uint32 level;
	SPOOL_PRINTER_DRIVER_INFO_LEVEL info;
} SPOOL_Q_ADDPRINTERDRIVER;

typedef struct spool_r_addprinterdriver
{
	uint32 status;
} SPOOL_R_ADDPRINTERDRIVER;

typedef struct spool_q_getprinterdriverdirectory
{
	UNISTR2 name;
	UNISTR2 environment;
	uint32 level;
	BUFFER buffer;
	uint32 buf_size;
} SPOOL_Q_GETPRINTERDRIVERDIR;

typedef struct driver_directory_1
{
	UNISTR name;
} DRIVER_DIRECTORY_1 ;

typedef struct spool_r_getprinterdriverdirectory
{
	uint32 level;
	union {
		DRIVER_DIRECTORY_1 driver_info_1;
	} driver;
	uint32 offered;
	uint32 status;
} SPOOL_R_GETPRINTERDRIVERDIR;

typedef struct spool_q_enumprintprocessors
{
	UNISTR2 name;
	UNISTR2 environment;
	uint32 level;
	BUFFER buffer;
	uint32 buf_size;
} SPOOL_Q_ENUMPRINTPROCESSORS;

typedef struct printprocessor_1
{
	UNISTR name;
} PRINTPROCESSOR_1;

typedef struct spool_r_enumprintprocessors
{
	uint32 level;
	PRINTPROCESSOR_1 *info_1;
	uint32 offered;
	uint32 numofprintprocessors;
	uint32 status;
} SPOOL_R_ENUMPRINTPROCESSORS;

typedef struct spool_q_enumprintprocessordatatypes
{
	UNISTR2 name;
	UNISTR2 printprocessor;
	uint32 level;
	BUFFER buffer;
	uint32 buf_size;
} SPOOL_Q_ENUMPRINTPROCESSORDATATYPES;

typedef struct ppdatatype_1
{
	UNISTR name;
} PPDATATYPE_1;

typedef struct spool_r_enumprintprocessordatatypes
{
	uint32 level;
	PPDATATYPE_1 *info_1;
	uint32 offered;
	uint32 numofppdatatypes;
	uint32 status;
} SPOOL_R_ENUMPRINTPROCESSORDATATYPES;

typedef struct spool_q_enumprintmonitors
{
	UNISTR2 name;
	uint32 level;
	BUFFER buffer;
	uint32 buf_size;
} SPOOL_Q_ENUMPRINTMONITORS;

typedef struct printmonitor_1
{
	UNISTR name;
} PRINTMONITOR_1;

typedef struct spool_r_enumprintmonitors
{
	uint32 level;
	PRINTMONITOR_1 *info_1;
	uint32 offered;
	uint32 numofprintmonitors;
	uint32 status;
} SPOOL_R_ENUMPRINTMONITORS;

typedef struct spool_q_enumprinterdata
{
	PRINTER_HND handle;
	uint32 index;
	uint32 valuesize;
	uint32 datasize;
} SPOOL_Q_ENUMPRINTERDATA;

typedef struct spool_r_enumprinterdata
{
	uint32 valuesize;
	UNISTR value;
	uint32 realvaluesize;
	uint32 type;
	uint32 datasize;
	uint8  *data;
	uint32 realdatasize;
	uint32 status;
} SPOOL_R_ENUMPRINTERDATA;

typedef struct spool_q_setprinterdata
{
	PRINTER_HND handle;
	UNISTR2 value;
	uint32 type;
	uint32 max_len;
	uint8 *data;
	uint32 real_len;
	uint32 numeric_data;
} SPOOL_Q_SETPRINTERDATA;

typedef struct spool_r_setprinterdata
{
	uint32 status;
} SPOOL_R_SETPRINTERDATA;

typedef struct _form
{
       uint32 flags;
       uint32 name_ptr;
       uint32 size_x;
       uint32 size_y;
       uint32 left;
       uint32 top;
       uint32 right;
       uint32 bottom;
       UNISTR2 name;
} FORM;

typedef struct spool_q_addform
{
	PRINTER_HND handle;
	uint32 level;
	uint32 level2;
	FORM form;
} SPOOL_Q_ADDFORM;

typedef struct spool_r_addform
{
	uint32 status;
} SPOOL_R_ADDFORM;

typedef struct spool_q_setform
{
	PRINTER_HND handle;
	UNISTR2 name;
	uint32 level;
	uint32 level2;
	FORM form;
} SPOOL_Q_SETFORM;

typedef struct spool_r_setform
{
	uint32 status;
} SPOOL_R_SETFORM;

typedef struct spool_q_getjob
{
	PRINTER_HND handle;
	uint32 jobid;
	uint32 level;
	BUFFER buffer;
	uint32 buf_size;
} SPOOL_Q_GETJOB;

typedef struct spool_r_getjob
{
	uint32 level;
	union {
		JOB_INFO_1 *job_info_1;
		JOB_INFO_2 *job_info_2;
	} job;
	uint32 offered;
	uint32 status;
} SPOOL_R_GETJOB;

#define PRINTER_DRIVER_VERSION 2
#define PRINTER_DRIVER_ARCHITECTURE "Windows NT x86"


#endif /* _RPC_SPOOLSS_H */

