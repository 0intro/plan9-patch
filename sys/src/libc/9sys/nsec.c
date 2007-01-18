#include <u.h>
#include <libc.h>


static uvlong order = 0x0001020304050607ULL;

static void
be2vlong(vlong *to, uchar *f)
{
	uchar *t, *o;
	int i;

	t = (uchar*)to;
	o = (uchar*)&order;
	for(i = 0; i < sizeof order; i++)
		t[o[i]] = f[i];
}

/*
	Reopen /dev/bintime on a failed read on the theory
	that the fd was mistakenly closed.  Beware rfork(RFFDG).
*/
vlong
nsec(void)
{
	static int fd = -1;
	uchar b[8];
	vlong t;
	int i;

	for(i = 0; i < 2; i++){
		if(fd < 0 && (fd = open("/dev/bintime", OREAD|OCEXEC)) < 0)
			return 0;
		if(pread(fd, b, sizeof b, 0) == sizeof b){
			be2vlong(&t, b);
			return t;
		}
		close(fd);
		fd = -1;
	}
	return 0;
}
