/*
 * LSI Fusion-MPT SAS 2.0 SCSI Host Adapter
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#include "../port/sd.h"

extern SDifc sdmpt2ifc;

static char Ebadunit[] = "invalid unit";
static char Enoreqs[] = "no free requests";
static char Etimeout[] = "request timeout";

/* System Interface Register Set */
enum {
	Doorbell			= 0x00,
	WriteSequence			= 0x04,
	HostDiagnostic			= 0x08,
	DiagRwData			= 0x10,
	DiagRwAddressLow		= 0x14,
	DiagRwAddressHigh		= 0x18,
	HostInterruptStatus		= 0x30,
	HostInterruptMask		= 0x34,
	DcrData				= 0x38,
	DcrAddress			= 0x3c,
	ReplyFreeHostIndex		= 0x48,
	ReplyPostHostIndex		= 0x6c,
	HcbSize				= 0x74,
	HcbAddressLow			= 0x78,
	HcbAddressHigh			= 0x7c,
	RequestDescriptorPostLow	= 0xc0,
	RequestDescriptorPostHigh	= 0xc4,
};

/* Doorbell */
enum {
	WhoInitNone			= 0x0<<24,
	WhoInitSystemBios		= 0x1<<24,
	WhoInitRomBios			= 0x2<<24,
	WhoInitPciPeer			= 0x3<<24,
	WhoInitHostDriver		= 0x4<<24,
	WhoInitManufacturing		= 0x5<<24,
	IocMessageUnitReset		= 0x40<<24,
	Handshake			= 0x42<<24,
	DoorbellUsed			= 1<<27,
	StateReset			= 0x0<<28,
	StateReady			= 0x1<<28,
	StateOperational		= 0x2<<28,
	StateFault			= 0x4<<28,
	StateMask			= 0xf<<28,
};

/* HostDiagnostic */
enum {
	HoldIocReset			= 1<<1,
	ResetAdapter			= 1<<2,
	DiagRwEnable			= 1<<4,
	ResetHistory			= 1<<5,
	FlashBadSignature		= 1<<6,
	DiagWriteEnable			= 1<<7,
	HcbMode				= 1<<8,
	ForceHcbOnReset			= 1<<9,
	ClearFlashBadSignature		= 1<<10,
	BootDeviceFlash			= 0<<11,
	BootDeviceHcdw			= 1<<11,
};

/* HostInterruptStatus */
enum {
	Ioc2SysDbStatus			= 1<<0,
	ReplyDescriptorInterrupt	= 1<<3,
	Sys2IocDbStatus			= 1<<31,
};

/* HostInterruptMask */
enum {
	Ioc2SysDbMask			= 1<<0,
	ReplyIntMask			= 1<<3,
	ResetIrqMask			= 1<<30,
};

/* Reply Descriptors */
enum {
	ScsiIoSuccess			= 0x0,
	AddressReply			= 0x1,
};

/* Messages */
enum {
	DefaultReplyLength		= 5,
	IocInit				= 0x02,
	IocFacts			= 0x03,
	PortEnable			= 0x06,
	EventNotification		= 0x07,
	EventAck			= 0x08,
	ScsiIoRequest			= 0x00,
	ScsiTaskManagement		= 0x01,
	SasIoUnitControl		= 0x1b,
	Config				= 0x04,
};

/* Events */
enum {
	LogData				= 0x0001,
	StateChange			= 0x0002,
	EventChange			= 0x000a,
	LogEntryAdded			= 0x0021,
	GpioInterrupt			= 0x0023,
	TempThreshold			= 0x0027,
	HostMessage			= 0x0028,
	PowerPerformanceChange		= 0x0029,
	HardResetReceived		= 0x0005,
	SasDeviceStatusChange		= 0x000f,
	IrOperationStatus		= 0x0014,
	IrVolume			= 0x001e,
	IrPhysicalDisk			= 0x001f,
	IrConfigurationChangeList	= 0x0020,
	SasDiscovery			= 0x0016,
	SasBroadcastPrimitive		= 0x0017,
	SasNotifyPrimitive		= 0x0026,
	SasInitDeviceStatusChange	= 0x0018,
	SasInitTableOverflow		= 0x0019,
	SasTopologyChangeList		= 0x001c,
	SasEnclDeviceStatusChange	= 0x001d,
	SasPhyCounter			= 0x0022,
	SasQuiesce			= 0x0025,
	HbdPhyEvent			= 0x0024,
};

typedef struct Ctlr Ctlr;
typedef struct Req Req;
typedef struct Unit Unit;

struct Unit {
	uchar		status;
	uchar		link;
	ushort		devh;
	ulong		info;
	ushort		flags;
	uchar		wwn[8];
};

struct Ctlr {
	Lock;
	int		ioc;
	char		name[8];
	int		port;
	Pcidev		*pcidev;
	SDev		*sdev;
	int		enabled;
	Rendez		reset;
	RWlock		resetlock;
	Lock		doorlock;
	RWlock		tasklock;

	Unit		unit[16];

	uchar		numports;
	ushort		maxtargs;

	ulong		*req;
	Req		*reqfree;
	Lock		reqfreelock;
	Lock		reqpostlock;
	ushort		reqcredit;
	ushort		iocreqfsz;
	ushort		reqfsz;

	ulong		*reply;
	ulong		*replyfree;
	ulong		*replypost;
	uchar		replyfsz;
	ushort		replyq;
	ushort		replyfreeq;
	ushort		replypostq;
	ushort		replypostmax;
	ulong		replyfreep;
	ulong		replypostp;

	Queue		*eventq;
};

struct Req {
	Rendez;
	ushort		smid;
	ulong		*req;
	SDreq		*sdreq;
	int		done;
	Req		*next;
};

static ulong
iocread(Ctlr *ctlr, int reg)
{
	return inl(ctlr->port+reg);
}

static void
iocwrite(Ctlr *ctlr, int reg, ulong val)
{
	outl(ctlr->port+reg, val);
}

static ulong
iocwait(Ctlr *ctlr, int reg, ulong mask, int on)
{
	ulong t, val;

	t = MACHP(0)->ticks;
	for(;;){
		val = iocread(ctlr, reg);
		if(on)
		if(val & mask)
			return val;
		if(!on)
		if(!(val & mask))
			return val;
		if(TK2MS(MACHP(0)->ticks - t) > 10*1000)	/* PG §3.7.1 */
			panic("iocwait: %s: wedge reg %#.2ux val %#.8ulx",
			    ctlr->name, reg, val);
		microdelay(500);
	}
}

static void
ckstate(Ctlr *ctlr, int state)
{
	ulong val;

	val = iocread(ctlr, Doorbell);
	if(val & StateMask != state)
		panic("ckstate: %s: bad state %#.8ulx", ctlr->name, val);
}

static int
ckstatus(Ctlr *ctlr, ulong *reply)
{
	ushort status;

	/*
	 * IOC Status is provided in every reply, which may be used
	 * to determine the success of a given function independent of
	 * message content.  In the event an unexpected status is
	 * returned, a panic is issued.
	 */
	status = reply[3]>>16 & 0x7fff;		/* IOCStatus */
	if(status == 0x0000)	/* SUCCESS */
		return 1;

	/*
	 * Some functions support nominal failure modes; rather than
	 * panic, we allow the caller to determine the severity of the
	 * condition.
	 */
	switch(reply[0]>>24){			/* Function */
	case ScsiIoRequest:
	case ScsiTaskManagement:
		switch(status){
		case 0x0040:	/* SCSI_RECOVERED_ERROR */
		case 0x0042:	/* SCSI_INVALID_DEVHANDLE */
		case 0x0043:	/* SCSI_DEVICE_NOT_THERE */
		case 0x0044:	/* SCSI_DATA_OVERRUN */
		case 0x0045:	/* SCSI_DATA_UNDERRUN */
		case 0x0046:	/* SCSI_IO_DATA_ERROR */
		case 0x0047:	/* SCSI_PROTOCOL_ERROR */
		case 0x0048:	/* SCSI_TASK_TERMINATED */
		case 0x0049:	/* SCSI_RESIDUAL_MISMATCH */
		case 0x004a:	/* SCSI_TASK_MGMT_FAILED */
		case 0x004b:	/* SCSI_IOC_TERMINATED */
		case 0x004c:	/* SCSI_EXT_TERMINATED */
			return 0;
		}
		break;
	case Config:
		switch(status){
		case 0x0020:	/* CONFIG_INVALID_ACTION */
		case 0x0021:	/* CONFIG_INVALID_TYPE */
		case 0x0022:	/* CONFIG_INVALID_PAGE */
		case 0x0023:	/* CONFIG_INVALID_DATA */
		case 0x0024:	/* CONFIG_NO_DEFAULTS */
		case 0x0025:	/* CONFIG_CANT_COMMIT */
			return 0;
		}
		break;
	}
	panic("ckstatus: %s: bad status %#.4ux", ctlr->name, status);
	return -1;	/* not reached */
}

static int
doorbell(Ctlr *ctlr, ulong *req, int nwords)
{
	ulong val;
	int i;
	ushort *reply;

	ilock(&ctlr->doorlock);
	iocwait(ctlr, Doorbell, DoorbellUsed, 0);
	iocwrite(ctlr, HostInterruptStatus, 0);

	val = Handshake | nwords<<16;
	iocwrite(ctlr, Doorbell, val);

	iocwait(ctlr, HostInterruptStatus, Ioc2SysDbStatus, 1);
	iocwrite(ctlr, HostInterruptStatus, 0);

	iocwait(ctlr, HostInterruptStatus, Sys2IocDbStatus, 0);

	for(i = 0; i < nwords; ++i){
		iocwrite(ctlr, Doorbell, req[i]);
		iocwait(ctlr, HostInterruptStatus, Sys2IocDbStatus, 0);
	}

	/*
	 * We do something sneaky here; replies are written back
	 * into the request buffer during handshake.  Buffers must be
	 * sized to accomodate the larger of the two messages.
	 *
	 * Doorbell reads yield 16 bits at a time; upper bits are
	 * considered reserved.  The reply MsgLength is located in the
	 * first 32-bit word; a reply will always contain at least
	 * DefaultReplyLength words.
	 */
	reply = (ushort *)req;
	nwords = DefaultReplyLength;
	for(i = 0; i < nwords*2; ++i){
		iocwait(ctlr, HostInterruptStatus, Ioc2SysDbStatus, 1);
		reply[i] = iocread(ctlr, Doorbell);
		iocwrite(ctlr, HostInterruptStatus, 0);
		if(i == 1)
			nwords = reply[i] & 0xff;	/* MsgLength */
	}

	iocwait(ctlr, HostInterruptStatus, Ioc2SysDbStatus, 1);
	iocwrite(ctlr, HostInterruptStatus, 0);

	iocwait(ctlr, Doorbell, DoorbellUsed, 0);
	iunlock(&ctlr->doorlock);
	return nwords;
}

#define UNIT(ctlr, n)	((ctlr)->unit + (n))

static int
getunitcaps(Unit *u, char *p, int l)
{
	char *o, *e;
	int i;
	static char *caps[] = {
		[4]	"fua",		/* SATA FUA */
		[5]	"ncq",		/* SATA NCQ */
		[6]	"smart",	/* SATA SMART */
		[7]	"lba48",	/* SATA 48-bit LBA */
		[9]	"ssp",		/* SATA Software Settings Preservation */
		[10]	"async",	/* SATA Asynchronous Notification */
		[11]	"partial",	/* Partial Power Management Mode */
		[12]	"slumber",	/* Slumber Power Management Mode */
		[13]	"fp",		/* Fast Path */
	};

	o = p;
	e = p + l;
	for(i = 0; i < nelem(caps); ++i)
		if(caps[i])
		if(u->flags & 1<<i){
			if(p != o)
				p = seprint(p, e, " ");
			p = seprint(p, e, "%s", caps[i]);
		}
	if(p == o)
		p = seprint(p, e, "none");
	return p - o;
}

static char *
unittype(Unit *u)
{
	if(u->info & 1<<7)			/* Device Type */
		return "sata";
	return "sas";
}

static char *
unitstatus(Unit *u)
{
	switch(u->status & 0xf){		/* Reason Code */
	case 0x1:
	case 0x5:
		return "online";
	case 0x2:
		return "missing";
	case 0x3:
		return "linkchange";
	case 0x4:
		return "nolinkchange";
	default:
		return "unknown";
	}
}

static char *
unitlink(Unit *u)
{
	switch(u->link>>4){			/* Current Link Rate */
	case 0x8:
		return "1.5Gb/s";
	case 0x9:
		return "3.0Gb/s";
	case 0xa:
		return "6.0Gb/s";
	case 0xb:
		return "12.0Gb/s";
	default:
		return "unknown";
	}
}

static int
unitonline(Unit *u)
{
	switch(u->status & 0xf){		/* Reason Code */
	case 0x1:
	case 0x5:
		return 1;
	default:
		return 0;
	}
}

#define REQ(ctlr, n)	((Req *)((ctlr)->req + (n)*(ctlr)->reqfsz + \
			    (ctlr)->iocreqfsz))

static ulong *
reallocreq(Ctlr *ctlr)
{
	ushort n;

	free(ctlr->req);

	/*
	 * System Request Message Frames must be allocated
	 * contiguously, aligned on a 16-byte boundary, and be a
	 * multiple of 16 bytes in length.
	 */
	n = (ctlr->iocreqfsz + ROUNDUP(sizeof(Req), 16))*BY2WD;
	ctlr->reqfsz = n/BY2WD;
	ctlr->req = mallocalign(ctlr->reqcredit*n, 16, 0, 0);
	if(ctlr->req == nil)
		print("reallocreq: %s: out of memory\n", ctlr->name);
	return ctlr->req;
}

static Req *
nextreq(Ctlr *ctlr)
{
	Req *r;

	lock(&ctlr->reqfreelock);
	if(r = ctlr->reqfree)
		ctlr->reqfree = r->next;
	unlock(&ctlr->reqfreelock);
	return r;
}

static void
freereq(Ctlr *ctlr, Req *r)
{
	lock(&ctlr->reqfreelock);
	r->next = ctlr->reqfree;
	ctlr->reqfree = r;
	unlock(&ctlr->reqfreelock);
}

static int
reqdone(void *arg)
{
	Req *r;

	r = arg;
	return r->done;
}

static void
postreq(Ctlr *ctlr, Req *r, ulong *desc, int ms)
{
	r->done = 0;

	ilock(&ctlr->reqpostlock);
	iocwrite(ctlr, RequestDescriptorPostLow, desc[0]);
	iocwrite(ctlr, RequestDescriptorPostHigh, desc[1]);
	iunlock(&ctlr->reqpostlock);

	while(waserror())
		;
	tsleep(r, reqdone, r, ms);
	poperror();

	if(!r->done)
		error(Etimeout);
}

static void
postio(Ctlr *ctlr, Req *r, ulong *desc, int ms)
{
	rlock(&ctlr->tasklock);
	if(waserror()){
		runlock(&ctlr->tasklock);
		nexterror();
	}
	postreq(ctlr, r, desc, ms);
	poperror();
	runlock(&ctlr->tasklock);
}

static void
posttask(Ctlr *ctlr, Req *r, ulong *desc, int ms)
{
	wlock(&ctlr->tasklock);
	if(waserror()){
		wunlock(&ctlr->tasklock);
		nexterror();
	}
	postreq(ctlr, r, desc, ms);
	poperror();
	wunlock(&ctlr->tasklock);
}

static void
mksge(ulong *sgl, void *data, int len, int write)
{
	int flags;

	flags = 1<<7;				/* LastElement */
	flags |= 1<<6;				/* EndOfBuffer */
	flags |= 1<<4;				/* ElementType (Simple Element) */
	if(write)
		flags |= 1<<2;			/* Direction (Write) */
	flags |= 0<<1;				/* AddressSize (32-bit) */
	flags |= 1<<0;				/* EndOfList */

	sgl[0] = flags<<24;			/* Flags */
	sgl[0] |= len & 0xffffff;		/* Length */
	if(data)
		sgl[1] = PCIWADDR(data);	/* Address */
	else
		sgl[1] = 0;
}

static void
iocfacts(Ctlr *ctlr)
{
	ulong buf[16];

	memset(buf, 0, 3*BY2WD);
	buf[0] = IocFacts<<24;			/* Function */

	doorbell(ctlr, buf, 3);
	ckstatus(ctlr, buf);

	ctlr->numports = buf[5]>>16 & 0xff;	/* NumberOfPorts */
	ctlr->reqcredit = buf[6] & 0xffff;	/* RequestCredit */
	ctlr->iocreqfsz = buf[9] & 0xffff;	/* IOCRequestFrameSize */
	ctlr->maxtargs = buf[10]>>16;		/* MaxTargets */
	ctlr->replyfsz = buf[12]>>16 & 0xff;	/* ReplyFrameSize */
	ctlr->replypostmax = buf[13] & 0xffff;	/* MaxReplyDescriptorPostQueueDepth */
}

static void
iocinit(Ctlr *ctlr)
{
	ulong buf[18];

	memset(buf, 0, 18*BY2WD);
	buf[0] = IocInit<<24;			/* Function */
	buf[6] = ctlr->reqfsz<<16;		/* SystemRequestFrameSize */
	buf[7] = ctlr->replyfreeq<<16;		/* ReplyFreeQueueDepth */
	buf[7] |= ctlr->replypostq;		/* ReplyDescriptorPostQueueDepth */
	buf[10] = PCIWADDR(ctlr->req);		/* SystemRequestFrameBaseAddress */
	buf[12] = PCIWADDR(ctlr->replypost);	/* ReplyDescriptorPostQueueAddress */
	buf[14] = PCIWADDR(ctlr->replyfree);	/* ReplyFreeQueueAddress */

	doorbell(ctlr, buf, 18);
	ckstatus(ctlr, buf);
	ckstate(ctlr, StateOperational);
}

#define EVENT(x)	(1U<<((x) % 32))

static void
eventnotify(Ctlr *ctlr)
{
	ulong buf[11];

	memset(buf, 0, 11*BY2WD);
	buf[0] = EventNotification<<24;		/* Function */

	/*
	 * Event notifications are masked using the bit identified
	 * by the value of the event; see MPI §8.4.  PG §3.7.4
	 * suggests a number of SAS events required for proper host
	 * mapping, however the SAS_TOPOLOGY_CHANGE_LIST event is
	 * merely sufficient.
	 */
	buf[5] = ~EVENT(SasTopologyChangeList);
	buf[6] = ~0;
	buf[7] = ~0;
	buf[8] = ~0;
	buf[9] = ~0;

	doorbell(ctlr, buf, 11);
	ckstatus(ctlr, buf);
}

static void
eventack(Ctlr *ctlr, ushort event, ulong context)
{
	Req *r;
	ulong desc[2];

	r = nextreq(ctlr);
	if(r == nil)
		error(Enoreqs);
	memset(r->req, 0, 5*BY2WD);
	r->req[0] = EventAck<<24;		/* Function */
	r->req[3] = event;			/* Event */
	r->req[4] = context;			/* EventContext */

	desc[0] = r->smid<<16 | 0x4<<1;		/* Default Request */
	desc[1] = 0;
	postreq(ctlr, r, desc, 5*1000);		/* PG §3.7.4 */
	freereq(ctlr, r);
}

static void
portenable(Ctlr *ctlr)
{
	Req *r;
	ulong desc[2];

	/*
	 * The Port Enable message is posted using the Request
	 * Descriptor Post Queue for reliable delivery of events.
	 * Use of the System Doorbell will miss events on a
	 * uniprocessor.
	 */
	r = nextreq(ctlr);
	if(r == nil)
		error(Enoreqs);
	memset(r->req, 0, 3*BY2WD);
	r->req[0] = PortEnable<<24;		/* Function */

	desc[0] = r->smid<<16 | 0x4<<1;		/* Default Request */
	desc[1] = 0;
	postreq(ctlr, r, desc, 300*1000);	/* PG §3.7.1 */
	freereq(ctlr, r);
}

static void
unitconfig(Ctlr *ctlr, Unit *u)
{
	ulong buf[7+2], page[14];

	/*
	 * Unit configuration is pulled from SAS Device Page 0.  The
	 * DeviceInfo and Flags fields provide device interconnect and
	 * capabilities.  We obtain configuration via the System
	 * Doorbell to simplify access to the page header.
	 */
	memset(buf, 0, 7*BY2WD);
	buf[0] = Config<<24;			/* Function */
	buf[0] |= 0x00;				/* Action (PAGE_HEADER) */
	buf[1] = 0x12<<16;			/* ExtPageType (SAS_DEVICE) */
	buf[5] = 0xf<<24;			/* PageType (Extended) */
	buf[5] |= 0<<16;			/* PageNumber */
	mksge(buf+7, nil, 0, 0);		/* PageBufferSGE (empty) */

	doorbell(ctlr, buf, 7+2);
	if(!ckstatus(ctlr, buf))
		error(Ebadunit);

	buf[0] |= 0x01;				/* Action (READ_CURRENT) */
	buf[6] = 0x2<<28 | u->devh;		/* PageAddress (HANDLE) */
	mksge(buf+7, page, sizeof page, 0);	/* PageBufferSGE */

	doorbell(ctlr, buf, 7+2);
	if(!ckstatus(ctlr, buf))
		error(Ebadunit);

	u->info = page[7];			/* DeviceInfo */
	u->flags = page[8] & 0xffff;		/* Flags */
	memmove(u->wwn, page+9, 8);		/* DeviceName */
}

static void
unitremove(Ctlr *ctlr, Unit *u)
{
	Req *r;
	ulong desc[2];

	r = nextreq(ctlr);
	if(r == nil)
		error(Enoreqs);
	memset(r->req, 0, 11*BY2WD);
	r->req[0] = SasIoUnitControl<<24;	/* Function */
	r->req[0] |= 0x0d;			/* Operation (REMOVE_DEVICE) */
	r->req[1] = u->devh;			/* DevHandle */

	desc[0] = r->smid<<16 | 0x4<<1;		/* Default Request */
	desc[1] = 0;
	postreq(ctlr, r, desc, 5*1000);		/* PG §3.7.4 */
	freereq(ctlr, r);
}

static void
unitreset(Ctlr *ctlr, Unit *u)
{
	Req *r;
	ulong desc[2];

	r = nextreq(ctlr);
	if(r == nil)
		error(Enoreqs);
	memset(r->req, 0, 13*BY2WD);
	r->req[0] = ScsiTaskManagement<<24;	/* Function */
	r->req[0] |= u->devh;			/* DevHandle */
	r->req[1] = 0x03<<8;			/* TaskType (Target Reset) */

	desc[0] = r->smid<<16 | 0x3<<1;		/* High Priority Request */
	desc[1] = 0;
	posttask(ctlr, r, desc, 30*1000);	/* PG §3.7.3 */
	freereq(ctlr, r);
}

static void
scsiabort(Ctlr *ctlr, Unit *u, Req *s)
{
	Req *r;
	ulong desc[2];

	r = nextreq(ctlr);
	if(r == nil)
		error(Enoreqs);
	memset(r->req, 0, 13*BY2WD);
	r->req[0] = ScsiTaskManagement<<24;	/* Function */
	r->req[0] |= u->devh;			/* DevHandle */
	r->req[1] = 0x01<<8;			/* TaskType (Abort Task) */
	r->req[12] = s->smid;			/* TaskMID */

	desc[0] = r->smid<<16 | 0x3<<1;		/* High Priority Request */
	desc[1] = 0;
	posttask(ctlr, r, desc, 5*1000);	/* PG §3.7.3 */
	freereq(ctlr, r);
}

static void
scsiio(Ctlr *ctlr, Unit *u, SDreq *sdreq)
{
	Req *r;
	ulong desc[2];

	r = nextreq(ctlr);
	if(r == nil){
		sdreq->status = SDbusy;
		return;
	}
	r->sdreq = sdreq;
	memset(r->req, 0, 24*BY2WD);
	r->req[0] = ScsiIoRequest<<24;		/* Function */
	r->req[0] |= u->devh;			/* DevHandle */
	r->req[3] = PCIWADDR(sdreq->sense);	/* SenseBufferLowAddress */
	r->req[4] = sizeof sdreq->sense<<16;	/* SenseBufferLength */
	r->req[5] = 24;				/* SGLOffset0 */
	r->req[7] = sdreq->dlen;		/* DataLength */
	r->req[9] = sdreq->clen;		/* CDBLength */
	if(sdreq->write)
		r->req[15] = 0x1<<24;		/* Data Direction (Write) */
	else
		r->req[15] = 0x2<<24;		/* Data Direction (Read) */

	memmove(r->req+16, sdreq->cmd, sdreq->clen);
	mksge(r->req+24, sdreq->data, sdreq->dlen, sdreq->write);

	desc[0] = r->smid<<16 | 0x0<<1;		/* SCSI IO Request */
	desc[1] = u->devh<<16;
	if(waserror()){
		/*
		 * SCSI Task Management Requests are guaranteed to
		 * block until the IOC has replied to outstanding
		 * messages; see MPI §9.5.
		 */
		if(!r->done)
			scsiabort(ctlr, u, r);
		if(!r->done)
			unitreset(ctlr, u);
		if(!r->done)
			nexterror();
	}else{
		postio(ctlr, r, desc, 10*1000);
		poperror();
	}
	freereq(ctlr, r);
}

#define REPLY(ctlr, n)		((ctlr)->reply + (n)*(ctlr)->replyfsz)
#define REPLYPOST(ctlr, n)	((ctlr)->replypost + (n)*2)

static ulong *
reallocreply(Ctlr *ctlr)
{
	free(ctlr->reply);

	/*
	 * System Reply Message Frames are less disciplined; they do
	 * not have to be contiguous, must be aligned on a 4-byte
	 * boundary, and must be a multiple of 4 bytes in length.
	 */
	ctlr->replyq = ctlr->reqcredit + 32;	/* PG §3.1.3 */
	ctlr->reply = mallocz(ctlr->replyq * ctlr->replyfsz*BY2WD, 0);
	if(ctlr->reply == nil)
		print("reallocreply: %s: out of memory\n", ctlr->name);
	return ctlr->reply;
}

static ulong *
reallocreplyfree(Ctlr *ctlr)
{
	free(ctlr->replyfree);

	/*
	 * The Reply Free Queue must be allocated contiguously,
	 * aligned on a 16-byte boundary, and must have a depth that
	 * is a multiple of 16 bytes.
	 */
	ctlr->replyfreeq = ROUNDUP(ctlr->replyq + 1, 16);
	ctlr->replyfree = mallocalign(ctlr->replyfreeq * BY2WD, 16, 0, 0);
	if(ctlr->replyfree == nil)
		print("reallocreplyfree: %s: out of memory\n", ctlr->name);
	return ctlr->replyfree;
}

static ulong *
reallocreplypost(Ctlr *ctlr)
{
	free(ctlr->replypost);

	/*
	 * The Reply Descriptor Post Queue must be allocated
	 * contiguously and aligned on a 16-byte boundary.  The depth
	 * must not exceed MaxReplyDescriptorPostQueueDepth returned
	 * in the IOC Facts reply.
	 */
	ctlr->replypostq = MIN(ctlr->replypostmax,
	    ROUNDUP(ctlr->replyq + ctlr->reqcredit + 1, 16));
	ctlr->replypost = mallocalign(ctlr->replypostq * 2*BY2WD, 16, 0, 0);
	if(ctlr->replypost == nil)
		print("reallocreplypost: %s: out of memory\n", ctlr->name);
	return ctlr->replypost;
}

static void
freereply(Ctlr *ctlr, ulong *reply)
{
	ctlr->replyfree[ctlr->replyfreep] = PCIWADDR(reply);
	ctlr->replyfreep++;
	if(ctlr->replyfreep == ctlr->replyfreeq)
		ctlr->replyfreep = 0;
}

static ulong *
nextreplypost(Ctlr *ctlr)
{
	ulong *desc;

	desc = REPLYPOST(ctlr, ctlr->replypostp);
	if(desc[0] == 0xffffffff)
	if(desc[1] == 0xffffffff)
		return nil;
	return desc;
}

static void
freereplypost(Ctlr *ctlr, ulong *desc)
{
	desc[0] = 0xffffffff;
	desc[1] = 0xffffffff;
	ctlr->replypostp++;
	if(ctlr->replypostp == ctlr->replypostq)
		ctlr->replypostp = 0;
}

static void
scsiok(Ctlr *ctlr, ushort smid)
{
	Req *r;

	r = REQ(ctlr, smid);
	r->sdreq->status = SDok;
	r->sdreq->rlen = r->sdreq->dlen;
	r->done++;
	wakeup(r);
}

static void
scsierror(Ctlr *ctlr, ushort smid, ulong *reply)
{
	Req *r;

	ckstatus(ctlr, reply);
	r = REQ(ctlr, smid);
	r->sdreq->status = reply[3] & 0xff;	/* SCSIStatus */
	if(reply[3]>>8 & 1<<0)			/* SCSIState (Sense Valid) */
		r->sdreq->flags |= SDvalidsense;
	r->sdreq->rlen = reply[5];		/* TransferCount */
	r->done++;
	wakeup(r);
}

static void
doreply(Ctlr *ctlr, ushort smid, ulong *reply)
{
	Req *r;

	ckstatus(ctlr, reply);
	r = REQ(ctlr, smid);
	r->done++;
	wakeup(r);
}

static void
topoevent(Ctlr *ctlr, ulong *data)
{
	ulong *p, *e;
	int i;
	Unit *u;

	/*
	 * Unfortunately, devsd limits us to 16 devices, which
	 * essentially precludes support for SAS expanders and/or
	 * enclosures.  A one-to-one mapping exists between a direct
	 * attached PHY and a device for simplicity.
	 */
	if(data[0]>>16 != 0x0)			/* ExpanderDevHandle */
		return;
	if((data[0] & 0xffff) != 0x1)		/* EnclosureHandle */
		return;

	/*
	 * SAS topology changes are handled in two phases; we first
	 * obtain identifying information from event data.  New units
	 * require additional configuration information and missing
	 * units must have resources released.
	 */
	p = data + 3;
	e = p + (data[2] & 0xff);		/* NumEntries */
	i = data[2]>>8 & 0xff;			/* StartPhyNum */
	for(; p < e && i < nelem(ctlr->unit); ++p, ++i){
		u = UNIT(ctlr, i);
		u->status = *p>>24 & 0xff;	/* PhyStatus */
		u->link = *p>>16 & 0xff;	/* LinkRate */
		switch(u->status & 0xf){	/* Reason Code */
		case 0x1:
			u->devh = *p & 0xffff;	/* AttachedDevHandle */
			unitconfig(ctlr, u);
			break;
		case 0x2:
			unitreset(ctlr, u);
			unitremove(ctlr, u);
			break;
		}
	}
}

static void
doevent(Ctlr *ctlr, ulong *reply)
{
	ushort event;
	ulong context;
	ulong *data;

	event = reply[5] & 0xffff;		/* Event */
	context = reply[6];			/* EventContext */
	data = reply + 7;			/* EventData */
	switch(event){
	case SasTopologyChangeList:
		topoevent(ctlr, data);
		break;
	default:
		panic("doevent: %s: bad event %#.4ux", ctlr->name, event);
	}
	if(reply[1]>>16 & 0xff)			/* AckRequired */
		eventack(ctlr, event, context);
}

static void
qevent(Ctlr *ctlr, ulong *reply)
{
	int n;
	Block *bp;

	n = (reply[0]>>16 & 0xff)*BY2WD;	/* MsgLength */
	bp = iallocb(n);
	if(bp == nil)
		panic("qevent: %s: out of memory", ctlr->name);
	memmove(bp->wp, reply, n);
	bp->wp += n;
	qpassnolim(ctlr->eventq, bp);
}

static void
addressreply(Ctlr *ctlr, ushort smid, ulong *reply)
{
	uchar fn;

	fn = reply[0]>>24;			/* Function */
	switch(fn){
	case PortEnable:
	case ScsiTaskManagement:
	case SasIoUnitControl:
		doreply(ctlr, smid, reply);
		break;
	case ScsiIoRequest:
		/*
		 * Address Replies to SCSI IO Requests always
		 * indicate an error; see MPI §9.4.
		 */
		scsierror(ctlr, smid, reply);
		break;
	case EventNotification:
		/*
		 * We queue events for handling in a separate
		 * process to ensure sanity when the IOC requires
		 * synchronization via acknowledgement.
		 */
		qevent(ctlr, reply);
		break;
	default:
		panic("addressreply: %s: bad function %#.2ux", ctlr->name, fn);
	}

	/*
	 * To simplify handing a System Reply Message Frame back to
	 * the IOC, we update the ReplyFreeHostIndex register
	 * immediately.  Unfortunately, this requires additional
	 * coherence.
	 */
	freereply(ctlr, reply);
	coherence();
	iocwrite(ctlr, ReplyFreeHostIndex, ctlr->replyfreep);
}

static void
freectlr(Ctlr *ctlr)
{
	iofree(ctlr->port);
	qfree(ctlr->eventq);
	free(ctlr->req);
	free(ctlr->reply);
	free(ctlr->replyfree);
	free(ctlr->replypost);
	free(ctlr->sdev);
	free(ctlr);
}

static void
resetctlr(Ctlr *ctlr)
{
	ulong val;

	iocwrite(ctlr, WriteSequence, 0);	/* flush */
	iocwrite(ctlr, WriteSequence, 0xf);
	iocwrite(ctlr, WriteSequence, 0x4);
	iocwrite(ctlr, WriteSequence, 0xb);
	iocwrite(ctlr, WriteSequence, 0x2);
	iocwrite(ctlr, WriteSequence, 0x7);
	iocwrite(ctlr, WriteSequence, 0xd);

	val = iocread(ctlr, HostDiagnostic);
	val |= ResetAdapter;
	iocwrite(ctlr, HostDiagnostic, val);

	delay(50);	/* don't touch! */

	val = iocwait(ctlr, HostDiagnostic, ResetAdapter, 0);
	if(val & HcbMode)
		panic("resetctlr: %s: host boot not supported", ctlr->name);
	val &= ~HoldIocReset;
	iocwrite(ctlr, HostDiagnostic, val);
	iocwrite(ctlr, WriteSequence, 0xf);	/* disable */

	iocwait(ctlr, Doorbell, StateMask, 1);
	ckstate(ctlr, StateReady);
}

static int
initctlr(Ctlr *ctlr)
{
	int i;
	Req *r;
	ulong *p;

	memset(ctlr->unit, 0, sizeof ctlr->unit);

	/*
	 * Each time the controller is reset, an IOC Facts reponse
	 * may return different values.  We err on the side of caution
	 * and reallocate resources prior to issuing an IOC Init
	 * request.
	 */
	iocfacts(ctlr);
	if(reallocreq(ctlr))
	if(reallocreply(ctlr))
	if(reallocreplyfree(ctlr))
	if(reallocreplypost(ctlr))
		goto init;
	return -1;
init:
	iocinit(ctlr);

	/*
	 * Initialize System Request Message Frames and associated
	 * structures.  A SMID is written once to avoid headaches
	 * constructing messages in the I/O path.  A SMID of 0 must be
	 * initialized and is considered reserved; it may not be
	 * placed on the free list or used by the host in any way.
	 */
	ctlr->reqfree = nil;
	for(i = 1; i < ctlr->reqcredit; ++i){
		r = REQ(ctlr, i);
		r->smid = i;
		r->req = (ulong *)r - ctlr->iocreqfsz;
		freereq(ctlr, r);
	}

	/*
	 * The Reply Descriptor Post Queue must be initialized with
	 * the unused descriptor type for each entry.  This is
	 * slightly reordered to take advantage of coherence required
	 * when updating the ReplyFreeHostIndex register below.
	 */
	ctlr->replypostp = 0;
	for(i = 0; i < ctlr->replypostq; ++i){
		p = REPLYPOST(ctlr, i);
		freereplypost(ctlr, p);
	}

	/*
	 * The Reply Free Queue is initialized with the lower 32
	 * bits of the PADDR for each System Reply Frame.  The
	 * ReplyFreeHostIndex register is initialized with the next
	 * (unused) entry.
	 */
	ctlr->replyfreep = 0;
	for(i = 0; i < ctlr->replyq; ++i){
		p = REPLY(ctlr, i);
		freereply(ctlr, p);
	}
	coherence();
	iocwrite(ctlr, ReplyFreeHostIndex, ctlr->replyfreep);

	/*
	 * Enable event notifications.
	 */
	qreopen(ctlr->eventq);
	eventnotify(ctlr);
	return 0;
}

static void
enablectlr(Ctlr *ctlr)
{
	ulong val;

	val = iocread(ctlr, HostInterruptMask);
	val &= ~ReplyIntMask;
	iocwrite(ctlr, HostInterruptMask, val);
}

static void
disablectlr(Ctlr *ctlr)
{
	ulong val;

	val = iocread(ctlr, HostInterruptMask);
	val |= ReplyIntMask;
	iocwrite(ctlr, HostInterruptMask, val);
}

static SDev *
mpt2pnp(void)
{
	Pcidev *p;
	SDev *sdev, *head, *tail;
	int ioc;
	char name[8];
	Ctlr *ctlr;
	static int iocs;

	p = nil;
	head = tail = nil;
	while(p = pcimatch(p, 0x1000, 0)){
		switch(p->did){
		case 0x0070:	/* LSISAS2004 */
		case 0x0072:	/* LSISAS2008 */
		case 0x0074:	/* LSISAS2108 */
		case 0x0076:
		case 0x0077:
		case 0x0064:	/* LSISAS2116 */
		case 0x0065:
		case 0x0080:	/* LSISAS2208 */
		case 0x0081:
		case 0x0082:
		case 0x0083:
		case 0x0084:
		case 0x0085:
		case 0x0086:	/* LSISAS2308 */
		case 0x0087:
			break;
		default:
			continue;
		}
		ioc = iocs++;
		snprint(name, sizeof name, "ioc%d", ioc);
		ctlr = malloc(sizeof *ctlr);
		if(ctlr == nil){
			print("mpt2pnp: %s: out of memory\n", name);
			continue;
		}
		ctlr->ioc = ioc;
		strncpy(ctlr->name, name, sizeof ctlr->name);
		ctlr->port = p->mem[0].bar & ~1;
		if(ioalloc(ctlr->port, p->mem[0].size, 0, "mpt2") < 0){
			print("mpt2pnp: %s: port %ux in use\n", name, ctlr->port);
			freectlr(ctlr);
			continue;
		}
		pcisetbme(p);
		ctlr->pcidev = p;
		ctlr->eventq = qopen(0, Qmsg|Qclosed, nil, nil);
		if(ctlr->eventq == nil){
			print("mpt2pnp: %s: qopen failed\n", name);
			freectlr(ctlr);
			continue;
		}
		resetctlr(ctlr);
		if(initctlr(ctlr) < 0){
			print("mpt2pnp: %s: initctlr failed\n", name);
			freectlr(ctlr);
			continue;
		}
		sdev = malloc(sizeof *sdev);
		if(sdev == nil){
			print("mpt2pnp: %s: out of memory\n", name);
			freectlr(ctlr);
			continue;
		}
		sdev->ifc = &sdmpt2ifc;
		sdev->ctlr = ctlr;
		sdev->idno = 'M' + ioc;
		sdev->nunit = nelem(ctlr->unit);
		ctlr->sdev = sdev;
		print("#S/sd%c: %s: mpt2 sas-2 with %d ports, %d max targets\n",
		    sdev->idno, name, ctlr->numports, ctlr->maxtargs);
		if(head == nil)
			head = sdev;
		else
			tail->next = sdev;
		tail = sdev;
	}
	return head;
}

static void
mpt2interrupt(Ureg *, void *arg)
{
	Ctlr *ctlr;
	ulong val, *desc, *reply;
	ushort smid;

	ctlr = arg;
	ilock(ctlr);
	val = iocread(ctlr, HostInterruptStatus);
	if(!(val & ReplyDescriptorInterrupt)){
		iunlock(ctlr);
		return;
	}
	for(;;){
		desc = nextreplypost(ctlr);
		if(desc == nil)
			break;
		switch(desc[0] & 0xf){		/* ReplyFlags */
		case ScsiIoSuccess:
			smid = desc[0]>>16;	/* SMID */
			scsiok(ctlr, smid);
			break;
		case AddressReply:
			smid = desc[0]>>16;	/* SMID */
			reply = KADDR(desc[1]);	/* ReplyFrameAddress */
			addressreply(ctlr, smid, reply);
			break;
		default:
			panic("mpt2interrupt: %s: bad reply %#.8ulx %#.8ulx",
			    ctlr->name, desc[0], desc[1]);
		}
		freereplypost(ctlr, desc);
	}
	iunlock(ctlr);	/* coherence */
	iocwrite(ctlr, ReplyPostHostIndex, ctlr->replypostp);
}

static void
mpt2reset(void *arg)
{
	Ctlr *ctlr;

	ctlr = arg;
	for(;;){
		while(waserror())
			;
		sleep(&ctlr->reset, return0, nil);
		poperror();

		qclose(ctlr->eventq);

		wlock(&ctlr->resetlock);
		ilock(ctlr);
		disablectlr(ctlr);
		resetctlr(ctlr);
		if(initctlr(ctlr) < 0){
			iunlock(ctlr);
			wunlock(&ctlr->resetlock);
			print("mpt2reset: %s: initctlr failed\n", ctlr->name);
			continue;
		}
		enablectlr(ctlr);
		iunlock(ctlr);
		wunlock(&ctlr->resetlock);

		if(waserror())
			print("mpt2reset: %s: %s\n",ctlr->name, up->errstr);
		else{
			portenable(ctlr);
			poperror();
		}
	}
}

static void
mpt2event(void *arg)
{
	Ctlr *ctlr;
	Block *bp;
	ulong *reply;

	/*
	 * For the unwary, a pending reset will first close the
	 * eventq, which will cause qbread to eventually error.  The
	 * control structure below yields to the reset kproc until the
	 * eventq is reopened and sanity is restored.
	 */
	ctlr = arg;
	while(waserror())
		yield();
	for(;;){
		rlock(&ctlr->resetlock);
		if(waserror()){
			runlock(&ctlr->resetlock);
			nexterror();
		}
		bp = qbread(ctlr->eventq, 0);
		if(bp)
		if(waserror())
			wakeup(&ctlr->reset);
		else{
			reply = (ulong *)bp->rp;
			doevent(ctlr, reply);
			poperror();
		}
		freeb(bp);
		poperror();
		runlock(&ctlr->resetlock);
	}
}

static int
mpt2enable(SDev *sdev)
{
	Ctlr *ctlr;
	Pcidev *p;
	char name[32];

	ctlr = sdev->ctlr;
	p = ctlr->pcidev;
	snprint(name, sizeof name, "%s (%s)", sdev->name, sdev->ifc->name);
	intrenable(p->intl, mpt2interrupt, ctlr, p->tbdf, name);

	ilock(ctlr);
	enablectlr(ctlr);
	iunlock(ctlr);

	if(!ctlr->enabled++){
		kproc("mpt2reset", mpt2reset, ctlr);
		kproc("mpt2event", mpt2event, ctlr);
	}

	if(waserror())
		print("mpt2enable: %s: %s\n", ctlr->name, up->errstr);
	else{
		portenable(ctlr);
		poperror();
	}
	return 0;
}

static int
mpt2disable(SDev *sdev)
{
	Ctlr *ctlr;
	Pcidev *p;
	char name[32];

	ctlr = sdev->ctlr;
	p = ctlr->pcidev;
	ilock(ctlr);
	disablectlr(ctlr);
	iunlock(ctlr);

	snprint(name, sizeof name, "%s (%s)", sdev->name, sdev->ifc->name);
	intrdisable(p->intl, mpt2interrupt, ctlr, p->tbdf, name);
	return 0;
}

static int
mpt2rio(SDreq *sdreq)
{
	Ctlr *ctlr;
	Unit *u;

	ctlr = sdreq->unit->dev->ctlr;
	u = UNIT(ctlr, sdreq->unit->subno);
	rlock(&ctlr->resetlock);
	if(waserror()){
		runlock(&ctlr->resetlock);
		return SDeio;
	}
	if(!unitonline(u))
		error(Ebadunit);
	if(waserror()){
		wakeup(&ctlr->reset);
		nexterror();
	}
	scsiio(ctlr, u, sdreq);
	poperror();
	poperror();
	runlock(&ctlr->resetlock);
	return sdreq->status;
}

static int
mpt2rctl(SDunit *unit, char *p, int l)
{
	Ctlr *ctlr;
	Unit *u;
	char *o, *e;
	char buf[48];

	ctlr = unit->dev->ctlr;
	if(ctlr == nil)
		return 0;
	o = p;
	e = p + l;
	u = UNIT(ctlr, unit->subno);
	getunitcaps(u, buf, sizeof buf);
	p = seprint(p, e, "capabilities %s\n", buf);
	p = seprint(p, e, "wwn %ullx\n", L64GET(u->wwn));
	p = seprint(p, e, "type %s\n", unittype(u));
	p = seprint(p, e, "status %s\n", unitstatus(u));
	p = seprint(p, e, "link %s\n", unitlink(u));
	p = seprint(p, e, "geometry %ulld %uld\n",
	    unit->sectors, unit->secsize);
	return p - o;
}

static char *
mpt2rtopctl(SDev *sdev, char *p, char *e)
{
	Ctlr *ctlr;

	ctlr = sdev->ctlr;
	return seprint(p, e, "%s mpt2 %s port %ux irq %d\n",
	    sdev->name, ctlr->name, ctlr->port, ctlr->pcidev->intl);
}

SDifc sdmpt2ifc = {
	"mpt2",				/* name */

	mpt2pnp,			/* pnp */
	nil,				/* legacy */
	mpt2enable,			/* enable */
	mpt2disable,			/* disable */

	scsiverify,			/* verify */
	scsionline,			/* online */
	mpt2rio,			/* rio */
	mpt2rctl,			/* rctl */
	nil,				/* wctl */

	scsibio,			/* bio */
	nil,				/* probe */
	nil,				/* clear */
	mpt2rtopctl,			/* rtopctl */
	nil,				/* wtopctl */
};
