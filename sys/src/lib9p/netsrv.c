#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

static Srv*
clonesrv(Srv* from)
{
	Srv*	to;

	/* If you call leak it might seem that we leak this
	 * srv and all its resources.
	 * That's not true. netsrvproc process
	 * releases to and its resources, but the parent
	 * process reuses the variable to allocate new
	 * servers, and that seems to confuse leak
	 * for a while.
	 */
	to = emalloc9p(sizeof(Srv));
	*to = *from;
	to->infd = to->outfd = to->srvfd = -1;
	to->fpool = nil;
	to->rpool = nil;
	to->rbuf = to->wbuf = nil;
	to->addr = nil;
	memset(&to->rlock, 0, sizeof(to->rlock));
	memset(&to->wlock, 0, sizeof(to->wlock));

	return to;
}

Srv*
newclientsrv(Srv* s, char* adir, int* lfdp)
{
	Srv*	msrv;
	char	ldir[40];
	int	dfd;
	NetConnInfo*	ni;	// I love these names that break indent
	
	*lfdp = listen(adir, ldir);
	if (*lfdp < 0)
		sysfatal("listen: %r");
	dfd = accept(*lfdp, ldir);
	if (dfd < 0){
		fprint(2, "%s: srvlistener: %r\n", argv0);
		return nil;
	}
	msrv = clonesrv(s);
	msrv->infd = msrv->outfd = dfd;
	ni = getnetconninfo(ldir, *lfdp);
	if (ni){
		msrv->addr = estrdup9p(ni->raddr);
		freenetconninfo(ni);
	} else
		msrv->addr = nil;
	return msrv;
}

static void
netsrvproc(Srv*	s)
{
	if (chatty9p)
		fprint(2, "%d %s: new srv: %s\n", getpid(), argv0, s->addr);
	srv(s);
	if (chatty9p)
		fprint(2, "%d %s: exiting: %s\n", getpid(), argv0, s->addr);
	close(s->infd);
	free(s->addr);
	free(s);
	_exits(0);
}

static void
srvlistener(Srv* s)
{
	char	adir[40];
	Srv*	msrv;
	int	afd,lfd;

	afd = announce(s->addr, adir);
	if (afd < 0)
		sysfatal("announce: %r");
	rendezvous((ulong)srvlistener, 0);
	for(;;){
		msrv = newclientsrv(s, adir, &lfd);
		switch(rfork(RFPROC|RFMEM|RFNOWAIT)){
		case -1:
			sysfatal("srvlistener: %r");
		case 0:
			netsrvproc(msrv);
			break;
		default:
			close(lfd);
		}
	}
}

int
netsrv(Srv* srv, char* addr)
{
	int	pid;

	srv->addr = estrdup9p(addr);
	srv->slock = emalloc9p(sizeof(QLock));
	memset(srv->slock, 0, sizeof(QLock));
	switch(pid = rfork(RFPROC|RFMEM|RFNAMEG|RFNOWAIT)){
	case -1:
		sysfatal("netsrv: %r");
	case 0:
		srvlistener(srv);
		_exits(0);
	default:
		rendezvous((ulong)srvlistener, 0);
	}
	return pid;
}
