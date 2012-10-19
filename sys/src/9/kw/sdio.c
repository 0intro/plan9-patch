#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/sd.h"

/*
 * kirkwood SDIO / SDMem / MMC host interface
 */

enum {
	Clkfreq		= 100000000,	/* external clk frequency */
	Initfreq	= 400000,		/* initialisation frequency for MMC */
	SDfreq		= 25000000,		/* standard SD frequency */
	PIOread		= 0,			/* use programmed i/o (not dma) for reading */
	PIOwrite	= 0,			/* use programmed i/o (not dma) writing */
	Polldone	= 0,			/* poll for Datadone status, don't use interrupt */
	Pollread	= 1,			/* poll for reading blocks */
	Pollwrite	= 1,			/* poll for writing blocks */

	MMCSelect	= 7,			/* mmc/sd card select command */
	Setbuswidth	= 6,			/* mmc/sd set bus width command */
};

enum {
/* Controller registers */
	DmaLSB			= 0x00>>2,
	DmaMSB			= 0x04>>2,
	Blksize			= 0x08>>2,
	Blkcount		= 0x0c>>2,
	ArgLSB			= 0x10>>2,
	ArgMSB			= 0x14>>2,
	Tm				= 0x18>>2,
	Cmd				= 0x1c>>2,
	Resp0			= 0x20>>2,
	Resp1			= 0x24>>2,
	Resp2			= 0x28>>2,
	Resp3			= 0x2c>>2,
	Resp4			= 0x30>>2,
	Resp5			= 0x34>>2,
	Resp6			= 0x38>>2,
	Resp7			= 0x3c>>2,
	Data			= 0x40>>2,
	Hoststat		= 0x48>>2,
	Hostctl			= 0x50>>2,
	Clockctl		= 0x58>>2,
	Softreset		= 0x5C>>2,
	Interrupt		= 0x60>>2,
	ErrIntr			= 0x64>>2,
	Irptmask		= 0x68>>2,
	ErrIrptmask		= 0x6C>>2,
	Irpten			= 0x70>>2,
	ErrIrpten		= 0x74>>2,
	Mbuslo			= 0x100>>2,
	Mbushi			= 0x104>>2,
	Win0ctl			= 0x108>>2,
	Win0base		= 0x10c>>2,
	Win1ctl			= 0x110>>2,
	Win1base		= 0x114>>2,
	Win2ctl			= 0x118>>2,
	Win2base		= 0x11c>>2,
	Win3ctl			= 0x120>>2,
	Win3base		= 0x124>>2,
	Clockdiv		= 0x128>>2,

/* Hostctl */
	Timeouten		= 1<<15,
	Datatoshift		= 11,
	Datatomask		= 0x7800,
	Hispeed			= 1<<10,
	Dwidth4			= 1<<9,
	Dwidth1			= 0<<9,
	Bigendian		= 1<<3,
	LSBfirst		= 1<<4,
	Cardtypemask	= 3<<1,
	Cardtypemem		= 0<<1,
	Cardtypeio		= 1<<1,
	Cardtypeiomem	= 2<<1,
	Cardtypsdio		= 3<<1,
	Pushpullen		= 1<<0,

/* Clockctl */
	Sdclken			= 1<<0,

/* Softreset */
	Swreset			= 1<<8,

/* Cmd */
	Indexshift		= 8,
	Isdata			= 1<<5,
	Ixchken			= 1<<4,
	Crcchken		= 3<<2,
	Respmask		= 3<<0,
	Respnone		= 0<<0,
	Resp136			= 1<<0,
	Resp48			= 2<<0,
	Resp48busy		= 3<<0,

/* Tm */
	Hostdma			= 0<<6,
	Hostpio			= 1<<6,
	Stopclken		= 1<<5,
	Host2card		= 0<<4,
	Card2host		= 1<<4,
	Autocmd12		= 1<<2,
	Hwwrdata		= 1<<1,
	Swwrdata		= 1<<0,

/* ErrIntr */
	Crcstaterr		= 1<<14,
	crcstartbiterr	= 1<<13,
	Crcendbiterr	= 1<<12,
	Resptbiterr		= 1<<11,
	Xfersizeerr		= 1<<10,
	Cmdstarterr		= 1<<9,
	Acmderr			= 1<<8,
	Denderr			= 1<<6,
	Dcrcerr			= 1<<5,
	Dtoerr			= 1<<4,
	Cbaderr			= 1<<3,
	Cenderr			= 1<<2,
	Ccrcerr			= 1<<1,
	Ctoerr			= 1<<0,

/* Interrupt */
	Err			= 1<<15,
	Write8ready	= 1<<11,
	Read8wready	= 1<<10,
	Cardintr	= 1<<8,
	Readrdy		= 1<<5,
	Writerdy	= 1<<4,
	Dmadone		= 1<<3,
	Blockgap	= 1<<2,
	Datadone	= 1<<1,
	Cmddone		= 1<<0,

/* Hoststat */
	Fifoempty	= 1<<13,
	Fifofull	= 1<<12,
	Rxactive	= 1<<9,
	Txactive	= 1<<8,
	Cardbusy	= 1<<1,
	Cmdinhibit	= 1<<0,
};

#define TM(bits)		((bits)<<16)
#define	GETTM(bits)		(((bits)>>16)&0xFFFF)
#define GETCMD(bits)	((bits)&0xFFFF)

int cmdinfo[64] = {
[0]  Ixchken,
[2]  Resp136,
[3]  Resp48 | Ixchken | Crcchken,
[6]  Resp48 | Ixchken | Crcchken,
[7]  Resp48busy | Ixchken | Crcchken,
[8]  Resp48 | Ixchken | Crcchken,
[9]  Resp136,
[12] Resp48busy | Ixchken | Crcchken,
[13] Resp48 | Ixchken | Crcchken,
[16] Resp48,
[17] Resp48 | Isdata | TM(Card2host) | Ixchken | Crcchken,
[18] Resp48 | Isdata | TM(Card2host) | Ixchken | Crcchken,
[24] Resp48 | Isdata | TM(Host2card | Hwwrdata) | Ixchken | Crcchken,
[25] Resp48 | Isdata | TM(Host2card | Hwwrdata) | Ixchken | Crcchken,
[41] Resp48,
[55] Resp48 | Ixchken | Crcchken,
};

typedef struct Ctlr Ctlr;

struct Ctlr {
	Rendez	r;
	int		datadone;
	int		fastclock;
};

static Ctlr ctlr;

static void sdiointerrupt(Ureg*, void*);

void
WR(int reg, u32int val)
{
	u32int *r = (u32int*)AddrSdio;

	val &= 0xFFFF;
	if(0)print("WR %2.2ux %ux\n", reg<<2, val);
	r[reg] = val;
}

static uint
clkdiv(uint d)
{
	assert(d < 1<<11);
	return d;
}

static int
datadone(void*)
{
	return ctlr.datadone;
}

static int
sdioinit(void)
{
	u32int *r;

	r = (u32int*)AddrSdio;
	WR(Softreset, Swreset);
	while(r[Softreset]&Swreset)
		;
	delay(10);
	return 0;
}

static int
sdioinquiry(char *inquiry, int inqlen)
{
	return snprint(inquiry, inqlen, "SDIO Host Controller");
}

static void
sdioenable(void)
{
	u32int *r;
	r = (u32int*)AddrSdio;

	WR(Clockdiv, clkdiv(Clkfreq/Initfreq-1));
	delay(10);
	WR(Clockctl, r[Clockctl]&~Sdclken);
	WR(Hostctl, Pushpullen|Bigendian|Cardtypemem);
	WR(Irpten, 0);
	WR(Interrupt, ~0);
	WR(ErrIntr, ~0);
	WR(Irptmask, ~0);
	WR(ErrIrptmask, ~Dtoerr);
	intrenable(Irqlo, IRQ0sdio,sdiointerrupt, &ctlr, "sdio");
}

static int
sdiocmd(u32int cmd, u32int arg, u32int *resp)
{
	u32int *r;
	u32int c;
	int i, err;
	ulong now;

	r = (u32int*)AddrSdio;
	assert(cmd < nelem(cmdinfo) && cmdinfo[cmd] != 0);
	i = GETTM(cmdinfo[cmd]);
	c = (cmd<<Indexshift) | GETCMD(cmdinfo[cmd]);
	if(c&Isdata){
		if(i&Card2host)
			i |= PIOread? Hostpio : Hostdma;
		else
			i |= PIOwrite? Hostpio : Hostdma;
	}
	WR(Tm, i);
	WR(ArgLSB, arg);
	WR(ArgMSB, arg>>16);
	WR(ErrIntr, ~0);
	WR(Cmd, c);
	now = m->ticks;
	while(((i=r[Interrupt])&(Cmddone|Err)) == 0)
		if(m->ticks-now > HZ)
			break;
	if((i&(Cmddone|Err)) != Cmddone){
		if((err = r[ErrIntr]) != Ctoerr)
			print("sdio: cmd %ux error intr %ux %ux stat %ux\n", c, i, err,
				r[Hoststat]);
		WR(ErrIntr, err);
		WR(Interrupt, i);
		error(Eio);
	}
	WR(Interrupt, i & ~Datadone);
	switch(c&Respmask){
	case Resp136:
		resp[0] = r[Resp7]<<8  | r[Resp6]<<22;
		resp[1] = r[Resp6]>>10 | r[Resp5]<<6 | r[Resp4]<<22;
		resp[2] = r[Resp4]>>10 | r[Resp3]<<6 | r[Resp2]<<22;
		resp[3] = r[Resp2]>>10 | r[Resp1]<<6 | r[Resp0]<<22;
		break;
	case Resp48:
	case Resp48busy:
		resp[0] = r[Resp2] | r[Resp1]<<6 | r[Resp0]<<22;
		break;
	case Respnone:
		resp[0] = 0;
	}
	if((c&Respmask) == Resp48busy){
		if(Polldone){
			now = m->ticks;
			while(((i=r[Interrupt])&(Datadone|Err)) == 0)
				if(m->ticks-now > 3*HZ)
					break;
		}else{
			WR(Irpten, Datadone|Err);
			tsleep(&ctlr.r, datadone, 0, 3000);
			i = ctlr.datadone;
			ctlr.datadone = 0;
			WR(Irpten, 0);
		}
		if((i&Datadone) == 0)
			print("sdioio: no Datadone after CMD%d\n", cmd);
		if(i&Err)
			print("sdioio: CMD%d error interrupt %ux %ux\n", cmd, r[Interrupt], r[ErrIntr]);
		WR(Interrupt, i);
	}
	/*
	 * Once card is selected, use faster clock
	 */
	if(cmd == MMCSelect){
		delay(10);
		WR(Clockdiv, clkdiv(Clkfreq/SDfreq-1));
		delay(10);
		ctlr.fastclock = 1;
	}
	/*
	 * If card bus width changes, change host bus width
	 */
	if(cmd == Setbuswidth){
		switch(arg){
			case 0:
				WR(Hostctl, r[Hostctl]&~Dwidth4);
				break;
			case 2:
				WR(Hostctl, r[Hostctl]|Dwidth4);
				break;
		}
	}
	return 0;
}

static void
sdioiosetup(int write, void *buf, int bsize, int bcount)
{
	uintptr pa;
	int len;

	pa = PADDR(buf);
	if(write && !PIOwrite){
		WR(DmaLSB, pa);
		WR(DmaMSB, pa>>16);
		len = bsize*bcount;
		cachedwbse(buf, len);
		l2cacheuwbse(buf, len);
	}else if(!write && !PIOread){
		WR(DmaLSB, pa);
		WR(DmaMSB, pa>>16);
		len = bsize*bcount;
		cachedwbinvse(buf, len);
		l2cacheuwbinvse(buf, len);
	}
	WR(Blksize, bsize);
	WR(Blkcount, bcount);
}

static void
sdioio(int write, uchar *buf, int len)
{
	u32int *r;
	int i, err, d, now;

	r = (u32int*)AddrSdio;
	assert((len&3) == 0);
	if(write && PIOwrite){
		while(len > 0){
			if(Pollwrite){
				now = m->ticks;
				while(((i = r[Interrupt])&(Writerdy|Err)) == 0)
					if(m->ticks-now > 8*HZ){
						print("sdioio: (%d) no Writerdy intr %ux stat %ux\n", len, i, r[Hoststat]);
						error(Eio);
					}
			}else{
				if(((i = r[Interrupt])&(Writerdy|Err)) == 0){
					WR(Irpten, Writerdy | Err);
					tsleep(&ctlr.r, datadone, 0, 8000);
					WR(Irpten, 0);
					i = ctlr.datadone;
					ctlr.datadone = 0;
					if(i&(Writerdy|Err) == 0){
						print("sdioio: (%d) no Writerdy intr %ux stat %ux\n", len, i, r[Hoststat]);
						error(Eio);
					}
				}
			}
			if(i&Writerdy){
				r[Data] = buf[0] | buf[1]<<8;
				r[Data] = buf[2] | buf[3]<<8;
				buf += 4;
				len -= 4;
				continue;
			}
			if(i&Err){
				err = r[ErrIntr];
				print("sdioio: (%d) write error intr %ux err %ux stat %ux\n", len, i, err, r[Hoststat]);
				WR(ErrIntr, err);
				WR(Interrupt, i);
				error(Eio);
			}
		}
	}else if(!write && PIOread){
		while(len > 0){
			if(Pollread){
				now = m->ticks;
				while(((i = r[Interrupt])&(Read8wready|Readrdy|Err)) == 0)
					if(m->ticks-now > 3*HZ){
						print("sdioio: (%d) no Readrdy intr %ux stat %ux\n", len, i, r[Hoststat]);
						error(Eio);
					}
			}else{
				if(((i = r[Interrupt])&(Read8wready|Readrdy|Err)) == 0){
					WR(Irpten, (len > 8*4? Read8wready : Readrdy) | Err);
					tsleep(&ctlr.r, datadone, 0, 3000);
					WR(Irpten, 0);
					i = ctlr.datadone;
					ctlr.datadone = 0;
					if(i&(Read8wready|Readrdy|Err) == 0){
						print("sdioio: (%d) no Readrdy intr %ux stat %ux\n", len, i, r[Hoststat]);
						error(Eio);
					}
				}
			}
			if((i&Read8wready) && len >= 8*4){
				for(i = 0; i < 8*2; i++){
					d = r[Data];
					buf[0] = d;
					buf[1] = d>>8;
					buf += 2;
				}
				len -= 8*4;
				continue;
			}
			if(i&Readrdy){
				d = r[Data];
				buf[0] = d;
				buf[1] = d>>8;
				d = r[Data];
				buf[2] = d;
				buf[3] = d>>8;
				buf += 4;
				len -= 4;
				continue;
			}
			if(i&Err){
				err = r[ErrIntr];
				print("sdioio: (%d) read error intr %ux err %ux stat %ux\n", len, i, err, r[Hoststat]);
				WR(ErrIntr, err);
				WR(Interrupt, i);
				error(Eio);
			}
		}
	}else{
		WR(Irpten, Dmadone|Err);
		tsleep(&ctlr.r, datadone, 0, 3000);
		WR(Irpten, 0);
		i = ctlr.datadone;
		ctlr.datadone = 0;
		if(i&Err){
			err = r[ErrIntr];
			print("sdioio: (%d) dma error intr %ux err %ux stat %ux\n", len, i, err, r[Hoststat]);
			WR(ErrIntr, err);
			WR(Interrupt, i);
			error(Eio);
		}
		if((i&Dmadone) == 0){
			print("sdioio: no dma end intr %ux stat %ux\n", i, r[Hoststat]);
			WR(Interrupt, i);
			error(Eio);
		}
		WR(Interrupt, Dmadone);
	}
	if(Polldone){
		now = m->ticks;
		while(((i = r[Interrupt])&(Datadone|Err)) == 0)
			if(m->ticks-now > 3*HZ)
				break;
	}else if((i&Datadone) == 0){
		WR(Irpten, Datadone|Err);
		tsleep(&ctlr.r, datadone, 0, 3000);
		i = ctlr.datadone;
		ctlr.datadone = 0;
		WR(Irpten, 0);
	}
	if(i&Err){
		err = r[ErrIntr];
		print("sdioio: %d error intr %ux %ux stat %ux\n",
				write, i, err, r[Hoststat]);
		WR(ErrIntr, err);
		WR(Interrupt, i);
		error(Eio);
	}
	if((i&Datadone) == 0){
		print("sdioio: %d timeout intr %ux stat %ux\n",
			write, i, r[Hoststat]);
		WR(Interrupt, i);
		error(Eio);
	}
	if(i)
		WR(Interrupt, i);
}

static void
sdiointerrupt(Ureg*, void*)
{	
	u32int *r;

	r = (u32int*)AddrSdio;
	ctlr.datadone = r[Interrupt];
	WR(Irpten, 0);
	wakeup(&ctlr.r);
}

SDio sdio = {
	"sdio",
	sdioinit,
	sdioenable,
	sdioinquiry,
	sdiocmd,
	sdioiosetup,
	sdioio,
};
