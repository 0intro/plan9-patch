/*
 * cec -- simple ethernet console
 * Copyright © Coraid, Inc. 2006—2008.  All Rights Reserved.
*/
#include <u.h>
#include <libc.h>
#include <ip.h>		/* really! */
#include <ctype.h>
#include "cec.h"

enum {
	Tinita = 0,
	Tinitb,
	Tinitc,
	Tdata,
	Tack,
	Tdiscover,
	Toffer,
	Treset,
	
	Hdrsz = 18,
	Eaddrlen = 6,
};

typedef struct{
	uchar	ea[6];
	int	shelfno;
	char	*str;
}Shelf;

void 	conn(int);
void 	gettingkilled(int);
int 	pickone(void);
void 	probe(void);
void	sethdr(Pkt *, int);
int	shelfidx(void);

Shelf	*connp;
uchar	contag;
uchar	ea[6];
char	*host;
int	ntab;
char	pflag;
int	shelf = -1;
Shelf	tab[1000];
uchar	unsetea[6];
char 	esc = '';
extern 	int fd;		/* set in netopen */

void
usage(void)
{
	fprint(2, "usage: cec [-dp] [-c esc] [-e ea] [-s shelf] [-h host] interface\n");
	exits0("usage");
}

void
catch(void*, char *note)
{
	if(strcmp(note, "alarm") == 0)
		noted(NCONT);
	noted(NDFLT);
}

int
nilea(uchar *ea)
{
	return memcmp(ea, unsetea, 6) == 0;
}

void
main(int argc, char **argv)
{
	int r, n;

	ARGBEGIN{
	case 'c':
		esc = toupper(*(EARGF(usage()))) - 'A' + 1;
		if(esc < 1 || esc > 0x19)
			usage();
		break;
	case 'd':
		debug++;
		break;
	case 'e':
		if(parseether(ea, EARGF(usage())) == -1)
			usage();
		pflag = 1;
		break;
	case 'h':
		host = EARGF(usage());
		break;
	case 'p':
		pflag = 1;
		break;
	case 's':
		shelf = atoi(EARGF(usage()));
		break;
	default:
		usage();
	}ARGEND
	if(argc == 0)
		*argv = "/net/ether0";
	else if(argc != 1)
		usage();
	fmtinstall('E', eipfmt);
	r = netopen(*argv);
	if(r == -1){
		fprint(2, "cec: can't netopen %s\n", *argv);
		exits0("open");
	}
	notify(catch);
	probe();
	for(;;){
		n = 0;
		if(shelf == -1 && host == 0 && nilea(ea))
			n = pickone();
		rawon();
		conn(n);
		rawoff();
		if(pflag == 0){
			if(shelf != -1)
				exits0("shelf not found");
			if(host)
				exits0("host not found");
			if(!nilea(ea))
				exits0("ea not found");
		}else if(shelf != -1 || host || !nilea(ea))
			exits0("");
	}
}

void
timewait(int ms)
{
	alarm(ms);
}

int
didtimeout(void)
{
	char buf[ERRMAX];

	rerrstr(buf, sizeof buf);
	if(strcmp(buf, "interrupted") == 0){
		werrstr(buf, 0);
		return 1;
	}
	return 0;
}

ushort
htons(ushort h)
{
	ushort n;
	uchar *p;

	p = (uchar*)&n;
	p[0] = h >> 8;
	p[1] = h;
	return n;
}

ushort
ntohs(int h)
{
	ushort n;
	uchar *p;

	n = h;
	p = (uchar*)&n;
	return p[0] << 8 | p[1];
}

int
tcmp(void *a, void *b)
{
	Shelf *s, *t;
	int d;

	s = a;
	t = b;
	d = s->shelfno-t->shelfno;
	if(d == 0)
		d = strcmp(s->str, t->str);
	if(d == 0)
		d = memcmp(s->ea, t->ea, 6);
	return d;
}

void
probe(void)
{
	char *sh, *other;
	int n;
	Pkt q;
	Shelf *p;

top:
	ntab = 0;
	memset(q.dst, 0xff, Eaddrlen);
	memset(q.src, 0, Eaddrlen);
	q.etype = htons(Etype);
	q.type = Tdiscover;
	q.len = 0;
	q.conn = 0;
	q.seq = 0;
	netsend(&q, 60);
	timewait(Iowait);
	while((n = netget(&q, sizeof q)) >= 0){
		if((n <= 0 && didtimeout()) || ntab == nelem(tab))
			break;
		if(n < 60 || q.len == 0 || q.type != Toffer)
			continue;
		q.data[q.len] = 0;
		sh = strtok((char *)q.data, " \t");
		if(sh == nil)
			continue;
		if(!nilea(ea) && memcmp(ea, q.src, Eaddrlen))
			continue;
		if(shelf != -1 && atoi(sh) != shelf)
			continue;
		other = strtok(nil, "\x1");
		if(other == 0)
			other = "";
		if(host && strcmp(host, other))
			continue;
		p = tab + ntab++;
		memcpy(p->ea, q.src, Eaddrlen);
		p->shelfno = atoi(sh);
		p->str = other? strdup(other): "";
	}
	alarm(0);
	if(ntab == 0) {
		if(pflag)
			goto top;
		fprint(2, "none found.\n");
		exits0("none found");
	}
	qsort(tab, ntab, sizeof tab[0], tcmp);
}

void
showtable(void)
{
	int i;

	for(i = 0; i < ntab; i++)
		print("%2d   %5d %E %s\n", i, tab[i].shelfno, tab[i].ea, tab[i].str);
}

int
pickone(void)
{
	char buf[80];
	int n, i;

	for(;;){
		showtable();
		print("[#qp]: ");
		switch(n = read(0, buf, sizeof buf)){
		case 1:
			if(buf[0] == '\n')
				continue;
		case 2:
			if(buf[0] == 'p'){
				probe();
				break;
			}
			if(buf[0] == 'q')
		case 0:
		case -1:
				exits0(0);
		}
		if(isdigit(buf[0])){
			buf[n] = 0;
			i = atoi(buf);
			if(i >= 0 && i < ntab)
				break;
		}
	}
	return i;
}

void
sethdr(Pkt *pp, int type)
{
	memmove(pp->dst, connp->ea, Eaddrlen);
	memset(pp->src, 0, Eaddrlen);
	pp->etype = htons(Etype);
	pp->type = type;
	pp->len = 0;
	pp->conn = contag;
}

void
ethclose(void)
{
	static Pkt msg;

	sethdr(&msg, Treset);
	timewait(Iowait);
	netsend(&msg, 60);
	alarm(0);
	connp = 0;
}

int
ethopen(void)
{
	Pkt tpk, rpk;
	int i, n;

	contag = (getpid() >> 8) ^ (getpid() & 0xff);
	sethdr(&tpk, Tinita);
	sethdr(&rpk, 0);
	for(i = 0; i < 3 && rpk.type != Tinitb; i++){
		netsend(&tpk, 60);
		timewait(Iowait);
		n = netget(&rpk, 1000);
		alarm(0);
		if(n < 0)
			return -1;
	}
	if(rpk.type != Tinitb)
		return -1;
	sethdr(&tpk, Tinitc);
	netsend(&tpk, 60);
	return 0;
}

char
escape(void)
{
	char buf[64];

	for(;;){
		fprint(2, ">>> ");
		buf[0] = '.';
		rawoff();
		read(0, buf, sizeof buf-1);
		rawon();
		switch(buf[0]){
		case 'i':
		case 'q':
		case '.':
			return buf[0];
		}
		fprint(2, "	(q)uit, (i)nterrupt, (.)continue\n");
	}
}

/*
 * this is a bit too agressive.  it really needs to replace only \n\r with \n.
 */
static uchar crbuf[256];

void
nocrwrite(int fd, uchar *buf, int n)
{
	int i, j, c;

	j = 0;
	for(i = 0; i < n; i++){
		if((c = buf[i]) == '\r')
			continue;
		crbuf[j++] = c;
	}
	write(fd, crbuf, j);
}

int
doloop(void)
{
	int unacked, retries, set[2];
	uchar c, tseq, rseq;
	uchar ea[Eaddrlen];
	Pkt tpk, spk;
	Mux *m;

	memmove(ea, connp->ea, Eaddrlen);
	retries = 0;
	unacked = 0;
	tseq = 0;
	rseq = -1;
	set[0] = 0;
	set[1] = fd;
top:
	if((m = mux(set)) == 0)
		exits0("mux: %r");
	for(;;) switch(muxread(m, &spk)){
	case -1:
		if(unacked == 0)
			break;
		if(retries-- == 0){
			fprint(2, "Connection timed out\n");
			muxfree(m);
			return 0;
		}
		netsend(&tpk, Hdrsz+unacked);
		break;
	case 0:
		c = spk.data[0];
		if(c == esc) {
			muxfree(m);
			switch(escape()) {
			case 'q':
				tpk.len = 0;
				tpk.type = Treset;
				netsend(&tpk, 60);
				return 0;
			case '.':
				goto top;
			case 'i':
				if((m = mux(set)) == 0)
					exits0("mux: %r");
				break;
			}
		}
		sethdr(&tpk, Tdata);
		memcpy(tpk.data, spk.data, spk.len);
		tpk.len = spk.len;
		tpk.seq = ++tseq;
		unacked = spk.len;
		retries = 2;
		netsend(&tpk, Hdrsz+spk.len);
		break;
	default:
		if(memcmp(spk.src, ea, 6) != 0 || ntohs(spk.etype) != Etype)
			continue;
		if(spk.type == Toffer){
			muxfree(m);
			return 1;
		}
		if(spk.conn != contag)
			continue;
		switch(spk.type){
		case Tdata:
			if(spk.seq == rseq)
				break;
			nocrwrite(1, spk.data, spk.len);
//			write(1, spk.data, spk.len);
			memmove(spk.dst, spk.src, 6);
			memset(spk.src, 0, 6);
			spk.type = Tack;
			spk.len = 0;
			rseq = spk.seq;
			netsend(&spk, 60);
			break;
		case Tack:
			if(spk.seq == tseq)
				unacked = 0;
			break;
		case Treset:
			muxfree(m);
			return 1;
		}
	}
}

void
conn(int n)
{
	do {
		if(connp)
			ethclose();
		connp = &tab[n];
		if(ethopen() < 0){
			fprint(2, "connection failed.\n");
			return;
		}
	} while(doloop());
}

void
exits0(char *s)
{
	if(connp != nil)
		ethclose();
	rawoff();
	exits(s);
}
