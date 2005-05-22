#include <u.h>
#include <libc.h>

void
main(int argc, char **argv)
{
	int fd;
	char dir[512], *str;

	fd = open("/dev/acme/ctl", OWRITE);
	if(fd < 0)
		exits(0);
	getwd(dir, 512);
	fprint(fd, "name %s%s-%s\n", dir,
					(dir[strlen(dir) - 1] != '/') ? "/" : "",
					(argc > 1) ? argv[1] : "rc");
	fprint(fd, "dumpdir %s\n", dir);
	exits(0);
}
