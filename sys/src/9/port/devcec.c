/*
 * Coraid Ethernet Console driver
 * kick (CEC) driver
 * Also requires hooks into devcons.c
 */
 
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "ureg.h"
#include "../port/error.h"
#include "../port/netif.h"

enum {
	Ncbuf 	= 8192,
	Ncmask 	= Ncbuf-1,
	Namelen	= 128,
};

enum{
	Tinita 	= 0,
	Tinitb,
	Tinitc,
	Tdata,
	Tack,
	Tdiscover,
	Toffer,
	Treset,
};

enum{
	Cunused	= 0,
	Cinitb,			// sent initb
	Clogin,			// login state
	Copen,			// up
};

typedef struct{
	Chan	*dc;
	Chan	*cc;
	Dev	*d;
	uchar	ea[6];
	char	path[32];
}If;

typedef struct{
	uchar	dst[6];
	uchar	src[6];
	uchar	etype[2];
	uchar	type;
	uchar	conn;
	uchar	seq;
	uchar	len;
	uchar	data[1500];
}Pkt;

typedef struct{
	QLock;
	Lock;
	uchar	ea[6];		// along with cno, the key to the connection
	uchar	cno;		// connection number on remote host
	uchar	stalled;		// cectimer needs to kick it -- cecputs while !islo()
	int	state;		// connection state
	int	idle;		// idle ticks
	int	to;		// ticks to timeout
	int	retries;	// remaining retries
	Block	*bp;		// unacked message
	If	*ifc;		// interface for this connection
	uchar	sndseq;		// sequence number of last sent message
	uchar	rcvseq;		// sequence number of last rcv'd message
	char	cbuf[Ncbuf];	// curcular buffer
	int	r, w;		// indexes into cbuf
	int	pwi;		// index into passwd;
	char	passwd[32];	// password typed by connection
}Conn;

/*
 * Since this code is in the output chain of procedures for console
 * output, we can't use the general printf functions.  See the ones
 * at the bottom of this file.  It assumes the serial port.
 */
 
static int cecprint(char *, ...);
extern int parseether(uchar *, char *);
extern Chan * chandial(char *, char *, char *, Chan **);

enum {
	Qdir = 0,
	Qstat,
	Qctl,
	Qdbg,
	Qcfg,
	CMsetshelf,
	CMsetname,
	CMtraceon,
	CMtraceoff,
	CMsetpasswd,
	CMcecon,
	CMcecoff,

	Nconns = 20,
};

static 	If 	ifs[4];
static 	char 	name[Namelen];
static	int	shelf = -1;
static 	Conn 	conn[Nconns];
static	int	tflag;		// trace flag
static	char	passwd[Namelen];
static	int	xmit;
static	int	rsnd;
static	Rendez trendez;
static	uchar	broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

Dirtab cecdir[] = {
	".",		{ Qdir, 0, QTDIR },	0,	DMDIR | 0555,
	"cecstat",	{ Qstat},		1,	0444,
	"cecctl",	{ Qctl},		1,	0222,
	"cecdbg",	{ Qdbg }, 		1,	0444,
	"ceccfg",	{ Qcfg },		1,	0444,
};

static Cmdtab ceccmd[] = {
	CMsetname,	"name",	2,
	CMtraceon,	"traceon",	1,
	CMtraceoff,	"traceoff",	1,
	CMsetpasswd,	"password",	2,
	CMcecon,		"cecon",		2,
	CMcecoff,		"cecoff",		2,
	CMsetshelf,	"shelf",	2,
};

static void
getaddr(char *path, uchar *ea)
{
	char buf[6*2+1];
	Chan *c;
	int n;
	
	snprint(up->genbuf, sizeof up->genbuf, "%s/addr", path);
	c = namec(up->genbuf, Aopen, OREAD, 0);
	if(waserror()) {
		cclose(c);
		nexterror();
	}
	n = devtab[c->type]->read(c, buf, sizeof buf-1, 0);
	if(n != 6*2)
		error("getaddr");
	buf[n] = 0;
	if(parseether(ea, buf) < 0)
		error("parseether failure");
	poperror();
	cclose(c);
}

static char *types[] = {
	"Tinita", "Tinitb", "Tinitc", "Tdata", "Tack", 
	"Tdiscover", "Toffer", "Treset", "*GOK*",
};

static int
cbget(Conn *cp)
{
	int c;
	
	if(cp->r == cp->w)
		return -1;
	c = cp->cbuf[cp->r];
	cp->r = (cp->r+1)&Ncmask;
	return c;
}

static void
cbput(Conn *cp, int c)
{
	if(cp->r == (cp->w+1)&Ncmask)	// full
		return;
	cp->cbuf[cp->w] = c;
	cp->w = (cp->w+1)&Ncmask;
}

	
static void
trace(Block *bp)
{
	Pkt *p;
	int type;
	
	if(tflag == 0)
		return;
	p = (Pkt *)bp->rp;
	type = p->type;
	if(type > Treset)
		type = Treset+1;
	cecprint("%E > %E) seq %d, type %s, len %d, conn %d\n",
		p->src, p->dst, p->seq, types[type], p->len, p->conn);
}

static Block *
sethdr(If *ifc, uchar *ea, Pkt **npp, int len)		// set header for response
{
	Block *bp;
	Pkt *np;

	len += 18;
	if(len < 60)
		len = 60;
	bp = allocb(len);
	bp->wp = bp->rp+len;
	np = (Pkt *)bp->rp;
	memmove(np->dst, ea, 6);
	memmove(np->src, ifc->ea, 6);
	np->etype[0] = 0xbc;
	np->etype[1] = 0xbc;
	np->seq = 0;
	*npp = np;
	return bp;
}

static void
send(Conn *cp, Block *bp)	// put on output queue
{
	Block *nbp;	
	
	if(cp->bp != nil)
		panic("cecsend: cp->bp not nil\n");
	nbp = allocb(BLEN(bp));
	memmove(nbp->wp, bp->rp, BLEN(bp));
	nbp->wp += BLEN(bp);
	cp->bp = nbp;
	trace(bp);
	cp->ifc->d->bwrite(cp->ifc->dc, bp, 0);
	xmit++;
	cp->to = 4;
	cp->retries = 3;
	xmit++;
}

static void
senddata(Conn *cp, void *data, int len)
{
	Block *bp;
	Pkt *p;
	
	bp = sethdr(cp->ifc, cp->ea, &p, len);
	memmove(p->data, data, len);
	p->len = len;
	p->seq = ++cp->sndseq;
	p->conn = cp->cno;
	p->type = Tdata;
	send(cp, bp);
}
	
static void
resend(Conn *cp)
{
	Block *nbp;
	
	rsnd++;
	nbp = allocb(BLEN(cp->bp));
	memmove(nbp->wp, cp->bp->rp, BLEN(cp->bp));
	nbp->wp += BLEN(cp->bp);
	trace(nbp);
	cp->ifc->d->bwrite(cp->ifc->dc, nbp, 0);
	cp->to = 4;
}

static void
reset(If *ifc, uchar conn)
{
	Block *bp;
	Pkt *p;
	
	bp = sethdr(ifc, ifc->ea, &p, 0);
	p->type = Treset;
	p->conn = conn;
	trace(bp);
	ifc->d->bwrite(ifc->dc, bp, 0);
}
	
static void
ack(Conn *cp)
{
	if(cp->bp)
		freeb(cp->bp);
	cp->bp = nil;
	cp->to = 0;
	cp->retries = 0;
}

static void
start(Conn *cp)
{
	char buf[250];
	int n, c;
	
	if(cp->bp != nil)
		return;
	n = 0;
	ilock(cp);
	while(n < sizeof buf){
		c = cbget(cp);
		if(c == -1)
			break;
		buf[n] = c;
		n++;
	}
	iunlock(cp);
	if(n == 0)
		return;
	senddata(cp, buf, n);
}
	
void
cecputs(char *str, int n)
{
	int i, c, ien;
	Conn *cp;
	extern int panicking;

	if(panicking || active.exiting)
		return;
	ien = islo();
	for(cp = conn; cp < &conn[Nconns]; cp++){
		ilock(cp);
		if(cp->state == Copen){
			for (i = 0; i < n; i++){
				c = str[i];
				if(c == '\n')
					cbput(cp, '\r');
				cbput(cp, c);
			}
		}
		iunlock(cp);
		if(ien){
			qlock(cp);
			start(cp);
			qunlock(cp);
		}else{
			cp->stalled = 1;
			wakeup(&trendez);
		}
	}
}

static void
conputs(Conn *c, char *s)
{
	for(; *s; s++)
		cbput(c, *s);
}

static void
cectimer(void *)
{
	Conn *cp;
	
	for(;;){
		tsleep(&trendez, return0, 0, 500);
		for(cp = conn; cp < &conn[Nconns]; cp++){
			qlock(cp);
			if(cp->bp != nil){
				if(--cp->to <= 0){
					if(--cp->retries <= 0){
						freeb(cp->bp);
						cp->bp = nil;
						cp->state = Cunused;
					}else
						resend(cp);
				}
			}else if(cp->stalled){
				cp->stalled = 0;
				start(cp);
			}
			qunlock(cp);
		}
	}
}

static void
discover(If *ifc, Pkt *p)	// tell about us
{
	Block *bp;
	Pkt *np;
	uchar *addr;

	if(p)
		addr = p->src;
	else
		addr = broadcast;
	bp = sethdr(ifc, addr, &np, 0);
	np->type = Toffer;
	np->len = snprint((char *)np->data, sizeof np->data, "%d %s", shelf, name);
	trace(bp);
	ifc->d->bwrite(ifc->dc, bp, 0);
}

static Conn *
findconn(uchar *ea, uchar cno)		// return locked connection object
{
	Conn *cp, *ncp = nil;
	
	for(cp = conn; cp < &conn[Nconns]; cp++){
		if(ncp == nil && cp->state == Cunused)
			ncp = cp;
		if(memcmp(ea, cp->ea, 6) == 0 && cno == cp->cno){
			qlock(cp);
			return cp;
		}
	}
	if(ncp != nil)
		qlock(ncp);
	return ncp;
}

static void
checkpw(Conn *cp, char *str, int len)
{
	int i, c;
	
	if(passwd[0] == 0)
		return;
	for(i = 0; i < len; i++){
		c = str[i];
		if(c != '\n' && c != '\r'){
			if(cp->pwi < (sizeof cp->passwd)-1)
				cp->passwd[cp->pwi++] = c;
			cbput(cp, '#');
			cecprint("%c", c);
			continue;
		}
		// is newline; check password
		cp->passwd[cp->pwi] = 0;
		if(strcmp(cp->passwd, passwd) == 0){
			cp->state = Copen;
			cp->pwi = 0;
			print("\r\n%E logged in\r\n", cp->ea);
		}else{
			conputs(cp, "\r\nBad password\r\npassword: ");
			cp->pwi = 0;
		}
	}
	start(cp);
}

static void
incoming(Conn *cp, If *ifc, Pkt *p)
{
	Pkt *np;
	int i;
	Block *bp;
	
	// ack it no matter what its sequence number
	bp = sethdr(ifc, p->src, &np, 0);
	np->type = Tack;
	np->seq = p->seq;
	np->conn = cp->cno;
	np->len = 0;
	trace(bp);
	ifc->d->bwrite(ifc->dc, bp, 0);
	
	if(p->seq == cp->rcvseq)
		return;

	// process message
	
	cp->rcvseq = p->seq;
	if(cp->state == Copen){
		for (i = 0; i < p->len; i++)
			kbdcr2nl(nil, (char)p->data[i]);
	}else if(cp->state == Clogin)
		checkpw(cp, (char *)p->data, p->len);
}

static void
inita(Conn *ncp, If *ifc, Pkt *p)		// connection request
{
	Pkt *np;
	Block *bp;
	
	ncp->ifc = ifc;
	ncp->state = Cinitb;
	memmove(ncp->ea, p->src, 6);
	ncp->cno = p->conn;
	bp = sethdr(ifc, p->src, &np, 0);
	np->type = Tinitb;
	np->conn = ncp->cno;
	np->len = 0;
	send(ncp, bp);
}


static void
cecrdr(void *vp)	// reader of incoming frames
{
	Block *bp;
	If *ifc;
	Pkt *p;
	Conn *cp;
	
	ifc = vp;
	if(waserror())
		goto kexit;

	discover(ifc, 0);
	for(;;){
		bp = ifc->d->bread(ifc->dc, ETHERMAXTU, 0);
		if(bp == nil)
			nexterror();
		p = (Pkt *)bp->rp;
		if(p->etype[0] != 0xbc || p->etype[1] != 0xbc){
			freeb(bp);
			continue;
		}
		trace(bp);
		cp = findconn(p->src, p->conn);
		if(cp == nil){
			cecprint("cec: out of connection structures\n");
			freeb(bp);
			continue;
		}
		if (waserror()){
			freeb(bp);
			qunlock(cp);
			continue;
		}
		switch(p->type){
		case Tinita:
			// connection request
			if(cp->bp){
				cecprint("cec: reset with bp!? ask quanstro\n");
				freeb(cp->bp);
				cp->bp = 0;
			}
			inita(cp, ifc, p);
			break;
		case Tinitb:
			cecprint("cec: unexpected initb\n");
			break;
		case Tinitc:
			if(cp->state == Cinitb){
				ack(cp);
				if(cp->passwd[0]){
					cp->state = Clogin;
					conputs(cp, "password: ");
					start(cp);
				}else
					cp->state = Copen;
			}
			break;
		case Tdata:
			// data packet arrived
			incoming(cp, ifc, p);
			break;
		case Tack:
			// ack for one I sent arrived
			if(cp->state == Clogin || cp->state == Copen){
				ack(cp);
				start(cp);
			}
			break;
		case Tdiscover:
			// someone wanting to know about us
			discover(ifc, p);
			break;
		case Toffer:
			// cecprint("cec: unexpected offer\n"); from ourselves.
			break;
		case Treset:
			if(cp->bp)
				freeb(cp->bp);
			cp->bp = 0;
			cp->state = Cunused;
			break;
		default:
			cecprint("bad cec type: %d\n", p->type);
			break;
		}
		nexterror();
	}

kexit:
	for(cp = conn; cp < conn + nelem(conn); cp++)
		if(cp->ifc == ifc){
			if(cp->bp)
				freeb(cp->bp);
			memset(cp, 0, sizeof *cp);
			break;
		}

	memset(ifc, 0, sizeof *ifc);
	pexit("cec exiting", 1);
}

static Chan *
cecattach(char *spec)
{
	Chan *c;
	static QLock q;
	static int inited;

	qlock(&q);
	if(inited == 0){
		kproc("cectimer", cectimer, nil);
		inited++;
	}
	qunlock(&q);
	c = devattach(L'©', spec);
	c->qid.path = Qdir;
	return c;
}

static Walkqid*
cecwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, cecdir, nelem(cecdir), devgen);
}

static int
cecstat(Chan *c, uchar *dp, int n)
{
	return devstat(c, dp, n, cecdir, nelem(cecdir), devgen);
}

static Chan *
cecopen(Chan *c, int omode)
{
	return devopen(c, omode, cecdir, nelem(cecdir), devgen);
}

static void
cecclose(Chan *)
{
}

static char * cstate[] = { "unused", "initb", "login", "open" };
enum { SIZE = READSTR*2 };

static long
cecread(Chan *c, void *a, long n, vlong offset)
{
	char *p;
	Conn *cp;
	int j;
	If *ifc;

	switch((int)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, cecdir, nelem(cecdir), devgen);
	case Qstat:
		p = malloc(SIZE);
		j = 0;
		for(cp = conn; cp < conn+Nconns; cp++)
			if(cp->state != Cunused)
			j += snprint(p+j, SIZE-j, 
				"%E %3d %-6s %12d %d %d %08ulx\n",
				cp->ea, cp->cno, cstate[cp->state], cp->idle,
				cp->to, cp->retries, cp->bp);
		n = readstr(offset, a, n, p);
		free(p);
		return n;
	case Qdbg:
		cecprint("xmit %d, rsnd %d\n", xmit, rsnd);
		return 0;
	case Qcfg:
		p = mallocz(SIZE, 1);
		j = 0;
		for(ifc=ifs; ifc < ifs + nelem(ifs); ifc++)
			if(ifc->d)
				j += snprint(p+j, SIZE-j, "%s\n", ifc->path);
		n = readstr(offset, a, n, p);
		free(p);
		return n;
	}
	error(Egreg);
	return 0;
}
	
static void
cecon(char *path)
{
	Chan *dc, *cc;
	uchar ea[6];
	char buf[64];
	If *ifc, *nifc = nil;

	for(ifc=ifs; ifc < ifs + nelem(ifs); ifc++)
		if(ifc->d == nil)
			nifc = ifc;
		else if(strcmp(ifc->path, path) == 0)
			return;
	ifc = nifc;
	if(ifc == nil)
		error("out of interface structures");

	getaddr(path, ea);
	snprint(buf, sizeof buf, "%s!0xbcbc", path);
	dc = chandial(buf, nil, nil, &cc);
	if(dc == nil || cc == nil){
		if (cc)
			cclose(cc);
		if (dc)
			cclose(dc);
		snprint(up->genbuf, nelem(up->genbuf), "can't dial %s", buf);
		error(up->genbuf);
	}
	ifc->d = devtab[cc->type];
	ifc->cc = cc;
	ifc->dc = dc;
	strncpy(ifc->path, path, nelem(ifc->path));
	memmove(ifc->ea, ea, 6);
	snprint(up->genbuf, nelem(up->genbuf), "cec@%s\n", path);
	kproc(up->genbuf, cecrdr, ifc);
}

static void
cecoff(char *path)
{
	If *ifc, *e;

	ifc = ifs;
	e = ifc+nelem(ifs);
	for(; ifc < e; ifc++)
		if(ifc->d && strcmp(path, ifc->path) == 0)
			break;
	if(ifc == e)
		error("cec not found");
	cclose(ifc->cc);
	cclose(ifc->dc);
}

static long
cecwrite(Chan *c, void *a, long n, vlong )
{
	Cmdbuf *cb;
	Cmdtab *cp;
	
	if(c->qid.path == Qctl){
		cb = parsecmd(a, n);
		if(waserror()){
			free(cb);
			nexterror();
		}
		cp = lookupcmd(cb, ceccmd, nelem(ceccmd));
		switch(cp->index){
		case CMsetname:
			strecpy(name, name+(sizeof name - 1), cb->f[1]);
			break;
		case CMtraceon:
			tflag = 1;
			break;
		case CMtraceoff:
			tflag = 0;
			break;
		case CMsetpasswd:
			strcpy(passwd, cb->f[1]);
			break;
		case CMcecon:
			cecon(cb->f[1]);
			break;
		case CMcecoff:
			cecoff(cb->f[1]);
			break;
		case CMsetshelf:
			shelf = atoi(cb->f[1]);
			break;
		default:
			cmderror(cb, "bad control message");
			break;
		}
		free(cb);
		poperror();
		return n;
	}
	error(Egreg);
	return 0;
}

Dev cecdevtab = {
	L'©',
	"cethcon",

	devreset,
	devinit,
	devshutdown,
	cecattach,
	cecwalk,
	cecstat,
	cecopen,
	devcreate,
	cecclose,
	cecread,
	devbread,
	cecwrite,
	devbwrite,
	devremove,
	devwstat,
	devpower,
	devconfig,
};

static int
cecprint(char *fmt, ...)
{
	int n;
	va_list arg;
	char buf[PRINTSIZE];

	va_start(arg, fmt);
	n = vseprint(buf, buf+sizeof(buf), fmt, arg)-buf;
	va_end(arg);
	uartputs(buf, n);
	return n;
}

