/* 
   Unix SMB/CIFS implementation.
   time handling functions

   Copyright (C) Andrew Tridgell 		1992-2004
   Copyright (C) Stefan (metze) Metzmacher	2002   
   Copyright (C) Jeremy Allison			2007

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"

/**
 * @file
 * @brief time handling functions
 */


#ifndef TIME_T_MIN
#define TIME_T_MIN ((time_t)0 < (time_t) -1 ? (time_t) 0 \
		    : ~ (time_t) 0 << (sizeof (time_t) * CHAR_BIT - 1))
#endif
#ifndef TIME_T_MAX
#define TIME_T_MAX (~ (time_t) 0 - TIME_T_MIN)
#endif

#define NTTIME_INFINITY (NTTIME)0x8000000000000000LL

#if (SIZEOF_LONG == 8)
#define TIME_FIXUP_CONSTANT_INT 11644473600L
#elif (SIZEOF_LONG_LONG == 8)
#define TIME_FIXUP_CONSTANT_INT 11644473600LL
#endif

/*******************************************************************
  create a 16 bit dos packed date
********************************************************************/
static uint16_t make_dos_date1(struct tm *t)
{
	uint16_t ret=0;
	ret = (((unsigned int)(t->tm_mon+1)) >> 3) | ((t->tm_year-80) << 1);
	ret = ((ret&0xFF)<<8) | (t->tm_mday | (((t->tm_mon+1) & 0x7) << 5));
	return ret;
}

/*******************************************************************
  create a 16 bit dos packed time
********************************************************************/
static uint16_t make_dos_time1(struct tm *t)
{
	uint16_t ret=0;
	ret = ((((unsigned int)t->tm_min >> 3)&0x7) | (((unsigned int)t->tm_hour) << 3));
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

/**
  parse a nttime as a large integer in a string and return a NTTIME
*/
NTTIME nttime_from_string(const char *s)
{
	return strtoull(s, NULL, 0);
}

/**************************************************************
 Handle conversions between time_t and uint32, taking care to
 preserve the "special" values.
**************************************************************/

uint32_t convert_time_t_to_uint32(time_t t)
{
#if (defined(SIZEOF_TIME_T) && (SIZEOF_TIME_T == 8))
	/* time_t is 64-bit. */
	if (t == 0x8000000000000000LL) {
		return 0x80000000;
	} else if (t == 0x7FFFFFFFFFFFFFFFLL) {
		return 0x7FFFFFFF;
	}
#endif
	return (uint32_t)t;
}

time_t convert_uint32_to_time_t(uint32_t u)
{
#if (defined(SIZEOF_TIME_T) && (SIZEOF_TIME_T == 8))
	/* time_t is 64-bit. */
	if (u == 0x80000000) {
		return (time_t)0x8000000000000000LL;
	} else if (u == 0x7FFFFFFF) {
		return (time_t)0x7FFFFFFFFFFFFFFFLL;
	}
#endif
	return (time_t)u;
}

/****************************************************************************
 Check if NTTIME is 0.
****************************************************************************/

bool nt_time_is_zero(const NTTIME *nt)
{
	return (*nt == 0);
}

/****************************************************************************
 Convert ASN.1 GeneralizedTime string to unix-time.
 Returns 0 on failure; Currently ignores timezone. 
****************************************************************************/

time_t generalized_to_unix_time(const char *str)
{ 
	struct tm tm;

	ZERO_STRUCT(tm);

	if (sscanf(str, "%4d%2d%2d%2d%2d%2d", 
		   &tm.tm_year, &tm.tm_mon, &tm.tm_mday, 
		   &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
		return 0;
	}
	tm.tm_year -= 1900;
	tm.tm_mon -= 1;

	return timegm(&tm);
}

/*******************************************************************
 Accessor function for the server time zone offset.
 set_server_zone_offset() must have been called first.
******************************************************************/

static int server_zone_offset;

int get_server_zone_offset(void)
{
	return server_zone_offset;
}

/*******************************************************************
 Initialize the server time zone offset. Called when a client connects.
******************************************************************/

int set_server_zone_offset(time_t t)
{
	server_zone_offset = get_time_zone(t);
	return server_zone_offset;
}

/****************************************************************************
 Return the date and time as a string
****************************************************************************/

char *current_timestring(TALLOC_CTX *ctx, bool hires)
{
	fstring TimeBuf;
	struct timeval tp;
	time_t t;
	struct tm *tm;

	if (hires) {
		GetTimeOfDay(&tp);
		t = (time_t)tp.tv_sec;
	} else {
		t = time(NULL);
	}
	tm = localtime(&t);
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
			const char *asct = asctime(tm);
			slprintf(TimeBuf, 
				 sizeof(TimeBuf)-1, 
				 "%s.%06ld", 
				 asct ? asct : "unknown", 
				 (long)tp.tv_usec);
		} else {
			const char *asct = asctime(tm);
			fstrcpy(TimeBuf, asct ? asct : "unknown");
		}
#endif
	}
	return talloc_strdup(ctx, TimeBuf);
}


/*******************************************************************
 Put a dos date into a buffer (time/date format).
 This takes GMT time and puts local time in the buffer.
********************************************************************/

static void put_dos_date(char *buf,int offset,time_t unixdate, int zone_offset)
{
	uint32_t x = make_dos_date(unixdate, zone_offset);
	SIVAL(buf,offset,x);
}

/*******************************************************************
 Put a dos date into a buffer (date/time format).
 This takes GMT time and puts local time in the buffer.
********************************************************************/

static void put_dos_date2(char *buf,int offset,time_t unixdate, int zone_offset)
{
	uint32_t x = make_dos_date(unixdate, zone_offset);
	x = ((x&0xFFFF)<<16) | ((x&0xFFFF0000)>>16);
	SIVAL(buf,offset,x);
}

/*******************************************************************
 Put a dos 32 bit "unix like" date into a buffer. This routine takes
 GMT and converts it to LOCAL time before putting it (most SMBs assume
 localtime for this sort of date)
********************************************************************/

static void put_dos_date3(char *buf,int offset,time_t unixdate, int zone_offset)
{
	if (!null_mtime(unixdate)) {
		unixdate -= zone_offset;
	}
	SIVAL(buf,offset,unixdate);
}


/***************************************************************************
 Server versions of the above functions.
***************************************************************************/

void srv_put_dos_date(char *buf,int offset,time_t unixdate)
{
	put_dos_date(buf, offset, unixdate, server_zone_offset);
}

void srv_put_dos_date2(char *buf,int offset, time_t unixdate)
{
	put_dos_date2(buf, offset, unixdate, server_zone_offset);
}

void srv_put_dos_date3(char *buf,int offset,time_t unixdate)
{
	put_dos_date3(buf, offset, unixdate, server_zone_offset);
}

/****************************************************************************
 Take a Unix time and convert to an NTTIME structure and place in buffer 
 pointed to by p.
****************************************************************************/

void put_long_date_timespec(char *p, struct timespec ts)
{
	NTTIME nt;
	unix_timespec_to_nt_time(&nt, ts);
	SIVAL(p, 0, nt & 0xFFFFFFFF);
	SIVAL(p, 4, nt >> 32);
}

void put_long_date(char *p, time_t t)
{
	struct timespec ts;
	ts.tv_sec = t;
	ts.tv_nsec = 0;
	put_long_date_timespec(p, ts);
}

/****************************************************************************
 Return the best approximation to a 'create time' under UNIX from a stat
 structure.
****************************************************************************/

static time_t calc_create_time(const SMB_STRUCT_STAT *st)
{
	time_t ret, ret1;

	ret = MIN(st->st_ctime, st->st_mtime);
	ret1 = MIN(ret, st->st_atime);

	if(ret1 != (time_t)0) {
		return ret1;
	}

	/*
	 * One of ctime, mtime or atime was zero (probably atime).
	 * Just return MIN(ctime, mtime).
	 */
	return ret;
}

/****************************************************************************
 Return the 'create time' from a stat struct if it exists (birthtime) or else
 use the best approximation.
****************************************************************************/

struct timespec get_create_timespec(const SMB_STRUCT_STAT *pst,bool fake_dirs)
{
	struct timespec ret;

	if(S_ISDIR(pst->st_mode) && fake_dirs) {
		ret.tv_sec = 315493200L;          /* 1/1/1980 */
		ret.tv_nsec = 0;
		return ret;
	}

#if defined(HAVE_STRUCT_STAT_ST_BIRTHTIMESPEC_TV_NSEC)
	ret = pst->st_birthtimespec;
#elif defined(HAVE_STRUCT_STAT_ST_BIRTHTIMENSEC)
	ret.tv_sec = pst->st_birthtime;
	ret.tv_nsec = pst->st_birthtimenspec;
#elif defined(HAVE_STRUCT_STAT_ST_BIRTHTIME)
	ret.tv_sec = pst->st_birthtime;
	ret.tv_nsec = 0;
#else
	ret.tv_sec = calc_create_time(pst);
	ret.tv_nsec = 0;
#endif

	/* Deal with systems that don't initialize birthtime correctly.
	 * Pointed out by SATOH Fumiyasu <fumiyas@osstech.jp>.
	 */
	if (null_timespec(ret)) {
		ret.tv_sec = calc_create_time(pst);
		ret.tv_nsec = 0;
	}
	return ret;
}

/****************************************************************************
 Get/Set all the possible time fields from a stat struct as a timespec.
****************************************************************************/

struct timespec get_atimespec(const SMB_STRUCT_STAT *pst)
{
#if !defined(HAVE_STAT_HIRES_TIMESTAMPS)
	struct timespec ret;

	/* Old system - no ns timestamp. */
	ret.tv_sec = pst->st_atime;
	ret.tv_nsec = 0;
	return ret;
#else
#if defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
	return pst->st_atim;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
	struct timespec ret;
	ret.tv_sec = pst->st_atime;
	ret.tv_nsec = pst->st_atimensec;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
	struct timespec ret;
	ret.tv_sec = pst->st_atime;
	ret.tv_nsec = pst->st_atime_n;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_UMTIME)
	struct timespec ret;
	ret.tv_sec = pst->st_atime;
	ret.tv_nsec = pst->st_uatime * 1000;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
	return pst->st_atimespec;
#else
#error	CONFIGURE_ERROR_IN_DETECTING_TIMESPEC_IN_STAT 
#endif
#endif
}

void set_atimespec(SMB_STRUCT_STAT *pst, struct timespec ts)
{
#if !defined(HAVE_STAT_HIRES_TIMESTAMPS)
	/* Old system - no ns timestamp. */
	pst->st_atime = ts.tv_sec;
#else
#if defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
	pst->st_atim = ts;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
	pst->st_atime = ts.tv_sec;
	pst->st_atimensec = ts.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
	pst->st_atime = ts.tv_sec;
	pst->st_atime_n = ts.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_UMTIME)
	pst->st_atime = ts.tv_sec;
	pst->st_uatime = ts.tv_nsec / 1000;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
	pst->st_atimespec = ts;
#else
#error	CONFIGURE_ERROR_IN_DETECTING_TIMESPEC_IN_STAT 
#endif
#endif
}

struct timespec get_mtimespec(const SMB_STRUCT_STAT *pst)
{
#if !defined(HAVE_STAT_HIRES_TIMESTAMPS)
	struct timespec ret;

	/* Old system - no ns timestamp. */
	ret.tv_sec = pst->st_mtime;
	ret.tv_nsec = 0;
	return ret;
#else
#if defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
	return pst->st_mtim;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
	struct timespec ret;
	ret.tv_sec = pst->st_mtime;
	ret.tv_nsec = pst->st_mtimensec;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
	struct timespec ret;
	ret.tv_sec = pst->st_mtime;
	ret.tv_nsec = pst->st_mtime_n;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_UMTIME)
	struct timespec ret;
	ret.tv_sec = pst->st_mtime;
	ret.tv_nsec = pst->st_umtime * 1000;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
	return pst->st_mtimespec;
#else
#error	CONFIGURE_ERROR_IN_DETECTING_TIMESPEC_IN_STAT 
#endif
#endif
}

void set_mtimespec(SMB_STRUCT_STAT *pst, struct timespec ts)
{
#if !defined(HAVE_STAT_HIRES_TIMESTAMPS)
	/* Old system - no ns timestamp. */
	pst->st_mtime = ts.tv_sec;
#else
#if defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
	pst->st_mtim = ts;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
	pst->st_mtime = ts.tv_sec;
	pst->st_mtimensec = ts.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
	pst->st_mtime = ts.tv_sec;
	pst->st_mtime_n = ts.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_UMTIME)
	pst->st_mtime = ts.tv_sec;
	pst->st_umtime = ts.tv_nsec / 1000;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
	pst->st_mtimespec = ts;
#else
#error	CONFIGURE_ERROR_IN_DETECTING_TIMESPEC_IN_STAT 
#endif
#endif
}

struct timespec get_ctimespec(const SMB_STRUCT_STAT *pst)
{
#if !defined(HAVE_STAT_HIRES_TIMESTAMPS)
	struct timespec ret;

	/* Old system - no ns timestamp. */
	ret.tv_sec = pst->st_ctime;
	ret.tv_nsec = 0;
	return ret;
#else
#if defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
	return pst->st_ctim;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
	struct timespec ret;
	ret.tv_sec = pst->st_ctime;
	ret.tv_nsec = pst->st_ctimensec;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
	struct timespec ret;
	ret.tv_sec = pst->st_ctime;
	ret.tv_nsec = pst->st_ctime_n;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_UMTIME)
	struct timespec ret;
	ret.tv_sec = pst->st_ctime;
	ret.tv_nsec = pst->st_uctime * 1000;
	return ret;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
	return pst->st_ctimespec;
#else
#error	CONFIGURE_ERROR_IN_DETECTING_TIMESPEC_IN_STAT 
#endif
#endif
}

void set_ctimespec(SMB_STRUCT_STAT *pst, struct timespec ts)
{
#if !defined(HAVE_STAT_HIRES_TIMESTAMPS)
	/* Old system - no ns timestamp. */
	pst->st_ctime = ts.tv_sec;
#else
#if defined(HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC)
	pst->st_ctim = ts;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMENSEC)
	pst->st_ctime = ts.tv_sec;
	pst->st_ctimensec = ts.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_MTIME_N)
	pst->st_ctime = ts.tv_sec;
	pst->st_ctime_n = ts.tv_nsec;
#elif defined(HAVE_STRUCT_STAT_ST_UMTIME)
	pst->st_ctime = ts.tv_sec;
	pst->st_uctime = ts.tv_nsec / 1000;
#elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC)
	pst->st_ctimespec = ts;
#else
#error	CONFIGURE_ERROR_IN_DETECTING_TIMESPEC_IN_STAT 
#endif
#endif
}

void dos_filetime_timespec(struct timespec *tsp)
{
	tsp->tv_sec &= ~1;
	tsp->tv_nsec = 0;
}

/*******************************************************************
 Create a unix date (int GMT) from a dos date (which is actually in
 localtime).
********************************************************************/

static time_t make_unix_date(const void *date_ptr, int zone_offset)
{
	uint32_t dos_date=0;
	struct tm t;
	time_t ret;

	dos_date = IVAL(date_ptr,0);

	if (dos_date == 0) {
		return 0;
	}
  
	interpret_dos_date(dos_date,&t.tm_year,&t.tm_mon,
			&t.tm_mday,&t.tm_hour,&t.tm_min,&t.tm_sec);
	t.tm_isdst = -1;
  
	ret = timegm(&t);

	ret += zone_offset;

	return(ret);
}

/*******************************************************************
 Like make_unix_date() but the words are reversed.
********************************************************************/

static time_t make_unix_date2(const void *date_ptr, int zone_offset)
{
	uint32_t x,x2;

	x = IVAL(date_ptr,0);
	x2 = ((x&0xFFFF)<<16) | ((x&0xFFFF0000)>>16);
	SIVAL(&x,0,x2);

	return(make_unix_date((const void *)&x, zone_offset));
}

/*******************************************************************
 Create a unix GMT date from a dos date in 32 bit "unix like" format
 these generally arrive as localtimes, with corresponding DST.
******************************************************************/

static time_t make_unix_date3(const void *date_ptr, int zone_offset)
{
	time_t t = (time_t)IVAL(date_ptr,0);
	if (!null_mtime(t)) {
		t += zone_offset;
	}
	return(t);
}

time_t srv_make_unix_date(const void *date_ptr)
{
	return make_unix_date(date_ptr, server_zone_offset);
}

time_t srv_make_unix_date2(const void *date_ptr)
{
	return make_unix_date2(date_ptr, server_zone_offset);
}

time_t srv_make_unix_date3(const void *date_ptr)
{
	return make_unix_date3(date_ptr, server_zone_offset);
}

/****************************************************************************
 Convert a normalized timeval to a timespec.
****************************************************************************/

struct timespec convert_timeval_to_timespec(const struct timeval tv)
{
	struct timespec ts;
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = tv.tv_usec * 1000;
	return ts;
}

/****************************************************************************
 Convert a normalized timespec to a timeval.
****************************************************************************/

struct timeval convert_timespec_to_timeval(const struct timespec ts)
{
	struct timeval tv;
	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = ts.tv_nsec / 1000;
	return tv;
}

/****************************************************************************
 Return a timespec for the current time
****************************************************************************/

struct timespec timespec_current(void)
{
	struct timeval tv;
	struct timespec ts;
	GetTimeOfDay(&tv);
	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = tv.tv_usec * 1000;
	return ts;
}

/****************************************************************************
 Return the lesser of two timespecs.
****************************************************************************/

struct timespec timespec_min(const struct timespec *ts1,
			   const struct timespec *ts2)
{
	if (ts1->tv_sec < ts2->tv_sec) return *ts1;
	if (ts1->tv_sec > ts2->tv_sec) return *ts2;
	if (ts1->tv_nsec < ts2->tv_nsec) return *ts1;
	return *ts2;
}

/****************************************************************************
  compare two timespec structures. 
  Return -1 if ts1 < ts2
  Return 0 if ts1 == ts2
  Return 1 if ts1 > ts2
****************************************************************************/

int timespec_compare(const struct timespec *ts1, const struct timespec *ts2)
{
	if (ts1->tv_sec  > ts2->tv_sec)  return 1;
	if (ts1->tv_sec  < ts2->tv_sec)  return -1;
	if (ts1->tv_nsec > ts2->tv_nsec) return 1;
	if (ts1->tv_nsec < ts2->tv_nsec) return -1;
	return 0;
}

/****************************************************************************
 Interprets an nt time into a unix struct timespec.
 Differs from nt_time_to_unix in that an 8 byte value of 0xffffffffffffffff
 will be returned as (time_t)-1, whereas nt_time_to_unix returns 0 in this case.
****************************************************************************/

struct timespec interpret_long_date(const char *p)
{
	NTTIME nt;
	nt = IVAL(p,0) + ((uint64_t)IVAL(p,4) << 32);
	if (nt == (uint64_t)-1) {
		struct timespec ret;
		ret.tv_sec = (time_t)-1;
		ret.tv_nsec = 0;
		return ret;
	}
	return nt_time_to_unix_timespec(&nt);
}

/***************************************************************************
 Client versions of the above functions.
***************************************************************************/

void cli_put_dos_date(struct cli_state *cli, char *buf, int offset, time_t unixdate)
{
	put_dos_date(buf, offset, unixdate, cli->serverzone);
}

void cli_put_dos_date2(struct cli_state *cli, char *buf, int offset, time_t unixdate)
{
	put_dos_date2(buf, offset, unixdate, cli->serverzone);
}

void cli_put_dos_date3(struct cli_state *cli, char *buf, int offset, time_t unixdate)
{
	put_dos_date3(buf, offset, unixdate, cli->serverzone);
}

time_t cli_make_unix_date(struct cli_state *cli, const void *date_ptr)
{
	return make_unix_date(date_ptr, cli->serverzone);
}

time_t cli_make_unix_date2(struct cli_state *cli, const void *date_ptr)
{
	return make_unix_date2(date_ptr, cli->serverzone);
}

time_t cli_make_unix_date3(struct cli_state *cli, const void *date_ptr)
{
	return make_unix_date3(date_ptr, cli->serverzone);
}

/****************************************************************************
 Check if two NTTIMEs are the same.
****************************************************************************/

bool nt_time_equals(const NTTIME *nt1, const NTTIME *nt2)
{
	return (*nt1 == *nt2);
}

/*******************************************************************
 Re-read the smb serverzone value.
******************************************************************/

static struct timeval start_time_hires;

void TimeInit(void)
{
	set_server_zone_offset(time(NULL));

	DEBUG(4,("TimeInit: Serverzone is %d\n", server_zone_offset));

	/* Save the start time of this process. */
	if (start_time_hires.tv_sec == 0 && start_time_hires.tv_usec == 0) {
		GetTimeOfDay(&start_time_hires);
	}
}

/**********************************************************************
 Return a timeval struct of the uptime of this process. As TimeInit is
 done before a daemon fork then this is the start time from the parent
 daemon start. JRA.
***********************************************************************/

void get_process_uptime(struct timeval *ret_time)
{
	struct timeval time_now_hires;

	GetTimeOfDay(&time_now_hires);
	ret_time->tv_sec = time_now_hires.tv_sec - start_time_hires.tv_sec;
	if (time_now_hires.tv_usec < start_time_hires.tv_usec) {
		ret_time->tv_sec -= 1;
		ret_time->tv_usec = 1000000 + (time_now_hires.tv_usec - start_time_hires.tv_usec);
	} else {
		ret_time->tv_usec = time_now_hires.tv_usec - start_time_hires.tv_usec;
	}
}

/****************************************************************************
 Convert a NTTIME structure to a time_t.
 It's originally in "100ns units".

 This is an absolute version of the one above.
 By absolute I mean, it doesn't adjust from 1/1/1601 to 1/1/1970
 if the NTTIME was 5 seconds, the time_t is 5 seconds. JFM
****************************************************************************/

time_t nt_time_to_unix_abs(const NTTIME *nt)
{
	uint64_t d;

	if (*nt == 0) {
		return (time_t)0;
	}

	if (*nt == (uint64_t)-1) {
		return (time_t)-1;
	}

	if (*nt == NTTIME_INFINITY) {
		return (time_t)-1;
	}

	/* reverse the time */
	/* it's a negative value, turn it to positive */
	d=~*nt;

	d += 1000*1000*10/2;
	d /= 1000*1000*10;

	if (!(TIME_T_MIN <= ((time_t)d) && ((time_t)d) <= TIME_T_MAX)) {
		return (time_t)0;
	}

	return (time_t)d;
}

time_t uint64s_nt_time_to_unix_abs(const uint64_t *src)
{
	NTTIME nttime;
	nttime = *src;
	return nt_time_to_unix_abs(&nttime);
}

/****************************************************************************
 Put a 8 byte filetime from a struct timespec. Uses GMT.
****************************************************************************/

void unix_timespec_to_nt_time(NTTIME *nt, struct timespec ts)
{
	uint64_t d;

	if (ts.tv_sec ==0 && ts.tv_nsec == 0) {
		*nt = 0;
		return;
	}
	if (ts.tv_sec == TIME_T_MAX) {
		*nt = 0x7fffffffffffffffLL;
		return;
	}		
	if (ts.tv_sec == (time_t)-1) {
		*nt = (uint64_t)-1;
		return;
	}		

	d = ts.tv_sec;
	d += TIME_FIXUP_CONSTANT_INT;
	d *= 1000*1000*10;
	/* d is now in 100ns units. */
	d += (ts.tv_nsec / 100);

	*nt = d;
}

/****************************************************************************
 Convert a time_t to a NTTIME structure

 This is an absolute version of the one above.
 By absolute I mean, it doesn't adjust from 1/1/1970 to 1/1/1601
 If the time_t was 5 seconds, the NTTIME is 5 seconds. JFM
****************************************************************************/

void unix_to_nt_time_abs(NTTIME *nt, time_t t)
{
	double d;

	if (t==0) {
		*nt = 0;
		return;
	}

	if (t == TIME_T_MAX) {
		*nt = 0x7fffffffffffffffLL;
		return;
	}
		
	if (t == (time_t)-1) {
		/* that's what NT uses for infinite */
		*nt = NTTIME_INFINITY;
		return;
	}		

	d = (double)(t);
	d *= 1.0e7;

	*nt = (NTTIME)d;

	/* convert to a negative value */
	*nt=~*nt;
}


/****************************************************************************
 Check if it's a null mtime.
****************************************************************************/

bool null_mtime(time_t mtime)
{
	if (mtime == 0 || mtime == (time_t)0xFFFFFFFF || mtime == (time_t)-1)
		return true;
	return false;
}

/****************************************************************************
 Utility function that always returns a const string even if localtime
 and asctime fail.
****************************************************************************/

const char *time_to_asc(const time_t t)
{
	const char *asct;
	struct tm *lt = localtime(&t);

	if (!lt) {
		return "unknown time";
	}

	asct = asctime(lt);
	if (!asct) {
		return "unknown time";
	}
	return asct;
}

const char *display_time(NTTIME nttime)
{
	float high;
	float low;
	int sec;
	int days, hours, mins, secs;

	if (nttime==0)
		return "Now";

	if (nttime==NTTIME_INFINITY)
		return "Never";

	high = 65536;	
	high = high/10000;
	high = high*65536;
	high = high/1000;
	high = high * (~(nttime >> 32));

	low = ~(nttime & 0xFFFFFFFF);
	low = low/(1000*1000*10);

	sec=(int)(high+low);

	days=sec/(60*60*24);
	hours=(sec - (days*60*60*24)) / (60*60);
	mins=(sec - (days*60*60*24) - (hours*60*60) ) / 60;
	secs=sec - (days*60*60*24) - (hours*60*60) - (mins*60);

	return talloc_asprintf(talloc_tos(), "%u days, %u hours, %u minutes, "
			       "%u seconds", days, hours, mins, secs);
}

bool nt_time_is_set(const NTTIME *nt)
{
	if (*nt == 0x7FFFFFFFFFFFFFFFLL) {
		return false;
	}

	if (*nt == NTTIME_INFINITY) {
		return false;
	}

	return true;
}
