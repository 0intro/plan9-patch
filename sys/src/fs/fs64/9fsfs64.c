#include "all.h"
#include "mem.h"
#include "io.h"
#include "ureg.h"

#include "../pc/dosfs.h"

void	apcinit(void);
int	sdinit(void);

/*
 * setting this to zero permits the use of discs of different sizes, but
 * can make jukeinit() quite slow while the robotics work through each disc
 * twice (once per side).
 */
int FIXEDSIZE = 1;

#ifndef	DATE
#define	DATE	1094098624L
#endif

Timet	mktime		= DATE;				/* set by mkfile */
Startsb	startsb[] =
{
	"main",		2,	/* */
/*	"main",		810988,	/* discontinuity before sb @ 696262 */
	0
};

Dos dos;

static struct
{
	char	*name;
	Off	(*read)(int, void*, long);
	Devsize	(*seek)(int, Devsize);
	Off	(*write)(int, void*, long);
	int	(*part)(int, char*);
} nvrdevs[] = {
	{ "fd", floppyread,	floppyseek,	floppywrite, 0, },
	{ "hd", ataread,	ataseek,	atawrite,	setatapart, },
	{ "md", mvsataread,	mvsataseek,	mvsatawrite,	setmv50part, },
/*	{ "sd", scsiread,	scsiseek,	scsiwrite,	setscsipart, },  */
};

char *bootpart;

void
otherinit(void)
{
	int dev, i, nfd, nhd, s;
	char *p, *q, *part, *v[4], buf[sizeof(nvrfile)+16];

	kbdinit();
	printcpufreq();
	etherinit();
	scsiinit();
	apcinit();

	s = spllo();
	sdinit();
	nhd = atainit();	/* harmless to call again */
	mvsatainit();		/* harmless to call again */
	nfd = floppyinit();
	dev = 0;
	part = "disk";
	if(p = getconf("nvr")){
		strncpy(buf, p, sizeof buf-2);
		buf[sizeof buf-1] = 0;
		switch(getfields(buf, v, nelem(v), 0, "!")){
		default:
			panic("malformed nvrfile: %s\n", buf);
		case 4:
			p = v[0];
			dev = strtoul(v[1], 0, 0);
			part = v[2];
			strcpy(nvrfile, v[3]);
			break;
		case 3:
			p = v[0];
			dev = strtoul(v[1], 0, 0);
			part = "disk";
			strcpy(nvrfile, v[2]);
			break;
		}	
	} else
	if(p = getconf("bootfile")){
		strncpy(buf, p, sizeof(buf)-2);
		buf[sizeof(buf)-1] = 0;
		p = strchr(buf, '!');
		q = strrchr(buf, '!');
		if(p == 0 || q == 0 || strchr(p+1, '!') != q)
			panic("malformed bootfile: %s\n", buf);
		*p++ = 0;
		*q = 0;
		dev = strtoul(p, 0, 0);
		p = buf;
	} else
	if(nfd)
		p = "fd";
	else
	if(nhd)
		p = "hd";
	else
		p = "sd";

	print("p = [%s]; dev=%d; part=[%s]; nvrfile=[%s]\n", p, dev, part, nvrfile);
	for(i = 0; i < nelem(nvrdevs); i++){
		if(strcmp(p, nvrdevs[i].name) != 0)
			continue;
		dos.dev = dev;
		if(nvrdevs[i].part && nvrdevs[i].part(dos.dev, part) == 0)
			continue;
		dos.read = nvrdevs[i].read;
		dos.seek = nvrdevs[i].seek;
		dos.write = nvrdevs[i].write;
		bootpart = p;
		break;
	}
	if(dos.read == 0)
		panic("no device (%s) for nvram\n", p);
	if(dosinit(&dos) < 0)
		panic("can't init dos dosfs on %s\n", p);
	splx(s);
}

static int
isdawn(void *)
{
	return predawn == 0;
}

void
touser(void)
{
	int i;

#ifdef dowereallyneedthis
	/* forget about partiton set for plan9.nvr */
	for(i = 0; nvrdevs[i].name; i++)
		if(strcmp(bootpart, nvrdevs[i].name) == 0){
			if(nvrdevs[i].part)
				nvrdevs[i].part(dos.dev, "disk");
			break;
		}
 #endif
	settime(rtctime());
	boottime = time();

	/* wait for predawn to change to zero to avoid confusing print */
	sleep(&dawnrend, isdawn, 0);
	print("sysinit\n");
	sysinit();

	userinit(floppyproc, 0, "floppyproc");
	/*
	 * Ethernet i/o processes
	 */
	etherstart();


	/*
	 * read ahead processes
	 */
	userinit(rahead, 0, "rah");

	/*
	 * server processes
	 */
	for(i=0; i<conf.nserve; i++)
		userinit(serve, 0, "srv");

	/*
	 * worm "dump" copy process
	 */
	userinit(wormcopy, 0, "wcp");

	/*
	 * processes to read the console
	 */
	consserve();

	/*
	 * "sync" copy process
	 * this doesn't return.
	 */
	u->text = "scp";
	synccopy();
}

void
localconfinit(void)
{
	/* conf.nfile = 60000; */	/* from emelie */
	conf.nodump = 0;
	conf.dumpreread = 1;
	conf.firstsb = 0;	/* time- & jukebox-dependent optimisation */
	conf.recovsb = 0;	/* 971531 is 24 june 2003, before w3 died */
	conf.ripoff = 1;
	conf.nlgmsg = 1100;	/* @8576 bytes, for packets */
	conf.nsmmsg = 500;	/* @128 bytes */
}

int (*fsprotocol[])(Msgbuf*) = {
	/* 64-bit file servers can't serve 9P1 correctly: NAMELEN is too big */
	serve9p2,
	nil,
};
