#include <u.h>
#include <libc.h>

int iflg, nflg, uflg;

void
main(int argc, char *argv[])
{
	ulong now;
	int tzhr, tzmin;
	struct Tm *tm;

	ARGBEGIN{
	case 'i':	iflg = 1; break;
	case 'n':	nflg = 1; break;
	case 'u':	uflg = 1; break;
	default:	fprint(2, "usage: date [-inu] [seconds]\n"); exits("usage");
	}ARGEND

	if(argc == 1)
		now = strtoul(*argv, 0, 0);
	else
		now = time(0);

	if(nflg)
		print("%ld\n", now);
	else if(iflg){
		if(uflg)
			tm = gmtime(now);
		else
			tm = localtime(now);
		tzhr = tm->tzoff / 3600;
		tzmin = (abs(tm->tzoff) - (abs(tzhr) * 3600)) / 60;
		print("%0.4d%0.2d%0.2dT%0.2d%0.2d%0.2d%+.2d%.2d\n",
			tm->year + 1900, tm->mon + 1, tm->mday,
			tm->hour, tm->min, tm->sec, tzhr, tzmin);
	}
	else if(uflg)
		print("%s", asctime(gmtime(now)));
	else
		print("%s", ctime(now));
	
	exits(0);
}
