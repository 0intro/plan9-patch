/*
 *  devtest 
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

enum {
	Qdir = 0,
	Qtest,
	Qmax,
};

typedef long Rdwrfn(Chan*, void*, long, vlong);

static Rdwrfn *readfn[Qmax];
static Rdwrfn *writefn[Qmax];

static Dirtab testdir[Qmax] = {
	".",		{ Qdir, 0, QTDIR },	0,	DMDIR | 0555,
};

int ntestdir = Qtest;

static Chan*
testattach(char* spec)
{
	return devattach('o', spec);
}

Walkqid*
testwalk(Chan* c, Chan *nc, char** name, int nname)
{
	return devwalk(c, nc, name, nname, testdir, ntestdir, devgen);
}

static int
teststat(Chan* c, uchar* dp, int n)
{
	return devstat(c, dp, n, testdir, ntestdir, devgen);
}

static Chan*
testopen(Chan* c, int omode)
{
	return devopen(c, omode, testdir, ntestdir, devgen);
}

static void
testclose(Chan*)
{
}

Dirtab*
addtestfile(char *name, int perm, Rdwrfn *rdfn, Rdwrfn *wrfn)
{
	int i;
	Dirtab d;
	Dirtab *dp;

	memset(&d, 0, sizeof d);
	strcpy(d.name, name);
	d.perm = perm;

	if(ntestdir >= Qmax)
		return nil;

	for(i=0; i<ntestdir; i++)
		if(strcmp(testdir[i].name, name) == 0)
			return nil;

	d.qid.path = ntestdir;
	testdir[ntestdir] = d;
	readfn[ntestdir] = rdfn;
	writefn[ntestdir] = wrfn;
	dp = &testdir[ntestdir++];

	return dp;
}

static long
testread(Chan *c, void *a, long n, vlong offset)
{
	USED(c); USED(a); USED(n); USED(offset);

	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, a, n, testdir, ntestdir, devgen);
	default:
		return 0;
	}
}

static long
testwrite(Chan *c, void *a, long n, vlong offset)
{
	ulong va;
	ulong fakephysaddr = 8192;
	char err[128];
	
	USED(c); USED(a); USED(n); USED(offset);

	va = (ulong)vmap(fakephysaddr, 32 * 1024 * 1024);

	if (va != 0)
		vunmap((void *)va, 32 * 1024 * 1024);
	else
		error(Enomem);

	snprint(err, sizeof(err), "testwrite: va 0x%lux", va);
	error(err);

	return n;
}

static void
testinit(void) {
	addtestfile("test", 0660, testread, testwrite);
}

Dev testdevtab = {
	'o',
	"test",

	devreset,
	testinit,
	devshutdown,
	testattach,
	testwalk,
	teststat,
	testopen,
	devcreate,
	testclose,
	testread,
	devbread,
	testwrite,
	devbwrite,
	devremove,
	devwstat,
};
