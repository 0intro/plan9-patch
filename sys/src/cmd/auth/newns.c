#include <u.h>
#include <libc.h>
#include <auth.h>


void
usage(void)
{
	fprint(2, "usage: newns [-ad] [-n namespace] [cmd [args...]]\n");
	exits("usage");
}

void
main(int argc, char **argv)
{
	extern int newnsdebug;
	char *nsfile;
	char *defargv[] = { "/bin/rc", "-i", nil };
	int  add = 0;

	nsfile = "/lib/namespace";
	ARGBEGIN{
	case 'a':
		add = 1;
		break;
	case 'd':
		newnsdebug = 1;
		break;
	case 'n':
		nsfile = ARGF();
		break;
	default:
		usage();
		break;
	}ARGEND
	if(argc == 0)
		argv = defargv;
	if (add)
		addns(getuser(), nsfile);
	else
		newns(getuser(), nsfile);
	exec(argv[0], argv);
	exec(smprint("/bin/%s", argv[0]), argv);	// try /bin/...
	exec(argv[0], argv);			// recover error message
	sysfatal("exec: %s: %r", argv[0]);
}	
