#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ndb.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include <mp.h>
#include <libsec.h>

char *mtpt;
char *host;
char *file;
char *port;
char *url;
char *get;
char *user;
char *netstk;
vlong size;
int usetls;
int blocks = 32;
int debug;

void
usage(void)
{
	fprint(2, "usage: htfilefs [-9d] [-b count] [-x netstk] [-m mntpt] [-s serv] URL\n");
	exits("usage");
}

enum
{
	Qroot,
	Qfile,
};

#define PATH(type, n)		((type)|((n)<<8))
#define TYPE(path)			((int)(path) & 0xFF)
#define NUM(path)			((uint)(path)>>8)

Channel *fsreqchan;
Channel *fsreqwaitchan;
Channel *fsblockchan;
Channel *fsblockwaitchan;
ulong time0;

typedef struct Block Block;
struct Block
{
	char *d;
	vlong off;
	vlong len;
	Block *link;
};

typedef struct Client Client;
struct Client
{
	int ref;
	int nb;
	Block *brq;
	Block **ebrq;
	Block *bq;
	Block **ebq;
	Req *rq;
	Req **erq;
};

Client client;

void
queuereq(Client *c, Req *r)
{

	if(c->rq==nil)
		c->erq = &c->rq;
	*c->erq = r;
	r->aux = nil;
	c->erq = (Req**)&r->aux;
}

void
queueblock(Client *c, Block *b)
{

	if(c->brq == nil)
		c->ebrq = &c->brq;
	*c->ebrq = b;
	b->link = nil;
	c->ebrq = (Block**)&b->link;
}

void
demiseblock(Client *c)
{
	Block *b;

	if(debug)
		print("Demising block.\n");

	b = c->bq->link;
	free(c->bq);
	c->bq = b;
	c->nb--;

	return;
}

void
addblock(Client *c, Block *b)
{

	if(debug)
		print("adding: %d %d %lld %lld\n", c->nb, (int)b, b->len, b->off);
	if(c->nb == blocks)
		demiseblock(c);
	if(c->bq == nil)
		c->ebq = &c->bq;
	*c->ebq = b;
	b->link = nil;
	c->ebq = (Block**)&b->link;
	c->nb++;
}

Block*
findblock(Client *c, vlong off)
{
	Block *l;

	l = c->bq;
	while(l) {
		if(off >= l->off && off < l->off + l->len) {
			if(debug)
				print("found: %lld -> %d %lld %lld\n", off, (int)l, l->off, l->len);
			return l;
		}
		l = l->link;
	}
	return nil;
}

Req*
findreq(Client *c, Req *r)
{
	Req **l;

	l = &c->rq;
	while(*l) {
		if(*l == r) {
			*l = r->aux;
			if(*l == nil)
				c->erq = l;
			return r;
		}
		l = (Req **)&(*l)->aux;
	}
	return nil;
}

void
matchblocks(Client *c)
{
	Req *r;
	Block *b, *br;
	vlong n, m;

	if(c->rq == nil)
		return;
	br = nil;
	r = c->rq;
	b = findblock(c, r->ifcall.offset);
	if(b != nil) {
		c->rq = r->aux;

		m = r->ifcall.offset - b->off;
		n = r->ifcall.count;
		if(n >= (b->len - m)) {
			n = b->len - m;
			if(b->off + b->len < size) {
				br = emalloc9p(sizeof(Block));
				br->d = nil;
				br->off = b->off + b->len;
				if(br->off + b->len < size) {
					br->len = b->len;
				} else {
					br->len = size - br->off;
				}
			}
		}
		if(debug)
			print("Giving back: %lld %lld -> %lld %lld\n", b->off, b->len, b->off + m, n);
		memmove(r->ofcall.data, b->d + m, n);
		r->ofcall.count = n;
		respond(r, nil);
	} else {
		if(r->ifcall.offset >= size) {
			c->rq = r->aux;
			respond(r, nil);
		}
		br = emalloc9p(sizeof(Block));
		br->d = nil;
		br->off = r->ifcall.offset - (r->ifcall.offset % 65536);
		if(br->off < size - 65536) {
			br->len = 65536;
		} else {
			br->len = size - br->off;
		}
	}

	if(br != nil) {
		sendp(fsblockchan, br);
		recvp(fsblockwaitchan);
	}
}

void
hangupclient(Client *c)
{
	Req *r, *next;
	Block *b, *bnext;

	if(--c->ref)
		return;

	b = c->brq;
	while(b) {
		bnext = b->link;
		if(b->d != nil)
			free(b->d);
		free(b);
		b = bnext;
	}
	c->brq = nil;

	b = c->bq;
	while(b) {
		bnext = b->link;
		if(b->d != nil)
			free(b->d);
		free(b);
		b = bnext;
	}
	c->bq = nil;

	r = c->rq;
	while(r) {
		next = r->aux;
		respond(r, "We are out of here.");
		r = next;
	}
	c->rq = nil;

	return;
}

char *
read_line(int s)
{
	char *ret;
	int l;

	ret = nil;
	l = 0;

	while((ret = realloc(ret, ++l)) != nil && read(s, &ret[l - 1], 1) > 0 && l < 1024) {
		if(l > 1) {
			if(ret[l - 1] == '\n') {
				ret[l - 1] = '\0';
				if(ret[l - 2] == '\r')
					ret[l - 2] = '\0';
				return ret;
			}
		}
	}

	if(ret != nil)
		free(ret);
	return nil;
}

int
do_tls(int fd)
{
	TLSconn conn;
	int ret;

	ret = tlsClient(fd, &conn);
	if(ret < 0)
		sysfatal("Unable to do TLS");

	if(conn.cert != nil)
		free(conn.cert);

	return ret;
}

char *
read_httphdr(int s, vlong *size)
{
	char *ret, *stat;

	stat = nil;
	while((ret = read_line(s)) != nil && ret[0] != '\0') {
		if(stat == nil)
			stat = estrdup9p(ret);
		if(!strncmp(ret, "Content-Length: ", 16) && size != nil)
			*size = atoll(ret + 16);
		free(ret);
	}

	return stat;
}

char *
get_range(vlong min, vlong len)
{
	char *ret, *b;
	int fd, userange;

	userange = 1;
	if(len < 65536 && min == 0) {
		userange = 0;
	} else {
		min -= 65536 - len;
	}

	fd = dial(netmkaddr(host, netstk, port), 0, 0, 0);
	if(fd < 0)
		return nil;
	
	if(usetls)
		fd = do_tls(fd);

	if(userange) {
		ret = smprint("Range: bytes=%lld-%lld\r\n", min, min + 65536);
	} else {
		ret = estrdup9p("");
	}
	fprint(fd, "GET /%s HTTP/1.1\r\nHost: %s\r\nAccept-Encoding:\r\n%s\r\n",
				get, host, ret);
	free(ret);
	ret = read_httphdr(fd, nil);
	if(!strstr(ret, "206 Partial Content") && userange) {
		free(ret);
		close(fd);
		return nil;
	}
	if(!strstr(ret, "200 OK") && !userange) {
		free(ret);
		close(fd);
		return nil;
	}
	free(ret);

	ret = emalloc9p(len);
	if(readn(fd, ret, len) != len) {
		free(ret);
		close(fd);
		return nil;
	}
	close(fd);

	if(len < 65536 && userange) {
		b = emalloc9p(len);
		memmove(b, ret + (65536 - len), len);
		free(ret);
		ret = b;
	}

	return ret;
}

void
htfilereadproc(void *a)
{
	Block *p;
	Client *c;

	p = nil;
	c = a;
	while(1) {
nothingtodo:
		matchblocks(c);
		sendp(fsblockchan, p);
		p = recvp(fsblockwaitchan);
		if(p == nil) {
			sleep(1000);
			goto nothingtodo;
		}
		p->d = get_range(p->off, p->len);
		if(p->d == nil)
			sysfatal("We got a nil pointer from get_range.");
	}
}

typedef struct Tab Tab;
struct Tab
{
	char *name;
	ulong mode;
};

Tab tab[] =
{
	"/",		DMDIR|0555,
	nil,		0444,
};

static void
fillstat(Dir *d, uvlong path)
{
	Tab *t;

	memset(d, 0, sizeof(*d));
	d->uid = estrdup9p(user);
	d->gid = estrdup9p(user);
	d->qid.path = path;
	d->atime = d->mtime = time0;
	t = &tab[TYPE(path)];
	d->name = estrdup9p(t->name);
	d->length = size;
	d->qid.type = t->mode>>24;
	d->mode = t->mode;
}

static void
fsattach(Req *r)
{

	if(r->ifcall.aname && r->ifcall.aname[0]) {
		respond(r, "invalid attach specifier");
		return;
	}
	r->fid->qid.path = PATH(Qroot, 0);
	r->fid->qid.type = QTDIR;
	r->fid->qid.vers = 0;
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static void
fsstat(Req *r)
{

	fillstat(&r->d, r->fid->qid.path);
	respond(r, nil);
}

static int
rootgen(int i, Dir *d, void*)
{

	i += Qroot + 1;
	if(i <= Qfile) {
		fillstat(d, i);
		return 0;
	}
	return -1;
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	int i;
	ulong path;

	path = fid->qid.path;
	if(!(fid->qid.type & QTDIR))
		return "walk in non-directory";

	if(strcmp(name, "..") == 0) {
		switch(TYPE(path)) {
			case Qroot:
				return nil;
			default:
				return "bug in fswalk1";
		}
	}

	i = TYPE(path) + 1;
	while(i < nelem(tab)) {
		if(strcmp(name, tab[i].name) == 0) {
			qid->path = PATH(i, NUM(path));
			qid->type = tab[i].mode>>24;
			return nil;
		}
		if(tab[i].mode & DMDIR)
			break;
		i++;
	}
	return "directory entry not found";
}

vlong
get_filesize(void)
{
	vlong ret;
	int fd;
	char *b;

	fd = dial(netmkaddr(host, netstk, port), 0, 0, 0);
	if(fd < 0)
		return -1;

	if(usetls)
		fd = do_tls(fd);

	fprint(fd, "HEAD /%s HTTP/1.1\r\nHost: %s\r\nAccept-Encoding:\r\n\r\n", get, host);
	b = read_httphdr(fd, &ret);
	if(!strstr(b, "200 OK"))
		ret = -1;
	free(b);
	close(fd);

	return ret;
}

static void
fileread(Req *r, Client *c)
{

	queuereq(c, r);
	matchblocks(c);
}

static void
fsread(Req *r)
{
	char e[ERRMAX];
	ulong path;

	path = r->fid->qid.path;
	switch(TYPE(path)) {
		case Qroot:
			dirread9p(r, rootgen, nil);
			respond(r, nil);
			break;
		case Qfile:
			fileread(r, &client);
			break;
		default:
			snprint(e, sizeof e, "bug in fsread path=%lux", path);
			respond(r, e);
			break;
	}
}

static void
fsopen(Req *r)
{
	static int need[4] = { 4, 2, 6, 1 };
	ulong path;
	int n;
	Tab *t;

	path = r->fid->qid.path;
	t = &tab[TYPE(path)];
	n = need[r->ifcall.mode&3];
	if((n&t->mode) != n) {
		respond(r, "permission denied");
		return;
	}

	respond(r, nil);
}

static void
fsflush(Req *r)
{

	if(findreq(&client, r->oldreq))
		respond(r->oldreq, "interrupted");
	respond(r, nil);
}

void
fsnetproc(void *)
{
	Alt a[3];
	Req *r;
	Block *b;

	threadsetname("fsthread");

	a[0].op = CHANRCV;
	a[0].c = fsblockchan;
	a[0].v = &b;
	a[1].op = CHANRCV;
	a[1].c = fsreqchan;
	a[1].v = &r;
	a[2].op = CHANEND;

	while(1) {
		switch(alt(a)) {
			case 0:
				if(b != nil && b->d == nil) {
					queueblock(&client, b);
					sendp(fsblockwaitchan, 0);
				} else {
					if(b != nil) {
						if(b != client.brq)
							sysfatal("Did not ask the first queue entry?");
						addblock(&client, b);
						client.brq = client.brq->link;
					}
					sendp(fsblockwaitchan, client.brq);
				}
				break;
			case 1:
				switch(r->ifcall.type) {
					case Tattach:
						fsattach(r);
						break;
					case Topen:
						fsopen(r);
						break;
					case Tstat:
						fsstat(r);
						break;
					case Tflush:
						fsflush(r);
						break;
					default:
						respond(r, "bug in fsthread");
						break;
				}
				sendp(fsreqwaitchan, 0);
				break;
		}
	}
}

static void
fssend(Req *r)
{

	sendp(fsreqchan, r);
	recvp(fsreqwaitchan);
}

static void
fsdestroyfid(Fid *)
{

	return;
}

void
takedown(Srv *)
{

	hangupclient(&client);
	threadexitsall("done");
}

Srv fs = 
{
.attach=		fssend,
.destroyfid=	fsdestroyfid,
.walk1=		fswalk1,
.open=		fssend,
.read=		fsread,
.stat=		fssend,
.flush=		fssend,
.end=		takedown,
};

void
threadmain(int argc, char **argv)
{
	char *srvname, *mntpt;

	srvname = "htfile";
	mntpt = nil;

	ARGBEGIN {
		case 'D':
			chatty9p++;
			break;
		case 'd':
			debug++;
			break;
		case 's':
			srvname = EARGF(usage());
			break;
		case 'm':
			mntpt = EARGF(usage());
			break;
		case 'b':
			blocks = atoi(EARGF(usage()));
			break;
		case 'x':
			netstk = EARGF(usage());
			break;
		default:
			usage();
	} ARGEND;

	if(argc < 1)
		usage();
	if(blocks <= 0)
		blocks = 32;

	time0 = time(0);
	host = url = estrdup9p(argv[0]);

	if(!strncmp(url, "https://", 8)) {
		host += 8;
		usetls = 1;
	} else if(!strncmp(url, "http://", 7)) {
		host += 7;
	} else {
		free(url);
		sysfatal("Only http and https are supported or bad syntax.");
	}
	port = strchr(host, ':');
	get = strchr(host, '/');
	if(get != nil)
		*get++ = '\0';
	if(port != nil && get != nil) {
		if(port < get) {
			*port++ = '\0';
			port = estrdup9p(port);
		} else {
			port = estrdup9p(usetls ? "443" : "80");
		}
	}
	if(port == nil && get != nil)
		port = estrdup9p(usetls ? "443" : "80");
	if(port != nil && get == nil) {
		*port++ = '\0';
		port = estrdup9p(port);
		get = estrdup9p("/");
	}
	if(port == nil && get == nil) {
		port = estrdup9p(usetls ? "443" : "80");
		get = estrdup9p("/");
	}

	file = strrchr(get, '/');
	if(file != nil) {
		file++;
	} else {
		if(strcmp(get, "/")) {
			file = get;
		} else {
			file = "index";
		}
	}

	tab[Qfile].name = file;
	user = getuser();
	size = get_filesize();
	if(size < 0)
		sysfatal("Could not get the filesize: %r");

	fsreqchan = chancreate(sizeof(Req *), 0);
	fsreqwaitchan = chancreate(sizeof(void *), 0);
	fsblockchan = chancreate(sizeof(Block *), 0);
	fsblockwaitchan = chancreate(sizeof(Block *), 0);

	procrfork(fsnetproc, nil, 8192, RFNAMEG | RFNOTEG);
	procrfork(htfilereadproc, &client, 8192, RFNAMEG | RFNOTEG);

	threadpostmountsrv(&fs, srvname, mntpt, MREPL);
	exits(0);
}

