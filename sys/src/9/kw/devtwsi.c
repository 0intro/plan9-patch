/*
 * kirkwood two-wire serial interface (TWSI) and
 * inter-integrated circuit (I⁲C) driver
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "io.h"

enum {
	Qdir,
	Qtwsi,
};

typedef struct Kwtwsi Kwtwsi;
typedef struct Twsi Twsi;

struct Kwtwsi {				/* device registers */
	ulong	saddr;
	ulong	data;
	ulong	ctl;
	union {
		ulong	status;		/* ro */
		ulong	rate;		/* wo */
	};
	ulong	saddrext;
	ulong	reset;
};

enum {
	Twsidowrite,
	Twsidoread,

	Twsiack		= 1<<2,
	Twsiint		= 1<<3,		/* interrupt generated */
	Twsistop	= 1<<4,
	Twsistart	= 1<<5,
	Twsislaveen	= 1<<6,
	Twsiinten	= 1<<7,		/* interrupt enabled */

	/* status codes */
	SStart	= 0x08,
	SWa	= 0x18,
	SWda	= 0x28,
	SRa	= 0x40,
	SRda	= 0x50,
	SRna	= 0x58,
};

struct Twsi {
	Kwtwsi	*reg;
	QLock;
	Rendez	nextbyte;

	/* remainder is state needed to track the operation in progress */
	int	intr;
	int	done;

	uchar	*bp;			/* current ptr into buf */
	uchar	*end;

	ulong	addr;			/* device address */
	char	*error;
};

static Twsi twsi = {
	.reg	(Kwtwsi *)AddrTwsi,
};

static Dirtab twsidir[] = {
	".",	{Qdir, 0, QTDIR},	0,	DMDIR|0555,
	"twsi",	{Qtwsi},		0,	0660,
};

static char Eabsts[] = "abnormal status";

static void
twsifinish(void)
{
	twsi.done = 1;
	twsi.reg->ctl |= Twsistop;
	coherence();
}

static void
twsidoread(void)
{
	Kwtwsi *krp = twsi.reg;

	switch(krp->status){
	case SStart:
		krp->data = twsi.addr << 1 | Twsidoread;
		break;
	case SRa:
		krp->ctl |= Twsiack;
		break;
	case SRda:
		if(twsi.bp < twsi.end) {
			*twsi.bp++ = krp->data;
			krp->ctl |= Twsiack;
		} else
			krp->ctl &= ~Twsiack;
		break;
	case SRna:
		twsifinish();
		break;
	default:
		twsifinish();
		twsi.error = Eabsts;
		break;
	}
}

static void
twsidowrite(void)
{
	Kwtwsi *krp = twsi.reg;

	switch(krp->status){
	case SStart:
		krp->data = twsi.addr << 1 | Twsidowrite;
		break;
	case SWa:
	case SWda:
		if(twsi.bp < twsi.end)
			krp->data = *twsi.bp++;
		else
			twsifinish();
		break;
	default:
		twsifinish();
		twsi.error = Eabsts;
		break;
	}
}

static int
twsigotintr(void *)
{
	return twsi.intr;
}

static long
twsixfer(uchar *buf, ulong len, ulong offset, void (*op)(void))
{
	ulong off;
	char *err;
	Kwtwsi *krp = twsi.reg;

	qlock(&twsi);
	twsi.bp = buf;
	twsi.end = buf + len;

	twsi.addr = offset;
	twsi.done = twsi.intr = 0;
	twsi.error = nil;

	krp->ctl = (krp->ctl & ~Twsiint) | Twsistart;
	coherence();
	while (!twsi.done) {
		sleep(&twsi.nextbyte, twsigotintr, 0);
		twsi.intr = 0;
		(*op)();
		coherence();
	}
	twsifinish();
	err = twsi.error;
	off = twsi.bp - buf;
	twsi.bp = nil;				/* prevent accidents */
	qunlock(&twsi);

	if(err)
		error(err);
	return off;
}

static void
interrupt(Ureg *, void *)
{
	Kwtwsi *krp = twsi.reg;

	twsi.intr = 1;
	wakeup(&twsi.nextbyte);

	krp->ctl &= ~Twsiint;
	coherence();
	intrclear(Irqlo, IRQ0twsi);
}

static void
twsiinit(void)
{
	Kwtwsi *krp = twsi.reg;

	krp->ctl &= ~Twsiint;
	krp->ctl |= Twsiinten;
	coherence();
	intrenable(Irqlo, IRQ0twsi, interrupt, nil, "twsi");
}

static void
twsishutdown(void)
{
	twsi.reg->ctl &= ~Twsiinten;
	coherence();
	intrdisable(Irqlo, IRQ0twsi, interrupt, nil, "twsi");
}

static Chan*
twsiattach(char *param)
{
	return devattach(L'⁲', param);
}

static Walkqid*
twsiwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, twsidir, nelem(twsidir), devgen);
}

static int
twsistat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, twsidir, nelem(twsidir), devgen);
}

static Chan*
twsiopen(Chan *c, int omode)
{
	switch((ulong)c->qid.path){
	default:
		error(Eperm);
	case Qdir:
	case Qtwsi:
		break;
	}
	c = devopen(c, omode, twsidir, nelem(twsidir), devgen);
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
twsiclose(Chan *)
{
}

static long
twsiread(Chan *c, void *v, long n, vlong off)
{
	switch((ulong)c->qid.path){
	default:
		error(Eperm);
	case Qdir:
		return devdirread(c, v, n, twsidir, nelem(twsidir), devgen);
	case Qtwsi:
		return twsixfer(v, n, off, twsidoread);
	}
}

static long
twsiwrite(Chan *c, void *v, long n, vlong off)
{
	switch((ulong)c->qid.path){
	default:
		error(Eperm);
	case Qtwsi:
		return twsixfer(v, n, off, twsidowrite);
	}
}

Dev twsidevtab = {
	L'⁲',
	"twsi",

	devreset,
	twsiinit,
	twsishutdown,
	twsiattach,
	twsiwalk,
	twsistat,
	twsiopen,
	devcreate,
	twsiclose,
	twsiread,
	devbread,
	twsiwrite,
	devbwrite,
	devremove,
	devwstat,
};
