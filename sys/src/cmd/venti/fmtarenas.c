#include "stdinc.h"
#include "dat.h"
#include "fns.h"

void
usage(void)
{
	fprint(2, "usage: fmtarenas [-Z] [-b blocksize] [-a arenasize | -n arenanum] name file\n");
	exits(0);
}

int
main(int argc, char *argv[])
{
	ArenaPart *ap;
	Part *part;
	Arena *arena;
	u64int addr, limit, asize, apsize;
	char *file, *name, aname[ANameSize];
	int i, anum, blockSize, tabSize, zero;

	fmtinstall('V', vtScoreFmt);
	fmtinstall('R', vtErrFmt);
	vtAttach();
	statsInit();

	blockSize = 8 * 1024;
	asize = 0;
	anum = 0;
	tabSize = 64 * 1024;		/* BUG: should be determine from number of arenas */
	zero = 1;
	ARGBEGIN{
	case 'a':
		if (anum > 0)
			usage();
		asize = unittoull(ARGF());
		if(asize == TWID64)
			usage();
		break;
	case 'b':
		blockSize = unittoull(ARGF());
		if(blockSize == ~0)
			usage();
		if(blockSize > MaxDiskBlock){
			fprint(2, "block size too large, max %d\n", MaxDiskBlock);
			exits("usage");
		}
		break;
	case 'n':
		if (asize > 0)
			usage();
		anum = unittoull(ARGF());
		if(anum == TWID64)
			usage();
		break;
	case 'Z':
		zero = 0;
		break;
	default:
		usage();
		break;
	}ARGEND

	if(argc != 2)
		usage();

	name = argv[0];
	file = argv[1];

	if(!nameOk(name))
		fatal("illegal name template %s", name);

	part = initPart(file, 1);
	if(part == nil)
		fatal("can't open partition %s: %r", file);

	if(zero)
		zeroPart(part, blockSize);

	ap = newArenaPart(part, blockSize, tabSize);
	if(ap == nil)
		fatal("can't initialize arena: %R");

	apsize = ap->size - ap->arenaBase;
	if (asize == 0 && anum == 0)
		asize = 512 * 1024 * 1024;
	if (anum == 0)
		anum = apsize / asize;
	else
		asize = apsize / anum;

	fprint(2, "configuring %s with arenas=%d for a total storage of bytes=%lld and directory bytes=%d\n",
		file, anum, apsize, ap->tabSize);

	ap->narenas = anum;
	ap->map = MKNZ(AMap, anum);
	ap->arenas = MKNZ(Arena*, anum);

	addr = ap->arenaBase;
	for(i = 0; i < anum; i++){
		limit = addr + asize;
		snprint(aname, ANameSize, "%s%d", name, i);
		fprint(2, "adding arena %s at [%lld,%lld)\n", aname, addr, limit);
		arena = newArena(part, aname, addr, limit - addr, blockSize);
		if(!arena)
			fprint(2, "can't make new arena %s: %r", aname);
		freeArena(arena);

		ap->map[i].start = addr;
		ap->map[i].stop = limit;
		nameCp(ap->map[i].name, aname);

		addr = limit;
	}

	if(!wbArenaPart(ap))
		fprint(2, "can't write back arena partition header for %s: %R\n", file);

	exits(0);
	return 0;	/* shut up stupid compiler */
}
