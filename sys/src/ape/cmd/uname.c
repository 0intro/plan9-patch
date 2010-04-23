#include <stdlib.h>
#include <sys/utsname.h>
#include <stdio.h>

#define	ARGBEGIN	for((argv0=*argv),argv++,argc--;\
			    argv[0] && argv[0][0]=='-' && argv[0][1];\
			    argc--, argv++) {\
				char *_args, *_argt, _argc;\
				_args = &argv[0][1];\
				if(_args[0]=='-' && _args[1]==0){\
					argc--; argv++; break;\
				}\
				while(*_args) switch(_argc=*_args++)
#define	ARGEND		}
#define	ARGF()		(_argt=_args, _args="",\
				(*_argt? _argt: argv[1]? (argc--, *++argv): 0))
#define	ARGC()		_argc
char *argv0;

main(int argc, char **argv)
{
	char *pad;
	struct utsname u;

	uname(&u);
	if(argc == 1){
		printf("%s\n", u.sysname);
		exit(0);
	}
	pad = "";
	ARGBEGIN{
	case 'a':
		printf("%s%s %s %s %s %s", pad, u.sysname, u.nodename,
			u.release, u.version, u.machine);
		pad = " ";
		break;
	case 'm':
		printf("%s%s", pad, u.machine);
		pad = " ";
		break;
	case 'n':
		printf("%s%s", pad, u.nodename);
		pad = " ";
		break;
	case 'r':
		printf("%s%s", pad, u.release);
		pad = " ";
		break;
	case 's':
		printf("%s%s", pad, u.sysname);
		pad = " ";
		break;
	case 'v':
		printf("%s%s", pad, u.version);
		pad = " ";
		break;
	} ARGEND
	printf("\n");
	exit(0);
}
