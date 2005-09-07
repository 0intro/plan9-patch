#include <u.h>
#include <libc.h>

int dflag;

int dialerr(char*, char*);
void xfer(int, int);

void
usage(void)
{
	fprint(2, "usage: %s server client\n", argv0);
	exits("usage");
}

void
main(int argc, char **argv)
{
	int sfd, cfd;

	ARGBEGIN{
	case 'd':
		dflag ++;
	}ARGEND;

	if(argc != 2)
		usage();

	sfd = dialerr(argv[0], "server");
	cfd = dialerr(argv[1], "client");

	rfork(RFNOTEG);
	switch(fork()){
	case -1:
		fprint(2, "%s: fork: %r\n", argv0);
		exits("fork");
	case 0:
		xfer(sfd, cfd);
		break;
	default:
		xfer(cfd, sfd);
		break;
	}
	postnote(PNGROUP, getpid(), "die yankee pig dog");
	exits(0);
}

int
dialerr(char *dstr, char *msg)
{
	int fd;

	fd = dial(dstr, 0, 0, 0);
	if(fd < 0){
		fprint(2, "%s: dialing %s: %r\n", argv0, dstr);
		exits(smprint("dial %s", msg));
	}
	return fd;
}

/*
 * borrowed from /sys/src/cmd/aux/trampoline.c
 */
void
xfer(int from, int to)
{

	char buf[12*1024];
	int n;

	while((n = read(from, buf, sizeof buf)) > 0)
		if(write(to, buf, n) < 0)
			break;
}
