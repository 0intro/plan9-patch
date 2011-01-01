#include <u.h>
#include <libc.h>
#include <bio.h>
#include <libsec.h>

#pragma	varargck	type	"M"	uchar*

static DS* (*shafunc)(uchar *data, ulong dlen, uchar *digest, DS *state);
static int shadlen;

static int
digestfmt(Fmt *fmt)
{
	char buf[SHA2_512dlen*2 + 1];
	uchar *p;
	int i;

	p = va_arg(fmt->args, uchar*);
	for(i = 0; i < shadlen; i++)
		sprint(buf + 2*i, "%.2ux", p[i]);
	return fmtstrcpy(fmt, buf);
}

static void
sum(int fd, char *name)
{
	int n;
	uchar buf[8192], digest[SHA2_512dlen];
	DigestState *s;

	s = (*shafunc)(nil, 0, nil, nil);
	while((n = read(fd, buf, sizeof buf)) > 0)
		(*shafunc)(buf, n, nil, s);
	if(n < 0){
		fprint(2, "reading %s: %r\n", name? name: "stdin");
		return;
	}
	(*shafunc)(nil, 0, digest, s);
	if(name == nil)
		print("%M\n", digest);
	else
		print("%M\t%s\n", digest, name);
}

void
main(int argc, char *argv[])
{
	int i, fd;

	shafunc = sha2_224;
	shadlen = SHA2_224dlen;
	ARGBEGIN{
	default:
		fprint(2, "usage: %s [file...]\n", argv0);
		exits("usage");
	}ARGEND

	fmtinstall('M', digestfmt);

	if(argc == 0)
		sum(0, nil);
	else
		for(i = 0; i < argc; i++){
			fd = open(argv[i], OREAD);
			if(fd < 0){
				fprint(2, "%s: can't open %s: %r\n", argv0, argv[i]);
				continue;
			}
			sum(fd, argv[i]);
			close(fd);
		}
	exits(nil);
}
