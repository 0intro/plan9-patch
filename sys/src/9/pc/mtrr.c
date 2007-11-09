#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

typedef struct Mtrreg Mtrreg;
typedef struct Mtrrop Mtrrop;

struct Mtrreg {
	vlong base;
	vlong mask;
};

struct Mtrrop {
	Mtrreg *reg;
	int slot;
};

enum {
	Uncacheable = 0,
	Writecomb = 1,
	Unknown1 = 2,
	Unknown2 = 3,
	Writethru = 4,
	Writeprot = 5,
	Writeback = 6
};

enum {
	Capvcnt = 0xff,
	Capwc = 1<<8,
	Capfix = 1<<10,
	Deftype = 0xff,
	Deffixena = 1<<10,
	Defena = 1<<11,
};

static char *types[] = {
[Uncacheable]	"uc",
[Writecomb]	"wc",
[Unknown1]	"uk1",
[Unknown2]	"uk2",
[Writethru]	"wt",
[Writeprot]	"wp",
[Writeback]	"wb",
			nil
};

static char *
type2str(int type)
{
	if(type < 0 || type >= nelem(types))
		return nil;
	return types[type];
}

static int
str2type(char *str)
{
	char **p;
	for(p = types; *p != nil; p++)
		if(!strcmp(str, *p))
			return p - types;
	return -1;
}

static vlong
physmask(void)
{
	ulong ax;
	cpuid(0x80000000, &ax, nil, nil, nil);
	if(ax < 0x80000008)
		return 0xFFFFFFFFFLL;	/* cpu does not tell. default to 36bit phys */
	cpuid(0x80000008, &ax, nil, nil, nil);
	return (1LL << (ax & 0xFF)) - 1;
}

static int
overlap(uintptr b1, long s1, uintptr b2, long s2)
{
	if(b1 > b2)
		return overlap(b2, s2, b1, s1);
	if(b1 + s1 > b2)
		return 1;
	return 0;
}

static int
ctpop(vlong a)
{	
	int i;
	i = 0;
	while(a > 0){
		if(a & 1)
			i++;
		a >>= 1;
	}
	return i;
}

static void
mtrrdec(Mtrreg *mtrr, uintptr *ptr, long *size, int *type, int *valid)
{
	if(ptr != nil) *ptr = mtrr->base & ~4095LL;
	if(type != nil) *type = mtrr->base & 0xff;
	if(size != nil) *size = (physmask() ^ (mtrr->mask & ~4095LL)) + 1;
	if(valid != nil) *valid = (mtrr->mask >> 11) & 1;
}

static void
mtrrenc(Mtrreg *mtrr, uintptr ptr, long size, int type, int valid)
{
	mtrr->base = ptr;
	mtrr->base |= type & 0xff;
	mtrr->mask = physmask() & ~(size - 1);
	mtrr->mask |= valid ? 1<<11 : 0;
}

static void
mtrrget(Mtrreg *mtrr, int i)
{
	rdmsr(0x200 + 2*i, &mtrr->base);
	rdmsr(0x200 + 2*i + 1, &mtrr->mask);
}

static void
mtrrput(Mtrreg *mtrr, int i)
{
	wrmsr(0x200 + 2*i, mtrr->base);
	wrmsr(0x200 + 2*i + 1, mtrr->mask);
}

static void
mtrrop(Mtrrop **op)
{
	vlong def;
	ulong cr0;
	ulong cr4;
	int pl;
	static long bar1, bar2;

	pl = splhi();

/*iprint("cpu%d enter mtrrop\n", m->machno);*/
	_xinc(&bar1);
	while(bar1 < conf.nmach)
		microdelay(10);

	cr4 = getcr4();
	putcr4(cr4 & ~(1<<7));	
	cr0 = getcr0();
	wbinvd();
	putcr0(cr0 | (1<<30));
	wbinvd();
	rdmsr(0x2FF, &def);
	wrmsr(0x2FF, def & ~(vlong)Defena);
	mtrrput((*op)->reg, (*op)->slot);
	wbinvd();
	wrmsr(0x2FF, def);
	putcr0(cr0);
	putcr4(cr4);

	_xinc(&bar2);
	while(bar2 < conf.nmach)
		microdelay(10);
	*op = nil;
	_xdec(&bar1);
	while(bar1 > 0)
		microdelay(10);
	_xdec(&bar2);
	splx(pl);
}

static Mtrrop *postedop;

void
mtrrcheck(void)
{
	if(postedop != nil)
		mtrrop(&postedop);
}

int
mtrr(uintptr base, long size, char *tstr)
{
	Mtrrop op;
	Mtrreg entry;
	vlong def, cap;
	int i, vcnt, slot, type, x;
	static int tickreg;
	static QLock mtrrlk;

	if((m->cpuiddx & Mtrr) == 0)
		error("mtrr not supported");

	if((base & 4095LL) != 0 || (size & 4095LL) != 0 || size <= 0)
		error("mtrr base and/or size not 4k aligned or size <= 0");

	if(base + size < base)
		error("mtrr range overlaps 4G");

	if(ctpop(size) != 1)
		error("mtrr size not power of 2");

	if((base & (size - 1)) != 0)
		error("mtrr base not naturally aligned");

	if((type = str2type(tstr)) == -1)
		error("mtrr invalid type");

	rdmsr(0x0FE, &cap);
	rdmsr(0x2FF, &def);

	switch(type){
	default:
		error("mtrr unknown type");
	case Writecomb:
		if((cap & Capwc) == 0)
			error("mtrr type wc (write combining) unsupported");
		/* fallthrough */
	case Uncacheable:
	case Writethru:
	case Writeprot:
	case Writeback:
		break;
	}

	slot = -1;
	vcnt = cap & Capvcnt;
	for(i = 0; i < vcnt; i++){
		Mtrreg mtrr;
		uintptr mp;
		long msize;
		int mtype, mvalid;
		mtrrget(&mtrr, i);
		mtrrdec(&mtrr, &mp, &msize, &mtype, &mvalid);
		if(!mvalid)
			slot = i;
		if(mvalid && mp == base && msize == size){
			slot = i;
			goto doit;
		}
		if(mvalid && overlap(mp, msize, base, size))
			error("mtrr range overlaps with existing definition");
	}
	if(slot == -1)
		error("no free mtrr slots");

doit:

	qlock(&mtrrlk);

	mtrrenc(&entry, base, size, type, 1);
	op.reg = &entry;
	op.slot = slot;
	postedop = &op;

	x = splhi();	/* avoid race with mtrrcheck */
	mtrrop(&postedop);
	splx(x);

	qunlock(&mtrrlk);

	return 0;
}

int
mtrrprint(char *buf, long bufsize)
{
	Mtrreg mtrr;
	vlong cap, def;
	long n;
	int i, vcnt;

	n = 0;
	if(m->cpuiddx & Mtrr){
		rdmsr(0x0FE, &cap);
		rdmsr(0x2FF, &def);
		n += snprint(buf+n, bufsize-n, "cache default %s\n", type2str(def & Deftype));
		vcnt = cap & Capvcnt;
		for(i = 0; i < vcnt; i++){
			uintptr base;
			long size;
			int type, valid;
			mtrrget(&mtrr, i);
			mtrrdec(&mtrr, &base, &size, &type, &valid);
			if(valid)
				n += snprint(buf+n, bufsize-n, "cache 0x%lux %lud %s\n", base, size, type2str(type));
		}
	}
	return n;
}
