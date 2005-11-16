#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

extern Srv* newclientsrv(Srv*, char*, int*);	// common code at netsrv.c

static void
netsrvproc(void* a)
{
	Srv*	s = a;

	if (chatty9p)
		fprint(2, "%d %s: new srv: %s\n", getpid(), argv0, s->addr);
	srv(s);
	if (chatty9p)
		fprint(2, "%d %s: exiting: %s\n", getpid(), argv0, s->addr);
	close(s->infd);
	free(s->addr);
	free(s);
	threadexits(nil);
}

static void
srvlistener(void *a)
{
	Srv*	s = a;
	int	afd, lfd;
	char	adir[40];
	Srv*	msrv;

	afd = announce(s->addr, adir);
	if (afd < 0)
		sysfatal("announce: %r");
	rendezvous((ulong)srvlistener, getpid());
	for(;;){
		msrv = newclientsrv(s, adir, &lfd);
		proccreate(netsrvproc, msrv, mainstacksize);
		close(lfd);
	}
}

int
threadnetsrv(Srv* srv, char* addr)
{

	srv->addr = estrdup9p(addr);
	srv->slock = emalloc9p(sizeof(QLock));
	memset(srv->slock, 0, sizeof(QLock));
	if (procrfork(srvlistener, srv, mainstacksize, RFNAMEG) < 0)
		sysfatal("netsrv: %r");
	return  rendezvous((ulong)srvlistener, 0);
}
