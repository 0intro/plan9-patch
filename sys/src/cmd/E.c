#include <u.h>
#include <libc.h>

ulong
mtime(char *file)
{
	Dir *d;
	ulong mtime;

	d = dirstat(file);
	if(d == nil)
		sysfatal("can't dirstat file: %r");
	mtime = d->mtime;
	free(d);
	return mtime;
}

void
usage(void)
{
	fprint(2, "usage: E file ...\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	char *file;
	ulong otime;

	if(argc < 2)
		usage();
	file = argv[--argc];
	otime = mtime(file);
	if(fork() == 0)
		exec("/bin/B", argv);
	waitpid();
	for(;;){
		if(mtime(file) > otime)
			break;
		sleep(1000);
	}
	exits(nil);
}
