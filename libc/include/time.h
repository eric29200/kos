#ifndef _LIBC_TIME_H_
#define _LIBC_TIME_H_

#include <stdio.h>

#define CLOCK_REALTIME		0

struct timespec {
	time_t			tv_sec;
	long			tv_nsec;
};

struct tm {
	int			tm_sec;
	int			tm_min;
	int			tm_hour;
	int			tm_mday;
	int			tm_mon;
	int			tm_year;
	int			tm_wday;
	int			tm_yday;
	int			tm_isdst;
	long			__tm_gmtoff;
	const char *		__tm_zone;
};

time_t time(time_t *tloc);
int clock_gettime(clockid_t clockid, struct timespec *tp);

#endif
