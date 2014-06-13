/* find-trouble: find an 8K chunk of the fortune file that has a SHA1
 * hash that ends with a NUL byte so we can make a file containing
 * a bunch of that chunk so that we can exhibit Wes Filardo's
 * venti/copy bug
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <mp.h>
#include <libsec.h>

Biobufhdr bin;
uchar	binbuf[256*1024];

void
main(int argc, char *argv[])
{
	char fbuf[8192];
	uchar sha1digest[SHA1dlen];
	char *fortunepath = "/sys/games/lib/fortunes";
	int fortunes;
	int got;

	if ((fortunes = open(fortunepath, OREAD)) < 0)
		sysfatal("%s: %r", fortunepath);

	Binits(&bin, fortunes, OREAD, binbuf, sizeof binbuf);

	do {
		vlong pos;
		char *newline;

		pos = Boffset(&bin);

		if ((got = Bread(&bin, fbuf, sizeof fbuf)) <0)
			sysfatal("%s: read(): %r", fortunepath);

		sha1((uchar *)fbuf, sizeof fbuf, sha1digest, nil);

		if (sha1digest[SHA1dlen - 1] == 0){
			write(1, fbuf, sizeof fbuf);
			exits(nil);
		}
		/* May as well START on a fortune boundary (if we can) */
		if ((newline = strchr(fbuf, '\n')) != nil)
			Bseek(&bin, pos + (newline - fbuf + 1), 0);
		else
			Bseek(&bin, pos + 1, 0);
	} while (got == sizeof fbuf);
}
