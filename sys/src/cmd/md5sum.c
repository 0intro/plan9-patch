#include <u.h>
#include <libc.h>
#include <bio.h>
#include <libsec.h>

#pragma	varargck	type	"M"	uchar*

static int
digestfmt(Fmt *fmt)
{
	char buf[MD5dlen*2+1];
	uchar *p;
	int i;

	p = va_arg(fmt->args, uchar*);
	for(i=0; i<MD5dlen; i++)
		sprint(buf+2*i, "%.2ux", p[i]);
	return fmtstrcpy(fmt, buf);
}

static int
sum(int fd, char *name)
{
	int n;
	uchar buf[8192], digest[MD5dlen];
	DigestState *s;

	s = md5(nil, 0, nil, nil);
	while((n = read(fd, buf, sizeof buf)) > 0)
		md5(buf, n, nil, s);
	if(n < 0){
		werrstr("reading %s: %r", name ? name : "stdin");
		return -1;
	}
	md5(nil, 0, digest, s);
	if(name == nil)
		print("%M\n", digest);
	else
		print("%M\t%s\n", digest, name);
	return 0;
}

void
main(int argc, char *argv[])
{
	int i, fd;
	char exitstr[ERRMAX];

	ARGBEGIN{
	default:
		fprint(2, "usage: md5sum [file...]\n");
		exits("usage");
	}ARGEND

	fmtinstall('M', digestfmt);

	exitstr[0] = '\0';
	if(argc == 0)
		sum(0, nil);
	else for(i = 0; i < argc; i++){
		fd = open(argv[i], OREAD);
		if(fd < 0){
			fprint(2, "%s: can't open %s: %r\n", argv0, argv[i]);
			rerrstr(exitstr, sizeof(exitstr));
			continue;
		}
		if(sum(fd, argv[i]) < 0){
			fprint(2, "%s: %r\n", argv0);
			rerrstr(exitstr, sizeof(exitstr));
		}
		close(fd);
	}
	exits(exitstr);
}
