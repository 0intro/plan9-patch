#include <u.h>
#include <libc.h>
#include <bio.h>

static char *delim = "\n";

int
openlog(char *name)
{
	int fd;

	fd = open(name, OWRITE);
	if(fd < 0)
		fd = create(name, OWRITE, DMAPPEND|0666);
	if(fd < 0){
		fprint(2, "%s: can't open %s: %r\n", argv0, name);
		return -1;
	}
	seek(fd, 0, 2);
	return fd;
}

void
usage (char *argv0)
{
		fprint(2, "usage: %s [-r] console logfile\n", argv0);
}

void
main(int argc, char **argv)
{
	Biobuf in;
	int fd;
	char *p, *t;
	char buf[8192];

	ARGBEGIN {
		case 'r':
			delim = "\r\n";
			break;
		default:
			usage(argv0);
			exits("usage");
	} ARGEND
	if(argc < 2){
		usage(argv0);
		exits("usage");
	}

	fd = open(argv[0], OREAD);
	if(fd < 0){
		fprint(2, "%s: can't open %s: %r\n", argv0, argv[0]);
		exits("open");
	}
	Binit(&in, fd, OREAD);

	fd = openlog(argv[1]);

	for(;;){
		if(p = Brdline(&in, '\n')){
			p[Blinelen(&in)-1] = 0;
			t = ctime(time(0));
			t[19] = 0;
			if(fprint(fd, "%s: %s%s", t, p, delim) < 0){
				close(fd);
				fd = openlog(argv[2]);
				fprint(fd, "%s: %s%s", t, p, delim);
			}
		} else if(Blinelen(&in) == 0)	// true eof
			break;
		else {
			Bread(&in, buf, sizeof buf);
		}
	}
	exits(0);
}
