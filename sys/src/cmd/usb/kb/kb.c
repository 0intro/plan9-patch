/*
 * USB Human Interaction Device: keyboard and mouse.
 *
 * If there's no usb keyboard, it tries to setup the mouse, if any.
 * It should be started at boot time.
 *
 * Mouse events are converted to the format of mouse(3)'s mousein file.
 * Keyboard keycodes are translated to scan codes and sent to kbin(3).
 *
 * If there is no keyboard, it tries to setup the mouse properly, else it falls
 * back to boot protocol.
 */

#include <u.h>
#include <libc.h>
#include <thread.h>
#include "usb.h"
#include "hid.h"

enum
{
	Stoprpt	= -2,
	Tick	= -3,
	Exiting	= -4,

	Msec	= 1000*1000,		/* msec per ns */

	Dwcidle	= 8,
};

typedef ushort Scan;
typedef struct KDev KDev;
typedef struct Kbd Kbd;
typedef struct Mouse Mouse;
typedef struct Kin Kin;

struct Kbd
{
	Channel*	repeatc;
	Channel*	exitc;
	long		nproc;
	uint		led;
};

struct Mouse
{
	int	accel;		/* only for mouse */
};

struct KDev
{
	Dev*	dev;		/* usb device*/
	Dev*	ep;		/* endpoint to get events */
	int	eid;		/* id of endpoint (not of open ep) */
	Kin*	in;		/* used to send events to kernel */
	int	idle;		/* min time between reports (× 4ms) */
	int	bootp;		/* has associated keyboard */
	int	debug;
	Kbd;
	Mouse;
	HidRepTempl templ;
	int	(*ptrvals)(KDev *kd, Chain *ch, int *px, int *py, int *pb);
};

/*
 * Kbdin and mousein files must be shared among all instances.
 */
struct Kin
{
	int	ref;
	int	fd;
	char*	name;
};

/*
 * Map for the logitech bluetooth mouse with 8 buttons and wheels.
 *	{ ptr ->mouse}
 *	{ 0x01, 0x01 },	// left
 *	{ 0x04, 0x02 },	// middle
 *	{ 0x02, 0x04 },	// right
 *	{ 0x40, 0x08 },	// up
 *	{ 0x80, 0x10 },	// down
 *	{ 0x10, 0x08 },	// side up
 *	{ 0x08, 0x10 },	// side down
 *	{ 0x20, 0x02 }, // page
 * besides wheel and regular up/down report the 4th byte as 1/-1
 */

/*
 * usb key code to ps/2 scan code; for the page table used by
 * the logitech bluetooth keyboard.
 */
#define E	SCesc1<<8

static Scan sctaben[256] ={
[0x00]	0,	0,	0,	0,	0x1e,	0x30,	0x2e,	0x20,
[0x08]	0x12,	0x21,	0x22,	0x23,	0x17,	0x24,	0x25,	0x26,
[0x10]	0x32,	0x31,	0x18,	0x19,	0x10,	0x13,	0x1f,	0x14,
[0x18]	0x16,	0x2f,	0x11,	0x2d,	0x15,	0x2c,	0x02,	0x03,
[0x20]	0x04,	0x05,	0x06,	0x07,	0x08,	0x09,	0x0a,	0x0b,
[0x28]	0x1c,	0x01,	0x0e,	0x0f,	0x39,	0x0c,	0x0d,	0x1a,
[0x30]	0x1b,	0x2b,	0x2b,	0x27,	0x28,	0x29,	0x33,	0x34,
[0x38]	0x35,	0x3a,	0x3b,	0x3c,	0x3d,	0x3e,	0x3f,	0x40,		/* /, ctl, f1-f6 */
[0x40]	0x41,	0x42,	0x43,	0x44,	0x57,	0x58,	E|0x63,	0x46,		/* f7-f10, ... */
[0x48]	E|0x77,	E|0x52,	E|0x47,	E|0x49,	E|0x53,	E|0x4f,	E|0x51,	E|0x4d,
[0x50]	E|0x4b,	E|0x50,	E|0x48,	0x45,	0x35,	0x37,	0x4a,	0x4e,
[0x58]	0x1c,	0x4f,	0x50,	0x51,	0x4b,	0x4c,	0x4d,	0x47,
[0x60]	0x48,	0x49,	0x52,	0x53,	E|0x56,	E|0x7f,	E|0x74,	E|0x75,
[0x68]	0x64,	0x65,	0x66,	0x67,	0x68,	0x69,	0x6a,	0x6b,		/* f13-f20 */
[0x70]	0x6c,	0x6d,	0x6e,	0x6f,	0,	0,	0,	0,		/* f21-f24, */
[0x78]	0,	0,	0,	0,	0,	0,	0,	E|0x71,
[0x80]	E|0x73,	E|0x72,	0,	0,	0,	E|0x7c,	0,	0,
[0x88]	0,	0,	0,	0,	0,	0,	0,	0,
[0x90]	0,	0,	0,	0,	0,	0,	0,	0,
[0x98]	0,	0,	0,	0,	0,	0,	0,	0,
[0xa0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xa8]	0,	0,	0,	0,	0,	0,	0,	0,
[0xb0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xb8]	0,	0,	0,	0,	0,	0,	0,	0,
[0xc0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xc8]	0,	0,	0,	0,	0,	0,	0,	0,
[0xd0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xd8]	0,	0,	0,	0,	0,	0,	0,	0,
[0xe0]	0x1d,	0x2a,	E|0x38,	E|0x7d,	E|0x61,	0x36,	E|0x64,	E|0x7e,
[0xe8]	0,	0,	0,	0,	0,	E|0x73,	E|0x72,	E|0x71,
[0xf0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xf8]	0,	0,	0,	0,	0,	0,	0,	0,
};

/*
 * scan codes above 0x7f are keyup + sc&0x7f.  i don't understand this.
 */
static Scan sctabjp[256] = {
[0x00]	0,	0,	0xfc,	0,	0x1e,	0x30,	0x2e,	0x20,
[0x08]	0x12,	0x21,	0x22,	0x23,	0x17,	0x24,	0x25,	0x26,
[0x10]	0x32,	0x31,	0x18,	0x19,	0x10,	0x13,	0x1f,	0x14,
[0x18]	0x16,	0x2f,	0x11,	0x2d,	0x15,	0x2c,	0x02,	0x03,
[0x20]	0x04,	0x05,	0x06,	0x07,	0x08,	0x09,	0x0a,	0x0b,
[0x28]	0x1c,	0x01,	0x0e,	0x0f,	0x39,	0x0c,	0x0d,	0x1a,
[0x30]	0x1b,	0x2b,	0x2b,	0x27,	0x28,	0x29,	0x33,	0x34,
[0x38]	0x35,	0x3a,	0x3b,	0x3c,	0x3d,	0x3e,	0x3f,	0x40,
[0x40]	0x41,	0x42,	0x43,	0x44,	0x57,	0x58,	0x37,	0x46,
[0x48]	0xc6,	E|0x52,	E|0x47,	E|0x49,	E|0x53,	E|0x4f,	E|0x51,	E|0x4d,
[0x50]	E|0x4b,	E|0x50,	E|0x48,	0x45,	0x35,	0x37,	E|0x4a,	E|0x4e,
[0x58]	0x1c,	E|0x4f,	E|0x50,	E|0x51,	E|0x4b,	E|0x4c,	E|0x4d,	E|0x47,
[0x60]	E|0x48,	E|0x49,	E|0x52,	E|0x53,	0x56,	0,	0x5e,	0x59,
[0x68]	0x64,	0x65,	E|0x66,	0x67,	0x68,	0x69,	0x6a,	0x6b,
[0x70]	0x6c,	0x6d,	0x6e,	0x76,	0,	0,	0,	0,
[0x78]	0,	0,	0,	0,	0,	0,	0,	0,
[0x80]	0,	0,	0,	0,	0,	0x7e,	0,	0x73,
[0x88]	0x70,	0x7d,	0x79,	0x7b,	0x5c,	0,	0,	0,
[0x90]	0xf2,	0xf1,	0x78,	0x77,	0x76,	0,	0,	0,
[0x98]	0,	0,	0,	0,	0,	0,	0,	0,
[0xa0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xa8]	0,	0,	0,	0,	0,	0,	0,	0,
[0xb0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xb8]	0,	0,	0,	0,	0,	0,	0,	0,
[0xc0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xc8]	0,	0,	0,	0,	0,	0,	0,	0,
[0xd0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xd8]	0,	0,	0,	0,	0,	0,	0,	0,
[0xe0]	0x1d,	0x2a,	0x38,	0x5b,	0x1d,	0x36,	0x38,	0x5c,
[0xe8]	0,	0,	0,	0,	0,	0,	0,	0,
[0xf0]	0,	0,	0,	0,	0,	0,	0,	0,
[0xf8]	0,	0,	0,	0,	0,	0,	0,	0,
};
static Scan *sctab = sctaben;

static QLock inlck;
static Kin kbdin =
{
	.ref = 0,
	.name = "#Ι/kbin",
	.fd = -1,
};
static Kin ptrin =
{
	.ref = 0,
	.name = "#m/mousein",
	.fd = -1,
};

static int ptrbootpvals(KDev *kd, Chain *ch, int *px, int *py, int *pb);
static int ptrrepvals(KDev *kd, Chain *ch, int *px, int *py, int *pb);

static int
setbootproto(KDev* f, int eid, uchar *, int)
{
	int nr, r, id;

	f->ptrvals = ptrbootpvals;
	r = Rh2d|Rclass|Riface;
	dprint(2, "setting boot protocol %d\n", eid);
	id = f->dev->usb->ep[eid]->iface->id;
	nr = usbcmd(f->dev, r, Setproto, Bootproto, id, nil, 0);
	if(nr < 0)
		return -1;
	usbcmd(f->dev, r, Setidle, f->idle<<8, id, nil, 0);
	return nr;
}

static int
setled(KDev* f, uint v)
{
	uchar led[1];
	int r, id;

	r = Rh2d|Rclass|Riface;
	led[0] = v;

	dprint(2, "led: %.2ux\n", led[0]);
	id = f->dev->usb->ep[f->eid]->iface->id;
	return usbcmd(f->dev, r, Setreport, Reportout | Ledreport, id, led, 1);
}

static uchar ignoredesc[128];

static int
setfirstconfig(KDev* f, int eid, uchar *desc, int descsz)
{
	int nr, r, id, i;

	dprint(2, "setting first config %d\n", eid);
	if(desc == nil){
		descsz = sizeof ignoredesc;
		desc = ignoredesc;
	}
	id = f->dev->usb->ep[eid]->iface->id;
	r = Rh2d | Rstd | Rdev;
	nr = usbcmd(f->dev,  r, Rsetconf, 1, 0, nil, 0);
	if(nr < 0)
		return -1;
	r = Rh2d | Rclass | Riface;
	nr = usbcmd(f->dev, r, Setidle, f->idle<<8, id, nil, 0);
	if(nr < 0)
		return -1;
	r = Rd2h | Rstd | Riface;
	nr=usbcmd(f->dev,  r, Rgetdesc, Dreport<<8, id, desc, descsz);
	if(nr <= 0)
		return -1;
	if(f->debug){
		fprint(2, "report descriptor:");
		for(i = 0; i < nr; i++){
			if(i%8 == 0)
				fprint(2, "\n\t");
			fprint(2, "%#2.2ux ", desc[i]);
		}
		fprint(2, "\n");
	}
	f->ptrvals = ptrrepvals;
	return nr;
}

/*
 * Try to recover from a babble error. A port reset is the only way out.
 * BUG: we should be careful not to reset a bundle with several devices.
 */
static void
recoverkb(KDev *f)
{
	int i;

	close(f->dev->dfd);		/* it's for usbd now */
	devctl(f->dev, "reset");
	for(i = 0; i < 10; i++){
		if(i == 5)
			f->bootp++;
		sleep(500);
		if(opendevdata(f->dev, ORDWR) >= 0){
			if(f->bootp)
				/* TODO func pointer */
				setbootproto(f, f->eid, nil, 0);
			else
				setfirstconfig(f, f->eid, nil, 0);
			break;
		}
		/* else usbd still working... */
	}
}

static void
kbfatal(KDev *kd, char *sts)
{
	Dev *dev;

	if(sts != nil)
		fprint(2, "kb: fatal: %s\n", sts);
	else
		fprint(2, "kb: exiting\n");
	if(kd->repeatc != nil){
		chanclose(kd->repeatc);
		for(; kd->nproc != 0; kd->nproc--)
			recvul(kd->exitc);
		chanfree(kd->repeatc);
		chanfree(kd->exitc);
		kd->repeatc = nil;
		kd->exitc = nil;
	}
	dev = kd->dev;
	kd->dev = nil;
	if(kd->ep != nil)
		closedev(kd->ep);
	kd->ep = nil;
	devctl(dev, "detach");
	closedev(dev);
	/*
	 * free(kd); done by closedev.
	 */
	threadexits(sts);
}

static int
scale(KDev *f, int x)
{
	int sign = 1;

	if(x < 0){
		sign = -1;
		x = -x;
	}
	switch(x){
	case 0:
	case 1:
	case 2:
	case 3:
		break;
	case 4:
		x = 6 + (f->accel>>2);
		break;
	case 5:
		x = 9 + (f->accel>>1);
		break;
	default:
		x *= MaxAcc;
		break;
	}
	return sign*x;
}

/*
 * ps2 mouse is processed mostly at interrupt time.
 * for usb we do what we can.
 */
static void
sethipri(void)
{
	char fn[30];
	int fd;

	snprint(fn, sizeof fn, "/proc/%d/ctl", getpid());
	fd = open(fn, OWRITE);
	if(fd >= 0) {
		fprint(fd, "pri 13");
		close(fd);
	}
}

static int
ptrrepvals(KDev *kd, Chain *ch, int *px, int *py, int *pb)
{
	int i, x, y, b, c;
	static char buts[] = {0x0, 0x2, 0x1};

	c = ch->e / 8;

	/* sometimes there is a report id, sometimes not */
	if(c == kd->templ.sz + 1)
		if(ch->buf[0] == kd->templ.id)
			ch->b += 8;
		else
			return -1;
	parsereport(&kd->templ, ch);

	if(kd->debug > 1)
		dumpreport(&kd->templ);
	if(c < 3)
		return -1;
	x = hidifcval(&kd->templ, KindX, 0);
	y = hidifcval(&kd->templ, KindY, 0);
	b = 0;
	for(i = 0; i<sizeof buts; i++)
		b |= (hidifcval(&kd->templ, KindButtons, i) & 1) << buts[i];
	if(c > 3 && hidifcval(&kd->templ, KindWheel, 0) > 0)	/* up */
		b |= 0x10;
	if(c > 3 && hidifcval(&kd->templ, KindWheel, 0) < 0)	/* down */
		b |= 0x08;

	*px = x;
	*py = y;
	*pb = b;
	return 0;
}

static int
ptrbootpvals(KDev *kd, Chain *ch, int *px, int *py, int *pb)
{
	int b, c;
	char x, y;
	static char maptab[] = {0x0, 0x1, 0x4, 0x5, 0x2, 0x3, 0x6, 0x7};

	c = ch->e / 8;
	if(c < 3)
		return -1;
	if(kd->templ.nifcs){
		x = hidifcval(&kd->templ, KindX, 0);
		y = hidifcval(&kd->templ, KindY, 0);
	}else{
		/* no report descriptor for boot protocol */
		x = ((signed char*)ch->buf)[1];
		y = ((signed char*)ch->buf)[2];
	}

	b = maptab[ch->buf[0] & 0x7];
	if(c > 3 && ch->buf[3] == 1)		/* up */
		b |= 0x08;
	if(c > 3 && ch->buf[3] == 0xff)		/* down */
		b |= 0x10;
	*px = x;
	*py = y;
	*pb = b;
	return 0;
}

static void
ptrwork(void* a)
{
	int hipri, mfd, nerrs, x, y, b, c, ptrfd;
	char mbuf[80];
	Chain ch;
	KDev* f = a;

	threadsetname("ptr %s", f->ep->dir);
	hipri = nerrs = 0;
	ptrfd = f->ep->dfd;
	mfd = f->in->fd;
	if(f->ep->maxpkt < 3 || f->ep->maxpkt > MaxChLen)
		kbfatal(f, "weird mouse maxpkt");
	for(;;){
		memset(ch.buf, 0, MaxChLen);
		if(f->ep == nil)
			kbfatal(f, nil);
		c = read(ptrfd, ch.buf, f->ep->maxpkt);
		assert(f->dev != nil);
		assert(f->ep != nil);
		if(c < 0){
			dprint(2, "kb: mouse: %s: read: %r\n", f->ep->dir);
			if(++nerrs < 3){
				recoverkb(f);
				continue;
			}
		}
		if(c <= 0)
			kbfatal(f, nil);
		ch.b = 0;
		ch.e = 8 * c;
		if(f->ptrvals(f, &ch, &x, &y, &b) < 0)
			continue;
		if(f->accel){
			x = scale(f, x);
			y = scale(f, y);
		}
		if(f->debug > 1)
			fprint(2, "kb: m%11d %11d %11d\n", x, y, b);
		seprint(mbuf, mbuf+sizeof(mbuf), "m%11d %11d %11d", x, y,b);
		if(write(mfd, mbuf, strlen(mbuf)) < 0)
			kbfatal(f, "mousein i/o");
		if(hipri == 0){
			sethipri();
			hipri = 1;
		}
	}
}

static void
stoprepeat(KDev *f)
{
	sendul(f->repeatc, Stoprpt);
}

static void
startrepeat(KDev *f, Scan sc)
{
	sendul(f->repeatc, sc);
}

static void
putscan(KDev *f, Scan sc)
{
	uchar s[2];

	if(f->debug > 1)
		fprint(2, "sc: %.4ux\n", sc);
	if((uchar)sc == 0)
		return;
	s[0] = sc>>8;
	s[1] = sc;
	if(sc >= 0x100)
		write(f->in->fd, s, 2);
	else
		write(f->in->fd, s+1, 1);
}

static void
repeattimerproc(void *a)
{
	Channel *c;
	KDev *f;

	threadsetname("kbd reptimer");
	f = a;
	c = f->repeatc;

	for(;;){
		sleep(30);
		if(sendul(c, Tick) == -1)
			break;
	}
	sendul(f->exitc, Exiting);
	threadexits("aborted");
}

static void
repeatproc(void* a)
{
	KDev *f;
	Channel *c;
	int code;
	uint l;
	uvlong timeout;

	threadsetname("kbd repeat");
	f = a;
	c = f->repeatc;

	timeout = 0;
	code = -1;
	for(;;){
		switch(l = recvul(c)){
		default:
			code = l;
			timeout = nsec() + 500*Msec;
			break;
		case ~0ul:
			sendul(f->exitc, Exiting);
			threadexits("aborted");
			break;
		case Stoprpt:
			code = -1;
			break;
		case Tick:
			if(code == -1 || nsec() < timeout)
				continue;
			putscan(f, code);
			timeout = nsec() + 30*Msec;
			break;
		}
	}
}

static void
putmod(KDev *f, uchar mods, uchar omods, uchar mask, Scan sc)
{
	/* BUG: Should be a single write */
	if((mods&mask) && !(omods&mask))
		putscan(f, sc);
	if(!(mods&mask) && (omods&mask))
		putscan(f, sc | Keyup);
}

static Scan
usbtosc(KDev *f, uchar usbcode)
{
	Scan sc;

	sc = sctab[usbcode];
	if((f->led & Lnum) == 0)
	if(usbcode >= 0x54 && usbcode <= 0x63)
		sc |= SCesc1<<8;
	return sc;
}

/*
 * This routine diffs the state with the last known state
 * and invents the scan codes that would have been sent
 * by a non-usb keyboard in that case. This also requires supplying
 * the extra esc1 byte as well as keyup flags.
 * The aim is to allow future addition of other keycode pages
 * for other keyboards.
 */
static Scan
putkeys(KDev *f, uchar buf[], uchar obuf[], int n, Scan dk)
{
	int i, j;
	Scan uk;

	putmod(f, buf[0], obuf[0], Mctrl, SCctrl);
	putmod(f, buf[0], obuf[0], (1<<Mlshift), SClshift);
	putmod(f, buf[0], obuf[0], (1<<Mrshift), SCrshift);
	putmod(f, buf[0], obuf[0], Mcompose, SCcompose);
	putmod(f, buf[0], obuf[0], Maltgr, 1<<8 | SCcompose);	/* i don't understand this one */

	/* Report key downs */
	for(i = 2; i < n; i++){
		for(j = 2; j < n; j++)
			if(buf[i] == obuf[j])
			 	break;
		if(j == n && buf[i] != 0){
			switch(buf[i]){
			case 0x40:
				f->debug += 2;
				break;
			case 0x41:
				f->debug = 0;
				break;
			case 0x53:
				f->led ^= Lnum;
				setled(f, f->led);
				break;
			case 0x47:
				f->led ^= Lscroll;
				setled(f, f->led);
				break;
			case 0x39:
			//	f->led ^= Lcaps;
			//	setled(f, f->led);
				break;
			case 0x70:	/* f21 */
				f->led ^= Lkana;
				setled(f, f->led);
				break;
			}
			dk = usbtosc(f, buf[i]);
			putscan(f, dk);
			startrepeat(f, dk);
		}
	}

	/* Report key ups */
	uk = 0;
	for(i = 2; i < n; i++){
		for(j = 2; j < n; j++)
			if(obuf[i] == buf[j])
				break;
		if(j == n && obuf[i] != 0){
			uk = usbtosc(f, obuf[i]);
			putscan(f, uk|Keyup);
		}
	}
	if(uk && (dk == 0 || dk == uk)){
		stoprepeat(f);
		dk = 0;
	}
	return dk;
}

static int
kbdbusy(uchar* buf, int n)
{
	int i;

	for(i = 1; i < n; i++)
		if(buf[i] == 0 || buf[i] != buf[0])
			return 0;
	return 1;
}

static void
kbdwork(void *a)
{
	int c, kbdfd, nerrs;
	uchar buf[64], lbuf[64];
	char err[ERRMAX];
	Scan dk;
	KDev *f = a;

	threadsetname("kbd %s", f->ep->dir);
	kbdfd = f->ep->dfd;

	if(f->ep->maxpkt < 3 || f->ep->maxpkt > sizeof buf)
		kbfatal(f, "weird maxpkt");

	f->exitc = chancreate(sizeof(ulong), 0);
	if(f->exitc == nil)
		kbfatal(f, "chancreate failed");
	f->repeatc = chancreate(sizeof(ulong), 0);
	if(f->repeatc == nil){
		chanfree(f->exitc);
		kbfatal(f, "chancreate failed");
	}

	f->nproc = 2;
	proccreate(repeatproc, f, Stack);
	proccreate(repeattimerproc, f, Stack);
	memset(lbuf, 0, sizeof lbuf);
	dk = nerrs = 0;
	for(;;){
		memset(buf, 0, sizeof buf);
		c = read(kbdfd, buf, f->ep->maxpkt);
		assert(f->dev != nil);
		assert(f->ep != nil);
		if(c < 0){
			rerrstr(err, sizeof(err));
			fprint(2, "kb: %s: read: %s\n", f->ep->dir, err);
			if(strstr(err, "babble") != 0 && ++nerrs < 3){
				recoverkb(f);
				continue;
			}
		}
		if(c <= 0)
			kbfatal(f, nil);
		if(c < 3)
			continue;
		if(kbdbusy(buf + 2, c - 2))
			continue;
		if(usbdebug > 2 || f->debug > 1){
			fprint(2, "kbd %c mod %.2ux: %.*lH\n", sctab==sctabjp? 'j': 'e', buf[0], c-2, buf+2);
		}
		dk = putkeys(f, buf, lbuf, f->ep->maxpkt, dk);
		memmove(lbuf, buf, c);
		nerrs = 0;
	}
}

static void
freekdev(void *a)
{
	KDev *kd;

	kd = a;
	if(kd->in != nil){
		qlock(&inlck);
		if(--kd->in->ref == 0){
			close(kd->in->fd);
			kd->in->fd = -1;
		}
		qunlock(&inlck);
	}
	dprint(2, "freekdev\n");
	free(kd);
}

static void
kbstart(Dev *d, Ep *ep, Kin *in, void (*f)(void*), KDev *kd)
{
	uchar desc[512];
	int n, res;

	qlock(&inlck);
	if(in->fd < 0){
		in->fd = open(in->name, OWRITE);
		if(in->fd < 0){
			fprint(2, "kb: %s: %r\n", in->name);
			qunlock(&inlck);
			return;
		}
	}
	in->ref++;	/* for kd->in = in */
	qunlock(&inlck);
	d->free = freekdev;
	kd->in = in;
	kd->dev = d;
	res = -1;
	kd->ep = openep(d, ep->id);
	if(kd->ep == nil){
		fprint(2, "kb: %s: workep: openep %d: %r\n", d->dir, ep->id);
		return;
	}
	kd->eid = ep->id;
	if(in == &kbdin){
		/*
		 * DWC OTG controller misses some split transaction inputs.
		 * Set nonzero idle time to return more frequent reports
		 * of keyboard state, to avoid losing key up/down events.
		 */
		n = read(d->cfd, desc, sizeof desc - 1);
		if(n > 0){
			desc[n] = 0;
			if(strstr((char*)desc, "dwcotg") != nil)
				kd->idle = Dwcidle;
		}
	}
	if(!kd->bootp)
		res= setfirstconfig(kd, ep->id, desc, sizeof desc);
	if(res > 0)
		res = parsereportdesc(&kd->templ, desc, sizeof desc);
	/* if we could not set the first config, we give up */
	if(kd->bootp || res < 0){
		kd->bootp = 1;
		if(setbootproto(kd, ep->id, nil, 0) < 0){
			fprint(2, "kb: %s: bootproto: %r\n", d->dir);
			return;
		}
	}else if(kd->debug)
		dumpreport(&kd->templ);
	if(opendevdata(kd->ep, OREAD) < 0){
		fprint(2, "kb: %s: opendevdata: %r\n", kd->ep->dir);
		closedev(kd->ep);
		kd->ep = nil;
		return;
	}

	kd->led = Lnum;
	setled(kd, kd->led);

	incref(d);
	proccreate(f, kd, Stack);
}

static int
usage(void)
{
	werrstr("usage: usb/kb [-bdjkm] [-a n] [-N nb]");
	return -1;
}

int
kbmain(Dev *d, int argc, char* argv[])
{
	int bootp, i, kena, pena, accel, devid, debug;
	Ep *ep;
	KDev *kd;
	Usbdev *ud;

	kena = pena = 1;
	bootp = 0;
	accel = 0;
	debug = 0;
	devid = d->id;
	ARGBEGIN{
	case 'a':
		accel = strtol(EARGF(usage()), nil, 0);
		break;
	case 'd':
		debug++;
		break;
	case 'k':
		kena = 1;
		pena = 0;
		break;
	case 'j':
		sctab = sctabjp;
		break;
	case 'm':
		kena = 0;
		pena = 1;
		break;
	case 'N':
		devid = atoi(EARGF(usage()));		/* ignore dev number */
		break;
	case 'b':
		bootp++;
		break;
	default:
		return usage();
	}ARGEND;
	if(argc != 0)
		return usage();
	USED(devid);
	ud = d->usb;
	d->aux = nil;
	dprint(2, "kb: main: dev %s ref %ld\n", d->dir, d->ref);

	if(kena)
		for(i = 0; i < nelem(ud->ep); i++)
			if((ep = ud->ep[i]) == nil)
				break;
			else if(ep->iface->csp == KbdCSP)
				bootp = 1;

	for(i = 0; i < nelem(ud->ep); i++){
		if((ep = ud->ep[i]) == nil)
			continue;
		if(kena && ep->type == Eintr && (ep->dir == Ein | ep->dir == Eboth) &&
		    ep->iface->csp == KbdCSP){
			kd = d->aux = emallocz(sizeof(KDev), 1);
			kd->accel = 0;
			kd->bootp = 1;
			kd->debug = debug;
			kbstart(d, ep, &kbdin, kbdwork, kd);
		}
		if(pena && ep->type == Eintr && (ep->dir == Ein | ep->dir == Eboth)  &&
		    ep->iface->csp == PtrCSP){
			kd = d->aux = emallocz(sizeof(KDev), 1);
			kd->accel = accel;
			kd->bootp = bootp;
			kd->debug = debug;
			kbstart(d, ep, &ptrin, ptrwork, kd);
		}
	}
	return 0;
}

