/* 
   Unix SMB/CIFS implementation.
   time handling functions

   Copyright (C) Andrew Tridgell 		1992-2004
   Copyright (C) Stefan (metze) Metzmacher	2002   

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

int extra_time_offset = 0;

#ifndef TIME_T_MIN
#define TIME_T_MIN 0
#endif
#ifndef TIME_T_MAX
#define TIME_T_MAX (~(time_t)0)
#endif

/*******************************************************************
 External access to time_t_min and time_t_max.
********************************************************************/
time_t get_time_t_max(void)
{
	return TIME_T_MAX;
}

/*******************************************************************
a gettimeofday wrapper
********************************************************************/
void GetTimeOfDay(struct timeval *tval)
{
#ifdef HAVE_GETTIMEOFDAY_TZ
	gettimeofday(tval,NULL);
#else
	gettimeofday(tval);
#endif
}

/*******************************************************************
yield the difference between *A and *B, in seconds, ignoring leap seconds
********************************************************************/
static int tm_diff(struct tm *a, struct tm *b)
{
	int ay = a->tm_year + (1900 - 1);
	int by = b->tm_year + (1900 - 1);
	int intervening_leap_days =
		(ay/4 - by/4) - (ay/100 - by/100) + (ay/400 - by/400);
	int years = ay - by;
	int days = 365*years + intervening_leap_days + (a->tm_yday - b->tm_yday);
	int hours = 24*days + (a->tm_hour - b->tm_hour);
	int minutes = 60*hours + (a->tm_min - b->tm_min);
	int seconds = 60*minutes + (a->tm_sec - b->tm_sec);

	return seconds;
}

/*******************************************************************
  return the UTC offset in seconds west of UTC, or 0 if it cannot be determined
  ******************************************************************/
static int TimeZone(time_t t)
{
  struct tm *tm = gmtime(&t);
  struct tm tm_utc;
  if (!tm)
    return 0;
  tm_utc = *tm;
  tm = localtime(&t);
  if (!tm)
    return 0;
  return tm_diff(&tm_utc,tm);

}

/*******************************************************************
  return the UTC offset in seconds west of UTC, or 0 if it cannot be determined
  ******************************************************************/
int get_time_zone(time_t t)
{
	struct tm *tm = gmtime(&t);
	struct tm tm_utc;
	if (!tm)
		return 0;
	tm_utc = *tm;
	tm = localtime(&t);
	if (!tm)
		return 0;
	return tm_diff(&tm_utc,tm);
}

/*******************************************************************
return the same value as TimeZone, but it should be more efficient.

We keep a table of DST offsets to prevent calling localtime() on each 
call of this function. This saves a LOT of time on many unixes.

Updated by Paul Eggert <eggert@twinsun.com>
********************************************************************/
static int TimeZoneFaster(time_t t)
{
  static struct dst_table {time_t start,end; int zone;} *tdt, *dst_table = NULL;
  static int table_size = 0;
  int i;
  int zone = 0;

  if (t == 0) t = time(NULL);

  /* Tunis has a 8 day DST region, we need to be careful ... */
#define MAX_DST_WIDTH (365*24*60*60)
#define MAX_DST_SKIP (7*24*60*60)

  for (i=0;i<table_size;i++)
    if (t >= dst_table[i].start && t <= dst_table[i].end) break;

  if (i<table_size) {
    zone = dst_table[i].zone;
  } else {
    time_t low,high;

    zone = TimeZone(t);
    tdt = (struct dst_table *)Realloc(dst_table,
					      sizeof(dst_table[0])*(i+1));
    if (!tdt) {
      DEBUG(0,("TimeZoneFaster: out of memory!\n"));
      SAFE_FREE(dst_table);
      table_size = 0;
    } else {
      dst_table = tdt;
      table_size++;

      dst_table[i].zone = zone; 
      dst_table[i].start = dst_table[i].end = t;
    
      /* no entry will cover more than 6 months */
      low = t - MAX_DST_WIDTH/2;
      if (t < low)
	low = TIME_T_MIN;
      
      high = t + MAX_DST_WIDTH/2;
      if (high < t)
	high = TIME_T_MAX;
      
      /* widen the new entry using two bisection searches */
      while (low+60*60 < dst_table[i].start) {
	if (dst_table[i].start - low > MAX_DST_SKIP*2)
	  t = dst_table[i].start - MAX_DST_SKIP;
	else
	  t = low + (dst_table[i].start-low)/2;
	if (TimeZone(t) == zone)
	  dst_table[i].start = t;
	else
	  low = t;
      }

      while (high-60*60 > dst_table[i].end) {
	if (high - dst_table[i].end > MAX_DST_SKIP*2)
	  t = dst_table[i].end + MAX_DST_SKIP;
	else
	  t = high - (high-dst_table[i].end)/2;
	if (TimeZone(t) == zone)
	  dst_table[i].end = t;
	else
	  high = t;
      }
#if 0
      DEBUG(1,("Added DST entry from %s ",
	       asctime(localtime(&dst_table[i].start))));
      DEBUG(1,("to %s (%d)\n",asctime(localtime(&dst_table[i].end)),
	       dst_table[i].zone));
#endif
    }
  }
  return zone;
}

/****************************************************************************
  return the UTC offset in seconds west of UTC, adjusted for extra time offset
  **************************************************************************/
int TimeDiff(time_t t)
{
  return TimeZoneFaster(t) + 60*extra_time_offset;
}

/****************************************************************************
try to optimise the localtime call, it can be quite expensive on some machines
****************************************************************************/
struct tm *LocalTime(time_t *t)
{
  time_t t2 = *t;

  t2 -= TimeDiff(t2);

  return(gmtime(&t2));
}

#define TIME_FIXUP_CONSTANT 11644473600LL

/****************************************************************************
interpret an 8 byte "filetime" structure to a time_t
It's originally in "100ns units since jan 1st 1601"
****************************************************************************/
time_t nt_time_to_unix(NTTIME nt)
{
	if (nt == 0) {
		return 0;
	}
	if (nt == -1LL) {
		return (time_t)-1;
	}
	nt += 1000*1000*10/2;
	nt /= 1000*1000*10;
	nt -= TIME_FIXUP_CONSTANT;

	if (TIME_T_MIN >= nt || nt >= TIME_T_MAX) {
		return 0;
	}

	return (time_t)nt;
}

/****************************************************************************
initialise an NTTIME to -1, which means "unknown" or "don't expire"
****************************************************************************/

void init_nt_time(NTTIME *nt)
{
	uint8_t init_time[] = { 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	memcpy( nt, init_time, sizeof(*nt) );
}


/****************************************************************************
put a 8 byte filetime from a time_t
This takes GMT as input
****************************************************************************/
void unix_to_nt_time(NTTIME *nt, time_t t)
{
	uint64_t t2; 

	if (t == (time_t)-1) {
		*nt = (NTTIME)-1LL;
		return;
	}		
	if (t == 0) {
		*nt = 0;
		return;
	}		

	t2 = t;
	t2 += TIME_FIXUP_CONSTANT;
	t2 *= 1000*1000*10;

	*nt = t2;
}


/****************************************************************************
check if it's a null mtime
****************************************************************************/
BOOL null_mtime(time_t mtime)
{
	return mtime == 0 || 
		mtime == (time_t)0xFFFFFFFF || 
		mtime == (time_t)-1;
}

/*******************************************************************
  create a 16 bit dos packed date
********************************************************************/
static uint16_t make_dos_date1(struct tm *t)
{
	uint16_t ret=0;
	ret = (((uint_t)(t->tm_mon+1)) >> 3) | ((t->tm_year-80) << 1);
	ret = ((ret&0xFF)<<8) | (t->tm_mday | (((t->tm_mon+1) & 0x7) << 5));
	return ret;
}

/*******************************************************************
  create a 16 bit dos packed time
********************************************************************/
static uint16_t make_dos_time1(struct tm *t)
{
	uint16_t ret=0;
	ret = ((((uint_t)t->tm_min >> 3)&0x7) | (((uint_t)t->tm_hour) << 3));
	ret = ((ret&0xFF)<<8) | ((t->tm_sec/2) | ((t->tm_min & 0x7) << 5));
	return ret;
}

/*******************************************************************
  create a 32 bit dos packed date/time from some parameters
  This takes a GMT time and returns a packed localtime structure
********************************************************************/
static uint32_t make_dos_date(time_t unixdate, int zone_offset)
{
	struct tm *t;
	uint32_t ret=0;

	if (unixdate == 0) {
		return 0;
	}

	unixdate -= zone_offset;

	t = gmtime(&unixdate);
	if (!t) {
		return 0xFFFFFFFF;
	}

	ret = make_dos_date1(t);
	ret = ((ret&0xFFFF)<<16) | make_dos_time1(t);

	return ret;
}

/*******************************************************************
put a dos date into a buffer (time/date format)
This takes GMT time and puts local time in the buffer
********************************************************************/
void push_dos_date(char *buf, int offset, time_t unixdate, int zone_offset)
{
	uint32_t x = make_dos_date(unixdate, zone_offset);
	SIVAL(buf,offset,x);
}

/*******************************************************************
put a dos date into a buffer (date/time format)
This takes GMT time and puts local time in the buffer
********************************************************************/
void push_dos_date2(char *buf,int offset,time_t unixdate, int zone_offset)
{
	uint32_t x;
	x = make_dos_date(unixdate, zone_offset);
	x = ((x&0xFFFF)<<16) | ((x&0xFFFF0000)>>16);
	SIVAL(buf,offset,x);
}

/*******************************************************************
put a dos 32 bit "unix like" date into a buffer. This routine takes
GMT and converts it to LOCAL time before putting it (most SMBs assume
localtime for this sort of date)
********************************************************************/
void push_dos_date3(char *buf,int offset,time_t unixdate, int zone_offset)
{
	if (!null_mtime(unixdate)) {
		unixdate -= zone_offset;
	}
	SIVAL(buf,offset,unixdate);
}

/*******************************************************************
  create a 16 bit dos packed date
********************************************************************/
static uint16 make_dos_date1_x(struct tm *t)
{
  uint16 ret=0;
  ret = (((unsigned)(t->tm_mon+1)) >> 3) | ((t->tm_year-80) << 1);
  ret = ((ret&0xFF)<<8) | (t->tm_mday | (((t->tm_mon+1) & 0x7) << 5));
  return(ret);
}

/*******************************************************************
  create a 32 bit dos packed date/time from some parameters
  This takes a GMT time and returns a packed localtime structure
********************************************************************/
static uint32 make_dos_date_x(time_t unixdate)
{
  struct tm *t;
  uint32 ret=0;

  t = LocalTime(&unixdate);
  if (!t)
    return 0xFFFFFFFF;

  ret = make_dos_date1_x(t);
  ret = ((ret&0xFFFF)<<16) | make_dos_time1(t);

  return(ret);
}

/*******************************************************************
put a dos date into a buffer (time/date format)
This takes GMT time and puts local time in the buffer
********************************************************************/
void put_dos_date(char *buf,int offset,time_t unixdate)
{
  uint32 x = make_dos_date_x(unixdate);
  SIVAL(buf,offset,x);
}

/*******************************************************************
put a dos date into a buffer (date/time format)
This takes GMT time and puts local time in the buffer
********************************************************************/
void put_dos_date2(char *buf,int offset,time_t unixdate)
{
  uint32 x = make_dos_date_x(unixdate);
  x = ((x&0xFFFF)<<16) | ((x&0xFFFF0000)>>16);
  SIVAL(buf,offset,x);
}

/*******************************************************************
put a dos 32 bit "unix like" date into a buffer. This routine takes
GMT and converts it to LOCAL time before putting it (most SMBs assume
localtime for this sort of date)
********************************************************************/
void put_dos_date3(char *buf,int offset,time_t unixdate)
{
  if (!null_mtime(unixdate))
    unixdate -= TimeDiff(unixdate);
  SIVAL(buf,offset,unixdate);
}

/*******************************************************************
  interpret a 32 bit dos packed date/time to some parameters
********************************************************************/
static void interpret_dos_date(uint32_t date,int *year,int *month,int *day,int *hour,int *minute,int *second)
{
	uint32_t p0,p1,p2,p3;

	p0=date&0xFF; p1=((date&0xFF00)>>8)&0xFF; 
	p2=((date&0xFF0000)>>16)&0xFF; p3=((date&0xFF000000)>>24)&0xFF;

	*second = 2*(p0 & 0x1F);
	*minute = ((p0>>5)&0xFF) + ((p1&0x7)<<3);
	*hour = (p1>>3)&0xFF;
	*day = (p2&0x1F);
	*month = ((p2>>5)&0xFF) + ((p3&0x1)<<3) - 1;
	*year = ((p3>>1)&0xFF) + 80;
}

/*******************************************************************
  create a unix date (int GMT) from a dos date (which is actually in
  localtime)
********************************************************************/
time_t pull_dos_date(const uint8_t *date_ptr, int zone_offset)
{
	uint32_t dos_date=0;
	struct tm t;
	time_t ret;

	dos_date = IVAL(date_ptr,0);

	if (dos_date == 0) return (time_t)0;
  
	interpret_dos_date(dos_date,&t.tm_year,&t.tm_mon,
			   &t.tm_mday,&t.tm_hour,&t.tm_min,&t.tm_sec);
	t.tm_isdst = -1;
  
	ret = timegm(&t);

	ret += zone_offset;

	return ret;
}

/*******************************************************************
like make_unix_date() but the words are reversed
********************************************************************/
time_t pull_dos_date2(const uint8_t *date_ptr, int zone_offset)
{
	uint32_t x,x2;

	x = IVAL(date_ptr,0);
	x2 = ((x&0xFFFF)<<16) | ((x&0xFFFF0000)>>16);
	SIVAL(&x,0,x2);

	return pull_dos_date((void *)&x, zone_offset);
}

/*******************************************************************
  create a unix GMT date from a dos date in 32 bit "unix like" format
  these generally arrive as localtimes, with corresponding DST
  ******************************************************************/
time_t pull_dos_date3(const uint8_t *date_ptr, int zone_offset)
{
	time_t t = (time_t)IVAL(date_ptr,0);
	if (!null_mtime(t)) {
		t += zone_offset;
	}
	return t;
}


/***************************************************************************
return a HTTP/1.0 time string
  ***************************************************************************/
char *http_timestring(TALLOC_CTX *mem_ctx, time_t t)
{
	char *buf;
	char tempTime[60];
	struct tm *tm = localtime(&t);

	if (!tm) {
		return talloc_asprintf(mem_ctx,"%ld seconds since the Epoch",(long)t);
	}

#ifndef HAVE_STRFTIME
	buf = talloc_strdup(mem_ctx, asctime(tm));
	if (buf[strlen(buf)-1] == '\n') {
		buf[strlen(buf)-1] = 0;
	}
#else
	strftime(tempTime, sizeof(tempTime)-1, "%a, %d %b %Y %H:%M:%S %Z", tm);
	buf = talloc_strdup(mem_ctx, tempTime);
#endif /* !HAVE_STRFTIME */

	return buf;
}

/***************************************************************************
return a LDAP time string
  ***************************************************************************/
char *ldap_timestring(TALLOC_CTX *mem_ctx, time_t t)
{
	struct tm *tm = gmtime(&t);

	if (!tm) {
		return NULL;
	}

	/* formatted like: 20040408072012.0Z */
	return talloc_asprintf(mem_ctx, 
			       "%04u%02u%02u%02u%02u%02u.0Z",
			       tm->tm_year+1900, tm->tm_mon+1,
			       tm->tm_mday, tm->tm_hour, tm->tm_min,
			       tm->tm_sec);
}


/****************************************************************************
 Return the date and time as a string
****************************************************************************/

char *timestring_x(BOOL hires)
{
	static fstring TimeBuf;
	struct timeval tp;
	time_t t;
	struct tm *tm;

	if (hires) {
		GetTimeOfDay(&tp);
		t = (time_t)tp.tv_sec;
	} else {
		t = time(NULL);
	}
	tm = LocalTime(&t);
	if (!tm) {
		if (hires) {
			slprintf(TimeBuf,
				 sizeof(TimeBuf)-1,
				 "%ld.%06ld seconds since the Epoch",
				 (long)tp.tv_sec, 
				 (long)tp.tv_usec);
		} else {
			slprintf(TimeBuf,
				 sizeof(TimeBuf)-1,
				 "%ld seconds since the Epoch",
				 (long)t);
		}
	} else {
#ifdef HAVE_STRFTIME
		if (hires) {
			strftime(TimeBuf,sizeof(TimeBuf)-1,"%Y/%m/%d %H:%M:%S",tm);
			slprintf(TimeBuf+strlen(TimeBuf),
				 sizeof(TimeBuf)-1 - strlen(TimeBuf), 
				 ".%06ld", 
				 (long)tp.tv_usec);
		} else {
			strftime(TimeBuf,sizeof(TimeBuf)-1,"%Y/%m/%d %H:%M:%S",tm);
		}
#else
		if (hires) {
			slprintf(TimeBuf, 
				 sizeof(TimeBuf)-1, 
				 "%s.%06ld", 
				 asctime(tm), 
				 (long)tp.tv_usec);
		} else {
			fstrcpy(TimeBuf, asctime(tm));
		}
#endif
	}
	return(TimeBuf);
}

/****************************************************************************
 Return the date and time as a string
****************************************************************************/
char *timestring(TALLOC_CTX *mem_ctx, time_t t)
{
	char *TimeBuf;
	char tempTime[80];
	struct tm *tm;

	tm = localtime(&t);
	if (!tm) {
		return talloc_asprintf(mem_ctx,
				       "%ld seconds since the Epoch",
				       (long)t);
	}

#ifdef HAVE_STRFTIME
	/* some versions of gcc complain about using %c. This is a bug
	   in the gcc warning, not a bug in this code. See a recent
	   strftime() manual page for details.
	 */
	strftime(tempTime,sizeof(tempTime)-1,"%c %Z",tm);
	TimeBuf = talloc_strdup(mem_ctx, tempTime);
#else
	TimeBuf = talloc_strdup(mem_ctx, asctime(tm));
#endif

	return TimeBuf;
}

/*
  return a talloced string representing a NTTIME for human consumption
*/
const char *nt_time_string(TALLOC_CTX *mem_ctx, NTTIME nt)
{
	time_t t = nt_time_to_unix(nt);
	return talloc_strdup(mem_ctx, timestring(mem_ctx, t));
}


/*
  put a NTTIME into a packet
*/
void push_nttime(void *base, uint16_t offset, NTTIME t)
{
	SBVAL(base, offset,   t);
}

/*
  pull a NTTIME from a packet
*/
NTTIME pull_nttime(void *base, uint16_t offset)
{
	NTTIME ret = BVAL(base, offset);
	return ret;
}

SMB_BIG_INT usec_time_diff(struct timeval *larget, struct timeval *smallt)
{
	SMB_BIG_INT sec_diff = larget->tv_sec - smallt->tv_sec;
	return (sec_diff * 1000000) + (SMB_BIG_INT)(larget->tv_usec - smallt->tv_usec);
}
