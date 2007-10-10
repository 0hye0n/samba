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
#include "system/time.h"

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
check if it's a null unix time
****************************************************************************/
BOOL null_time(time_t t)
{
	return t == 0 || 
		t == (time_t)0xFFFFFFFF || 
		t == (time_t)-1;
}


/****************************************************************************
check if it's a null NTTIME
****************************************************************************/
BOOL null_nttime(NTTIME t)
{
	return t == 0 || t == (NTTIME)-1;
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
void push_dos_date(uint8_t *buf, int offset, time_t unixdate, int zone_offset)
{
	uint32_t x = make_dos_date(unixdate, zone_offset);
	SIVAL(buf,offset,x);
}

/*******************************************************************
put a dos date into a buffer (date/time format)
This takes GMT time and puts local time in the buffer
********************************************************************/
void push_dos_date2(uint8_t *buf,int offset,time_t unixdate, int zone_offset)
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
void push_dos_date3(uint8_t *buf,int offset,time_t unixdate, int zone_offset)
{
	if (!null_time(unixdate)) {
		unixdate -= zone_offset;
	}
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
	if (!null_time(t)) {
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

/*
  return a LDAP time string
*/
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


/*
  convert a LDAP time string to a time_t. Return 0 if unable to convert
*/
time_t ldap_string_to_time(const char *s)
{
	struct tm tm;
	
	if (s == NULL) return 0;
	
	ZERO_STRUCT(tm);
	if (sscanf(s, "%04u%02u%02u%02u%02u%02u.0Z", 
		   &tm.tm_year, &tm.tm_mon, &tm.tm_mday, 
		   &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
		return 0;
	}
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;
	
	return timegm(&tm);
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
	time_t t;
	if (nt == 0) {
		return "NTTIME(0)";
	}
	t = nt_time_to_unix(nt);
	return timestring(mem_ctx, t);
}


/*
  put a NTTIME into a packet
*/
void push_nttime(uint8_t *base, uint16_t offset, NTTIME t)
{
	SBVAL(base, offset,   t);
}

/*
  pull a NTTIME from a packet
*/
NTTIME pull_nttime(uint8_t *base, uint16_t offset)
{
	NTTIME ret = BVAL(base, offset);
	return ret;
}

/*
  parse a nttime as a large integer in a string and return a NTTIME
*/
NTTIME nttime_from_string(const char *s)
{
	return strtoull(s, NULL, 0);
}

/*
  return (tv1 - tv2) in microseconds
*/
int64_t usec_time_diff(struct timeval *tv1, struct timeval *tv2)
{
	int64_t sec_diff = tv1->tv_sec - tv2->tv_sec;
	return (sec_diff * 1000000) + (int64_t)(tv1->tv_usec - tv2->tv_usec);
}


/*
  return a zero timeval
*/
struct timeval timeval_zero(void)
{
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	return tv;
}

/*
  return True if a timeval is zero
*/
BOOL timeval_is_zero(struct timeval *tv)
{
	return tv->tv_sec == 0 && tv->tv_usec == 0;
}

/*
  return a timeval for the current time
*/
struct timeval timeval_current(void)
{
	struct timeval tv;
	GetTimeOfDay(&tv);
	return tv;
}

/*
  return a timeval struct with the given elements
*/
struct timeval timeval_set(uint32_t secs, uint32_t usecs)
{
	struct timeval tv;
	tv.tv_sec = secs;
	tv.tv_usec = usecs;
	return tv;
}


/*
  return a timeval ofs microseconds after tv
*/
struct timeval timeval_add(struct timeval *tv, uint32_t secs, uint32_t usecs)
{
	struct timeval tv2 = *tv;
	const uint_t million = 1000000;
	tv2.tv_sec += secs;
	tv2.tv_usec += usecs;
	tv2.tv_sec += tv2.tv_usec / million;
	tv2.tv_usec = tv2.tv_usec % million;
	return tv2;
}

/*
  return the sum of two timeval structures
*/
struct timeval timeval_sum(struct timeval *tv1, struct timeval *tv2)
{
	return timeval_add(tv1, tv2->tv_sec, tv2->tv_usec);
}

/*
  return a timeval secs/usecs into the future
*/
struct timeval timeval_current_ofs(uint32_t secs, uint32_t usecs)
{
	struct timeval tv = timeval_current();
	return timeval_add(&tv, secs, usecs);
}

/*
  compare two timeval structures. 
  Return 1 if tv2 > tv1
  Return 0 if tv2 == tv1
  Return -1 if tv2 < tv1
*/
int timeval_compare(struct timeval *tv1, struct timeval *tv2)
{
	if (tv2->tv_sec  > tv1->tv_sec)  return 1;
	if (tv2->tv_sec  < tv1->tv_sec)  return -1;
	if (tv2->tv_usec > tv1->tv_usec) return 1;
	if (tv2->tv_usec < tv1->tv_usec) return -1;
	return 0;
}

/*
  return True if a timer is in the past
*/
BOOL timeval_expired(struct timeval *tv)
{
	struct timeval tv2 = timeval_current();
	if (tv2.tv_sec > tv->tv_sec) return True;
	if (tv2.tv_sec < tv->tv_sec) return False;
	return (tv2.tv_usec >= tv->tv_usec);
}

/*
  return the number of seconds elapsed between two times
*/
double timeval_elapsed2(struct timeval *tv1, struct timeval *tv2)
{
	return (tv2->tv_sec - tv1->tv_sec) + 
	       (tv2->tv_usec - tv1->tv_usec)*1.0e-6;
}

/*
  return the number of seconds elapsed since a given time
*/
double timeval_elapsed(struct timeval *tv)
{
	struct timeval tv2 = timeval_current();
	return timeval_elapsed2(tv, &tv2);
}

/*
  return the lesser of two timevals
*/
struct timeval timeval_min(struct timeval *tv1, struct timeval *tv2)
{
	if (tv1->tv_sec < tv2->tv_sec) return *tv1;
	if (tv1->tv_sec > tv2->tv_sec) return *tv2;
	if (tv1->tv_usec < tv2->tv_usec) return *tv1;
	return *tv2;
}

/*
  return the greater of two timevals
*/
struct timeval timeval_max(struct timeval *tv1, struct timeval *tv2)
{
	if (tv1->tv_sec > tv2->tv_sec) return *tv1;
	if (tv1->tv_sec < tv2->tv_sec) return *tv2;
	if (tv1->tv_usec > tv2->tv_usec) return *tv1;
	return *tv2;
}

/*
  return the difference between two timevals as a timeval
  if tv1 comes after tv2, then return a zero timeval
  (this is *tv2 - *tv1)
*/
struct timeval timeval_until(struct timeval *tv1, struct timeval *tv2)
{
	struct timeval t;
	if (timeval_compare(tv2, tv1) >= 0) {
		return timeval_zero();
	}
	t.tv_sec = tv2->tv_sec - tv1->tv_sec;
	if (tv1->tv_usec > tv2->tv_usec) {
		t.tv_sec--;
		t.tv_usec = 1000000 - (tv1->tv_usec - tv2->tv_usec);
	} else {
		t.tv_usec = tv2->tv_usec - tv1->tv_usec;
	}
	return t;
}


/*
  convert a timeval to a NTTIME
*/
NTTIME timeval_to_nttime(struct timeval *tv)
{
	return 10*(tv->tv_usec + 
		  ((TIME_FIXUP_CONSTANT + (uint64_t)tv->tv_sec) * 1000000));
}
