/* yenc file decoder
 *
 * Please read www.exit109.com/~jeremy/news/yenc.html
 * "Why Yenc is bad for Usenet" before adding an encoder.
 */
#include <u.h>
#include <libc.h>
#include <bio.h>
#include <flate.h>

static int Verbose = 0;
static ulong *Crctab;
static Biobuf bout;

int
yydec(char *file, Biobuf *bi)
{
	uchar c;
	char *name, *buf, *p, *q;
	static int totgot = 0;
	static int nextpart = 1;
	unsigned long crc, ecrc;
	int got, line, part, size, esize, begin, end;

	got = 0;
	crc = 0;
	part = 0;
	name = 0;
	while((buf = Brdline(bi, '\n')) != nil){
		if ((p = strchr(buf, '\n')) != nil)
			*p = 0;	
		if ((p = strchr(buf, '\r')) != nil)
			*p = 0;	
		if (strncmp(buf, "=ybegin ", 8)==0) {
			if ((p = strstr(buf,"name=")) == nil)
				sysfatal("%s - no name= in =ybegin\n", file);
			name = strdup(p+5);

			if ((p = strstr(buf,"size=")) == nil)
				sysfatal("%s - no size= in =ybegin\n", file);
			size = atoi(p+5);

			if ((p = strstr(buf,"line=")) == nil)
				sysfatal("%s - no line= in =ybegin\n", file);
			line = atoi(p+5);

			if ((p = strstr(buf,"part=")) != nil)
				part = atoi(p+5);
			break;
		}
	}

	if (part){
		if ((buf = Brdline(bi, '\n')) == nil)
			sysfatal("%s - unexpected EOF\n", file);
		if ((p = strchr(buf, '\n')) != nil)
			*p = 0;	
		if ((p = strchr(buf, '\r')) != nil)
			*p = 0;	
		if (strncmp(buf, "=ypart ", 7) != 0)
			sysfatal("%s - no =ypart in multipart file\n", file);

		if ((p = strstr(buf, "end=")) == nil)
			sysfatal("%s - no end= in =ypart\n", file);
		end = atol(p+4);

		if ((p = strstr(buf, "begin=")) == nil)
			sysfatal("%s - no begin= in =ypart\n", file);
		begin = atol(p+6);
	}

	if (name == nil)
		sysfatal("%s - =ybegin not found\n", file);

	if (Verbose && part == 0 || part == 1)
		fprint(2, "%s %d bytes\n", name, size);

	if (part && part != nextpart)
		sysfatal("%s - got part=%d, wanted part=%d\n", file, part, nextpart);

	while (1){
		if ((buf = Brdline(bi, '\n')) == nil)
			sysfatal("%s - unexpected EOF\n", file);
		if ((p = strchr(buf, '\n')) != nil)
			*p = 0;	
		if ((p = strchr(buf, '\r')) != nil)
			*p = 0;	
		if (strncmp(buf, "=yend ", 6) == 0)
			break;
		for (p = buf; *p;){
			c = (uchar)*p++;
			if (c == '='){
				if ((c = *p++) == 0)
					sysfatal("%s - Esc char (=) at end of line\n", file);
				c = (uchar)(c-64);
			}
			c = (uchar)(c-42);
			Bputc(&bout, c);
			crc = blockcrc(Crctab, crc, &c, 1);
			totgot++;
			got++;
		}
	}

	if ((p = strstr(buf, "size=")) != nil){
		esize = atoi(p+5);
		if (esize != got) {
			fprint(2, "%s corrupted size=%d != %d\n", file, esize, got);
			return 0;
		}
	}

	if (! part){
		if ((p = strstr(buf, "crc32=")) != nil)
			ecrc = strtoul((p+6), nil, 16);
		if (crc != ecrc)
			fprint(2, "%s: %s corrupt crc=%08lux != %08lux\n", argv0, file, crc, ecrc);
		return 0;
	}

	/*
	 * multipart only from here on
	 */
	if ((p = strstr(buf, "pcrc32=")) != nil)
		ecrc = strtoul((p+7), nil, 16);
	if (crc != ecrc)
			fprint(2, "%s: %s corrupt part crc=%08lux != %08lux\n", argv0, file, crc, ecrc);

	if (got != (end - begin + 1))
		sysfatal("%s part=%d wrong size\n", file, part);

	nextpart = part +1;
	return 0;
}

void
usage(void)
{
	fprint(2, "usage: %s [-v] [file...]\n", argv0);
	exits("usage");
}

void
main(int argc, char *argv[])
{
	Biobuf bin, *bi = &bin;

	Crctab = mkcrctab(0xedb88320UL);
	Binit(&bin, 0, OREAD);
	Binit(&bout, 1, OWRITE);
	argv0 = *argv;

	ARGBEGIN {
	case 'v':
		Verbose++;
		break;
	default:
		usage();
		break;
	} ARGEND;

	if (argc == 0)
		yydec(*argv, &bin);

	for (; argc--; argv++){
		if ((bi = Bopen(*argv, OREAD)) == nil){
			fprint(2, "%s: %s - cannot read file %r\n", argv0, *argv);
			continue;
		}
		yydec(*argv, bi);
		Bterm(bi);
	}
	Bterm(&bout);
	exits(0);
}
