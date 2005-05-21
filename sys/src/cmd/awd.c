#include <u.h>
#include <libc.h>

void
main(int argc, char **argv)
{
	int fd, n;
	char dir[512], *str;

	fd = open("/dev/acme/ctl", OWRITE);
	if(fd < 0)
		exits(0);
	getwd(dir, 512);
	fprint(fd, "name %s", dir);
	n = strlen(dir);
	if(n>0 && dir[n-1]!='/')
		fprint(fd, "/");
	fprint(fd, "-");
	if(argc > 1)
		str = argv[1];
	else
		str = "rc";
	fprint(fd, "%s\n", str);
	fprint(fd, "dumpdir %s\n", dir);
	exits(0);
}
