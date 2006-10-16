#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>

typedef struct Proc Proc;
struct Proc {
	char pid[32];
	char name[32];
	char user[32];
	long cpri;
	long bpri;
	long promil;
	long usr;
	long sys;
	long mem;
	long pusr;
	long psys;
	long pmem;
};

enum { 
	Mcpu,
	Mmem,
	Muser,
	Mprio,
	Mexit, 
};

char *menustr[] = {
	"cpu",
	"mem",
	"user",
	"prio",
	"exit", 
	nil 
};
Menu menu = { 
	menustr, 
	nil, 
	-1 
};

int (*cmp)(void *, void *);

Image *normal;
Image *title;

int procallo;
int nproc, sleeptime = 5000;
int sort = Mcpu;
Proc *procs;

void updater(int fd);

void
usage(void)
{
	fprint(2, "usage: %s [-f font]\n", argv0);
	exits("usage");
}

int
cpucmp(void *aptr, void *bptr)
{
	Proc *a, *b;
	a = aptr;
	b = bptr;
	return b->promil - a->promil;
}

int
memcmpr(void *aptr, void *bptr)
{
	Proc *a, *b;
	a = aptr;
	b = bptr;
	return b->mem - a->mem;
}

int
usercmp(void *aptr, void *bptr)
{
	Proc *a, *b;
	a = aptr;
	b = bptr;
	return strcmp(a->user, b->user);
}

int
priocmp(void *aptr, void *bptr)
{
	Proc *a, *b;
	a = aptr;
	b = bptr;
	return b->cpri - a->cpri;
}

void
main(int argc, char **argv)
{
	Event e;
	char path[64];
	char *font = nil;
	int kupd, k, p[2], pid, fd;
	
	ARGBEGIN{
	case 'f':
		font = EARGF(usage());
		break;
	case 'T':
		sleeptime = atoi(EARGF(usage()));
		if(sleeptime <= 0)
			sysfatal("bad sleep time: %d\n", sleeptime);
		break;
	default:
		usage();
	}ARGEND

	if(font == nil)
		font = getenv("font");

	if(initdraw(nil, font, "top") == -1)
		sysfatal("initdraw");

	normal = allocimagemix(display, DPalebluegreen, DWhite);
	title = allocimagemix(display, DPalegreyblue, DWhite);

	if(normal == nil || title == nil)
		sysfatal("allocimagemix");

	pipe(p);
	pid = rfork(RFPROC|RFMEM);
	if(!pid)
		updater(p[0]);

	cmp = cpucmp;

	einit(Ekeyboard|Emouse);
	kupd = estart(0, p[1], 1);
	eresized(0);
	for(;;)
	switch(k = event(&e)){
	default:
		if(k == kupd){
			eresized(0);
			write(p[1], &k, 1); /* release */
		}
		break;
	case Ekeyboard:
		switch(e.kbdc){
		default:
			break;
		case 0x7f:
		case 'q':
			goto casequit;
		}
		break;
	case Emouse:
		if(e.mouse.buttons & 4)
		switch(emenuhit(3, &e.mouse, &menu)){
		default:
			break;
		case Mcpu:
			cmp = cpucmp;
			break;
		case Mmem:
			cmp = memcmpr;
			break;
		case Muser:
			cmp = usercmp;
			break;
		case Mprio:
			cmp = priocmp;
			break;
		case Mexit:
		casequit:
			snprint(path, sizeof path, "/proc/%d/note", pid);
			fd = open(path, OWRITE);
			write(fd, "die", 3);
			close(fd);
			exits(0);
		}
	}
}

long 
msec(void)
{
	vlong ms;
	ms = nsec();
	ms /= 1000000LL;
	return (long)ms;
}

void
updater(int syncfd)
{
	static char buf[256];
	long prev, now, elaps;

	prev = msec();
	for(;;){
		int fd, proci;
		long i, j, n, ndir;
		Dir *dir;
		fd = open("/proc", OREAD);
		ndir = dirreadall(fd, &dir);
		close(fd);
		chdir("/proc");
		if(procallo < ndir){
			procallo = ndir;
			/*
			 * allocate for worst case: all old processes died and as many
			 * new ones were created
			 */
			procs = realloc(procs, 2 * sizeof(Proc) * procallo);
		}
		now = msec();
		elaps = now - prev;
		if(elaps == 0)
			elaps = 1;
		proci = 0;

		for(i = 0; i < nproc; i++)
			*procs[i].name = 0;
		for(i = 0; i < ndir; i++){
			char *tok[12];
			if(*dir[i].name < '0' || *dir[i].name > '9')
				continue;

			chdir(dir[i].name);
			fd = open("status", OREAD);
			if(fd == -1)
				continue;
			n = read(fd, buf, sizeof buf - 1);
			close(fd);
			chdir("..");

			buf[n] = 0;
			tokenize(buf, tok, nelem(tok));
			for(j = 0; j < nproc; j++) {
				if(!strcmp(procs[j].pid, dir[i].name)){
					long dif;
					strncpy(procs[j].user, tok[1], 31);
					procs[j].user[31] = '\0';
					procs[j].pusr = procs[j].usr;
					procs[j].psys = procs[j].sys;
					procs[j].pmem = procs[j].mem;
					procs[j].usr = atol(tok[3]) + atol(tok[6]);
					procs[j].sys = atol(tok[4]) + atol(tok[7]);
					procs[j].mem = atol(tok[9]);
					procs[j].bpri = atol(tok[10]);
					procs[j].cpri = atol(tok[11]);
					strncpy(procs[j].name, tok[0], 32);
					dif = procs[j].usr + procs[j].sys;
					dif -= procs[j].pusr + procs[j].psys;
					procs[j].promil = (dif * 1000) / elaps;
					break;
				}
			}
			if(j == nproc){
				strncpy(procs[nproc].pid, dir[i].name, 32);
				strncpy(procs[nproc].name, tok[0], 32);
				strncpy(procs[j].user, tok[1], 31);
				procs[j].user[31] = '\0';
				procs[nproc].promil = 0;
				procs[nproc].usr = atol(tok[3]) + atol(tok[6]);
				procs[nproc].sys = atol(tok[4]) + atol(tok[7]);
				procs[nproc].mem = atol(tok[9]);
				procs[nproc].pusr = atol(tok[3]) + atol(tok[6]);
				procs[nproc].psys = atol(tok[4]) + atol(tok[7]);
				procs[nproc].pmem = atol(tok[9]);
				procs[nproc].bpri = atol(tok[10]);
				procs[nproc].cpri = atol(tok[11]);
				nproc++;
			}
			proci++;
		}
		j = 0;
		for(i = 0; i < nproc; i++)
			if(*procs[i].name != 0)
				memmove(&procs[j++], &procs[i], sizeof procs[0]);
		nproc = j;

		qsort(procs, nproc, sizeof(Proc), cmp);
		nproc = proci;
		free(dir);
		write(syncfd, buf, 1);
		read(syncfd, buf, 1);
		prev = now;
		sleep(sleeptime);
	}
}

void
eresized(int new)
{
	Point p, dp;
	int i, wid, namewid, userwid, memwid, difwid, priwid;
	long t;
	char str[256];
	Image *bnc;

	if(new)
	if(getwindow(display, Refmesg) == -1)
		sysfatal("can't reattach window");

	priwid = namewid = userwid = memwid = difwid = 0;
	p = screen->r.min;
	for(i = 0; i < nproc; i++){
		wid = stringwidth(font, procs[i].name);
		if(wid > namewid) 
			namewid = wid;

		wid = stringwidth(font, procs[i].user);
		if(wid > userwid) 
			userwid = wid;

		t = procs[i].promil;
		snprint(str, sizeof str, "%ld.%01ld%%", t / 10, t % 10);
		wid = stringwidth(font, str);
		if(wid > difwid) 
			difwid = wid;

		t = procs[i].mem;
		snprint(str, sizeof str, "%ld.%01ldM", t / 1000, (t % 1000) / 100);
		wid = stringwidth(font, str);
		if(wid > memwid) 
			memwid = wid;

		t = procs[i].cpri;
		snprint(str, sizeof str, "%ld", t);
		wid = stringwidth(font, str);
		if(wid > priwid) 
			priwid = wid;
	}
	wid = stringwidth(font, "prio");
	if(priwid < wid)
		priwid = wid;
	priwid += 10;
	namewid += 10;
	userwid += 10;
	memwid += 10;
	difwid += 10;

	bnc = allocimage(display, Rect(0,0, Dx(screen->r), font->height), screen->chan, 0, DOpaque);
	dp = screen->r.min;
	p = bnc->r.min;
	p.x += 10;
	draw(bnc, bnc->r, title, nil, ZP);
	string(bnc, p, display->black, ZP, font, "name");
	p.x += namewid;

	string(bnc, p, display->black, ZP, font, "user");
	p.x += userwid;


	wid = stringwidth(font, "%cpu");
	p.x += difwid - wid;
	string(bnc, p, display->black, ZP, font, "%cpu");
	p.x += wid;

	wid = stringwidth(font, "mem");
	p.x += memwid - wid;
	string(bnc, p, display->black, ZP, font, "mem");
	p.x += wid;

	wid = stringwidth(font, "prio");
	p.x += priwid - wid;
	string(bnc, p, display->black, ZP, font, "prio");
	p.x += wid;

	border(bnc, bnc->r, 1, display->black, ZP);
	draw(screen, Rpt(dp, addpt(dp, Pt(Dx(bnc->r), font->height))), bnc, nil, ZP);
	dp.y += Dy(bnc->r) + 2;

	for(i = 0; dp.y < screen->r.max.y && i < nproc; i++){
		p = bnc->r.min;
		p.x += 10;
		draw(bnc, bnc->r, normal, nil, ZP);
		string(bnc, p, display->black, ZP, font, procs[i].name);
		p.x += namewid;

		string(bnc, p, display->black, ZP, font, procs[i].user);
		p.x += userwid;

		t = procs[i].promil;
		snprint(str, sizeof str, "%ld.%01ld%%", t / 10, t % 10);
		wid = stringwidth(font, str);
		p.x += difwid - wid;
		string(bnc, p, display->black, ZP, font, str);
		p.x += wid;

		t = procs[i].mem;
		snprint(str, sizeof str, "%ld.%01ldM", t / 1000, (t % 1000) / 100);
		wid = stringwidth(font, str);
		p.x += memwid - wid;
		string(bnc, p, display->black, ZP, font, str);
		p.x += wid;

		t = procs[i].cpri;
		snprint(str, sizeof str, "%ld", t);
		wid = stringwidth(font, str);
		p.x += priwid - wid;
		string(bnc, p, display->black, ZP, font, str);
		p.x += wid;

		border(bnc, bnc->r, 1, display->black, ZP);

		draw(screen, Rpt(dp, addpt(dp, Pt(Dx(bnc->r), font->height))), bnc, nil, ZP);

		dp.y += Dy(bnc->r) + 2;
	}
	if(dp.y < screen->r.max.y)
		draw(screen, Rpt(dp, screen->r.max), display->white, nil, ZP);
	freeimage(bnc);
}
