#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include "sys9.h"

typedef unsigned long long uvlong;
typedef long long vlong;
typedef unsigned char uchar;

static uvlong order = 0x0001020304050607ULL;

static void
be2vlong(vlong *to, uchar *f)
{
	uchar *t, *o;
	int i;

	t = (uchar*)to;
	o = (uchar*)&order;
	for(i = 0; i < 8; i++)
		t[o[i]] = f[i];
}

int
gettimeofday(struct timeval *tp, struct timezone *tzp)
{
	static int fd = -1;
	uchar b[8];
	vlong t;
	int opened;

	opened = 0;
	if(fd < 0){
	reopen:
		if(opened++ || (fd = _OPEN("/dev/bintime", OREAD|OCEXEC)) < 0)
			return 0;
	}
	if(_PREAD(fd, b, sizeof b, 0) != sizeof b){
		_CLOSE(fd);
		fd = -1;
		goto reopen;
	}
	be2vlong(&t, b);

	tp->tv_sec = t/1000000000;
	tp->tv_usec = (t/1000)%1000000;

	if(tzp) {
		tzp->tz_minuteswest = 240;
		tzp->tz_dsttime = 1;
	}

	return 0;
}
