#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <bio.h>

enum
{
	SSIZE = 10,

	/* list types */
	Lordered = 0,
	Lunordered,
	Lmenu,
	Ldir,

};

Biobuf in, out;
int lastc = '\n';
int inpre = 0;

/* stack for fonts */
char *fontstack[SSIZE];
char *font = "R";
int fsp;

/* stack for lists */
struct
{
	int	type;
	int	ord;
} liststack[SSIZE];
int lsp;

int quoting;

typedef struct Goobie Goobie;
struct Goobie
{
	char *name;
	void (*f)(Goobie*, char*);
	void (*ef)(Goobie*, char*);
};

void	eatwhite(void);
void	escape(void);

typedef void Action(Goobie*, char*);

Action	g_ignore;
Action	g_unexpected;
Action	g_title;
Action	g_p;
Action	g_h;
Action	g_li;
Action	g_list, g_listend;
Action	g_pre;
Action	g_fpush, g_fpop;
Action	g_indent, g_exdent;
Action	g_dt;
Action	g_display;
Action	g_displayend;
Action	g_table, g_tableend, g_caption, g_captionend;
Action	g_br, g_hr;

Goobie gtab[] =
{
	"!--",		g_ignore,	g_unexpected,
	"!doctype",	g_ignore,	g_unexpected,
	"a",		g_ignore,	g_ignore,
	"address",	g_display,	g_displayend,
	"b",		g_fpush,	g_fpop,
	"base",		g_ignore,	g_unexpected,
	"blink",	g_ignore,	g_ignore,
	"blockquote",	g_ignore,	g_ignore,
	"body",		g_ignore,	g_ignore,
	"br",		g_br,		g_unexpected,
	"caption",	g_caption,	g_captionend,
	"center",	g_ignore,	g_ignore,
	"cite",		g_ignore,	g_ignore,
	"code",		g_ignore,	g_ignore,
	"dd",		g_ignore,	g_unexpected,
	"dfn",		g_ignore,	g_ignore,
	"dir",		g_list,		g_listend,
	"div",		g_ignore,	g_ignore,
	"dl",		g_indent,	g_exdent,
	"dt",		g_dt,		g_unexpected,
	"em",		g_ignore,	g_ignore,
	"font",		g_ignore,	g_ignore,
	"form",		g_ignore,	g_ignore,
	"h1",		g_h,		g_p,
	"h2",		g_h,		g_p,
	"h3",		g_h,		g_p,
	"h4",		g_h,		g_p,
	"h5",		g_h,		g_p,
	"h6",		g_h,		g_p,
	"head",		g_ignore,	g_ignore,
	"hr",		g_hr,		g_unexpected,
	"html",		g_ignore,	g_ignore,
	"i",		g_fpush,	g_fpop,
	"input",	g_ignore,	g_unexpected,
	"img",		g_ignore,	g_unexpected,
	"isindex",	g_ignore,	g_unexpected,
	"kbd",		g_fpush,	g_fpop,
	"key",		g_ignore,	g_ignore,
	"li",		g_li,		g_ignore,
	"link",		g_ignore,	g_unexpected,
	"listing",	g_ignore,	g_ignore,
	"menu",		g_list,		g_listend,
	"meta",		g_ignore,	g_unexpected,
	"nextid",	g_ignore,	g_unexpected,
	"ol",		g_list,		g_listend,
	"option",	g_ignore,	g_unexpected,
	"p",		g_p,		g_ignore,
	"plaintext",	g_ignore,	g_unexpected,
	"pre",		g_pre,		g_displayend,
	"script",	g_ignore,	g_ignore,
	"samp",		g_ignore,	g_ignore,
	"select",	g_ignore,	g_ignore,
	"span",		g_ignore,	g_ignore,
	"strike",	g_ignore,	g_ignore,
	"strong",	g_ignore,	g_ignore,
	"table",	g_table,	g_tableend,
	"textarea",	g_ignore,	g_ignore,
	"title",	g_title,	g_ignore,
	"tt",		g_fpush,	g_fpop,
	"u",		g_ignore,	g_ignore,
	"ul",		g_list,		g_listend,
	"var",		g_ignore,	g_ignore,
	"xmp",		g_ignore,	g_ignore,
	0,		0,	0,
};

typedef struct Entity Entity;
struct Entity
{
	char *name;
	Rune value;
};

Entity pl_entity[]=
{
	{ "quot",	34 },
	{ "amp",	38 },
	{ "lt",		60 },
	{ "gt",		62 },
	{ "nbsp",	160 },
	{ "iexcl",	161 },
	{ "cent",	162 },
	{ "pound",	163 },
	{ "curren",	164 },
	{ "yen",	165 },
	{ "brvbar",	166 },
	{ "sect",	167 },
	{ "uml",	168 },
	{ "copy",	169 },
	{ "ordf",	170 },
	{ "laquo",	171 },
	{ "not",	172 },
	{ "shy",	173 },
	{ "reg",	174 },
	{ "macr",	175 },
	{ "deg",	176 },
	{ "plusmn",	177 },
	{ "sup2",	178 },
	{ "sup3",	179 },
	{ "acute",	180 },
	{ "micro",	181 },
	{ "para",	182 },
	{ "middot",	183 },
	{ "cedil",	184 },
	{ "sup1",	185 },
	{ "ordm",	186 },
	{ "raquo",	187 },
	{ "frac14",	188 },
	{ "frac12",	189 },
	{ "frac34",	190 },
	{ "iquest",	191 },
	{ "Agrave",	192 },
	{ "Aacute",	193 },
	{ "Acirc",	194 },
	{ "Atilde",	195 },
	{ "Auml",	196 },
	{ "Aring",	197 },
	{ "AElig",	198 },
	{ "Ccedil",	199 },
	{ "Egrave",	200 },
	{ "Eacute",	201 },
	{ "Ecirc",	202 },
	{ "Euml",	203 },
	{ "Igrave",	204 },
	{ "Iacute",	205 },
	{ "Icirc",	206 },
	{ "Iuml",	207 },
	{ "ETH",	208 },
	{ "Ntilde",	209 },
	{ "Ograve",	210 },
	{ "Oacute",	211 },
	{ "Ocirc",	212 },
	{ "Otilde",	213 },
	{ "Ouml",	214 },
	{ "times",	215 },
	{ "Oslash",	216 },
	{ "Ugrave",	217 },
	{ "Uacute",	218 },
	{ "Ucirc",	219 },
	{ "Uuml",	220 },
	{ "Yacute",	221 },
	{ "THORN",	222 },
	{ "szlig",	223 },
	{ "agrave",	224 },
	{ "aacute",	225 },
	{ "acirc",	226 },
	{ "atilde",	227 },
	{ "auml",	228 },
	{ "aring",	229 },
	{ "aelig",	230 },
	{ "ccedil",	231 },
	{ "egrave",	232 },
	{ "eacute",	233 },
	{ "ecirc",	234 },
	{ "euml",	235 },
	{ "igrave",	236 },
	{ "iacute",	237 },
	{ "icirc",	238 },
	{ "iuml",	239 },
	{ "eth",	240 },
	{ "ntilde",	241 },
	{ "ograve",	242 },
	{ "oacute",	243 },
	{ "ocirc",	244 },
	{ "otilde",	245 },
	{ "ouml",	246 },
	{ "divide",	247 },
	{ "oslash",	248 },
	{ "ugrave",	249 },
	{ "uacute",	250 },
	{ "ucirc",	251 },
	{ "uuml",	252 },
	{ "yacute",	253 },
	{ "thorn",	254 },
	{ "yuml",	255 },
	{ "OElig",	338 },
	{ "oelig",	339 },
	{ "Scaron",	352 },
	{ "scaron",	353 },
	{ "Yuml",	376 },
	{ "fnof",	402 },
	{ "circ",	710 },
	{ "tilde",	732 },
	{ "Alpha",	913 },
	{ "Beta",	914 },
	{ "Gamma",	915 },
	{ "Delta",	916 },
	{ "Epsilon",	917 },
	{ "Zeta",	918 },
	{ "Eta",	919 },
	{ "Theta",	920 },
	{ "Iota",	921 },
	{ "Kappa",	922 },
	{ "Lambda",	923 },
	{ "Mu",		924 },
	{ "Nu",		925 },
	{ "Xi",		926 },
	{ "Omicron",	927 },
	{ "Pi",		928 },
	{ "Rho",	929 },
	{ "Sigma",	931 },
	{ "Tau",	932 },
	{ "Upsilon",	933 },
	{ "Phi",	934 },
	{ "Chi",	935 },
	{ "Psi",	936 },
	{ "Omega",	937 },
	{ "alpha",	945 },
	{ "beta",	946 },
	{ "gamma",	947 },
	{ "delta",	948 },
	{ "epsilon",	949 },
	{ "zeta",	950 },
	{ "eta",	951 },
	{ "theta",	952 },
	{ "iota",	953 },
	{ "kappa",	954 },
	{ "lambda",	955 },
	{ "mu",		956 },
	{ "nu",		957 },
	{ "xi",		958 },
	{ "omicron",	959 },
	{ "pi",		960 },
	{ "rho",	961 },
	{ "sigmaf",	962 },
	{ "sigma",	963 },
	{ "tau",	964 },
	{ "upsilon",	965 },
	{ "phi",	966 },
	{ "chi",	967 },
	{ "psi",	968 },
	{ "omega",	969 },
	{ "thetasym",	977 },
	{ "upsih",	978 },
	{ "piv",	982 },
	{ "ensp",	8194 },
	{ "emsp",	8195 },
	{ "thinsp",	8201 },
	{ "zwnj",	8204 },
	{ "zwj",	8205 },
	{ "lrm",	8206 },
	{ "rlm",	8207 },
	{ "ndash",	8211 },
	{ "mdash",	8212 },
	{ "lsquo",	8216 },
	{ "rsquo",	8217 },
	{ "sbquo",	8218 },
	{ "ldquo",	8220 },
	{ "rdquo",	8221 },
	{ "bdquo",	8222 },
	{ "dagger",	8224 },
	{ "Dagger",	8225 },
	{ "bull",	8226 },
	{ "hellip",	8230 },
	{ "permil",	8240 },
	{ "prime",	8242 },
	{ "Prime",	8243 },
	{ "lsaquo",	8249 },
	{ "rsaquo",	8250 },
	{ "oline",	8254 },
	{ "frasl",	8260 },
	{ "euro",	8364 },
	{ "image",	8465 },
	{ "weierp",	8472 },
	{ "real",	8476 },
	{ "trade",	8482 },
	{ "alefsym",	8501 },
	{ "larr",	8592 },
	{ "uarr",	8593 },
	{ "rarr",	8594 },
	{ "darr",	8595 },
	{ "harr",	8596 },
	{ "crarr",	8629 },
	{ "lArr",	8656 },
	{ "uArr",	8657 },
	{ "rArr",	8658 },
	{ "dArr",	8659 },
	{ "hArr",	8660 },
	{ "forall",	8704 },
	{ "part",	8706 },
	{ "exist",	8707 },
	{ "empty",	8709 },
	{ "nabla",	8711 },
	{ "isin",	8712 },
	{ "notin",	8713 },
	{ "ni",		8715 },
	{ "prod",	8719 },
	{ "sum",	8721 },
	{ "minus",	8722 },
	{ "lowast",	8727 },
	{ "radic",	8730 },
	{ "prop",	8733 },
	{ "infin",	8734 },
	{ "ang",	8736 },
	{ "and",	8743 },
	{ "or",		8744 },
	{ "cap",	8745 },
	{ "cup",	8746 },
	{ "int",	8747 },
	{ "there4",	8756 },
	{ "sim",	8764 },
	{ "cong",	8773 },
	{ "asymp",	8776 },
	{ "ne",		8800 },
	{ "equiv",	8801 },
	{ "le",		8804 },
	{ "ge",		8805 },
	{ "sub",	8834 },
	{ "sup",	8835 },
	{ "nsub",	8836 },
	{ "sube",	8838 },
	{ "supe",	8839 },
	{ "oplus",	8853 },
	{ "otimes",	8855 },
	{ "perp",	8869 },
	{ "sdot",	8901 },
	{ "lceil",	8968 },
	{ "rceil",	8969 },
	{ "lfloor",	8970 },
	{ "rfloor",	8971 },
	{ "lang",	9001 },
	{ "rang",	9002 },
	{ "loz",	9674 },
	{ "spades",	9824 },
	{ "clubs",	9827 },
	{ "hearts",	9829 },
	{ "diams",	9830 },
	{ nil,		0 },
};

int
cistrcmp(char *a, char *b)
{
	int c, d;

	for(;; a++, b++){
		d = tolower(*a);
		c = d - tolower(*b);
		if(c)
			break;
		if(d == 0)
			break;
	}
	return c;
}

int
readupto(char *buf, int n, char d, char notme)
{
	char *p;
	int c;

	buf[0] = 0;
	for(p = buf;; p++){
		c = Bgetc(&in);
		if(c < 0){
			*p = 0;
			return -1;
		}
		if(c == notme){
			Bungetc(&in);
			return -1;
		}
		if(c == d){
			*p = 0;
			return 0;
		}
		*p = c;
		if(p == buf + n){
			*p = 0;
			Bprint(&out, "<%s", buf);
			return -1;
		}
	}
}

void
dogoobie(void)
{
	char *arg, *type;
	Goobie *g;
	char buf[1024];
	int closing;

	if(readupto(buf, sizeof(buf), '>', '<') < 0){
		Bprint(&out, "<%s", buf);
		return;
	}
	type = buf;
	if(*type == '/'){
		type++;
		closing = 1;
	} else
		closing = 0;
	arg = strchr(type, ' ');
	if(arg == 0)
		arg = strchr(type, '\r');
	if(arg == 0)
		arg = strchr(type, '\n');
	if(arg)
		*arg++ = 0;
	for(g = gtab; g->name; g++)
		if(cistrcmp(type, g->name) == 0){
			if(closing){
				if(g->ef){
					(*g->ef)(g, arg);
					return;
				}
			} else {
				if(g->f){
					(*g->f)(g, arg);
					return;
				}
			}
		}
	if(closing)
		type--;
	if(arg)
		Bprint(&out, "<%s %s>\n", type, arg);
	else
		Bprint(&out, "<%s>\n", type);
}

void
main(int, char *argv[])
{
	int c, pos;

	argv0 = argv[0];
	Binit(&in, 0, OREAD);
	Binit(&out, 1, OWRITE);

	pos = 0;
	for(;;){
		c = Bgetc(&in);
		if(c < 0)
			return;
		switch(c){
		case '<':
			dogoobie();
			break;
		case '&':
			escape();
			break;
		case '\\':
			Bprint(&out, "\\(bs");
			break;
		case '\r':
			pos = 0;
			break;
		case '\n':
			if(quoting){
				Bputc(&out, '"');
				quoting = 0;
			}
			if(lastc != '\n')
				Bputc(&out, '\n');
			/* can't emit leading spaces in filled troff docs */
			if (!inpre)
				eatwhite();
			lastc = c;
			break;
		default:
			++pos;
			if(!inpre && isascii(c) && isspace(c) && pos > 80){
				Bputc(&out, '\n');
				eatwhite();
				pos = 0;
			}else
				Bputc(&out, c);
			lastc = c;
			break;
		}
	}
}

void
escape(void)
{
	int c;
	Entity *e;
	char buf[8];

	if(readupto(buf, sizeof(buf), ';', '\n') < 0){
		Bprint(&out, "&%s", buf);
		return;
	}
	for(e = pl_entity; e->name; e++)
		if(strcmp(buf, e->name) == 0){
			Bprint(&out, "%C", e->value);
			return;
		}
	if(*buf == '#'){
		c = strtol(buf+1, nil, 10);
		Bprint(&out, "%C", c);
		return;
	}
	fprint(2, "%s: unknown entity reference &%s;\n", argv0, buf);
	Bprint(&out, "&%s;", buf);
}

/*
 * whitespace is not significant to HTML, but newlines
 * and leading spaces are significant to troff.
 */
void
eatwhite(void)
{
	int c;

	for(;;){
		c = Bgetc(&in);
		if(c < 0)
			break;
		if(!isspace(c)){
			Bungetc(&in);
			break;
		}
	}
}

/*
 *  print at start of line
 */
void
printsol(char *fmt, ...)
{
	va_list arg;

	if(quoting){
		Bputc(&out, '"');
		quoting = 0;
	}
	if(lastc != '\n')
		Bputc(&out, '\n');
	va_start(arg, fmt);
	Bvprint(&out, fmt, arg);
	va_end(arg);
	lastc = '\n';
}

void
g_ignore(Goobie *g, char *arg)
{
	USED(g, arg);
}

void
g_unexpected(Goobie *g, char *arg)
{
	USED(arg);
	fprint(2, "%s: unexpected %s ending\n", argv0, g->name);
}

void
g_title(Goobie *g, char *arg)
{
	USED(arg);
	printsol(".TL\n", g->name);
}

void
g_p(Goobie *g, char *arg)
{
	USED(arg);
	printsol(".LP\n", g->name);
}

void
g_h(Goobie *g, char *arg)
{
	USED(arg);
	printsol(".SH %c\n", g->name[1]);
}

void
g_list(Goobie *g, char *arg)
{
	USED(arg);

	if(lsp != SSIZE){
		switch(g->name[0]){
		case 'o':
			liststack[lsp].type  = Lordered;
			liststack[lsp].ord = 0;
			break;
		default:
			liststack[lsp].type = Lunordered;
			break;
		}
	}
	lsp++;
}

void
g_br(Goobie *g, char *arg)
{
	USED(g, arg);
	printsol(".br\n");
}

void
g_li(Goobie *g, char *arg)
{
	USED(g, arg);
	if(lsp <= 0 || lsp > SSIZE){
		printsol(".IP \\(bu\n");
		return;
	}
	switch(liststack[lsp-1].type){
	case Lunordered:
		printsol(".IP \\(bu\n");
		break;
	case Lordered:
		printsol(".IP %d\n", ++liststack[lsp-1].ord);
		break;
	}
}

void
g_listend(Goobie *g, char *arg)
{
	USED(g, arg);
	if(--lsp < 0)
		lsp = 0;
	printsol(".LP\n");
}

void
g_display(Goobie *g, char *arg)
{
	USED(g, arg);
	printsol(".DS\n");
}

void
g_pre(Goobie *g, char *arg)
{
	USED(g, arg);
	printsol(".DS L\n");
	inpre = 1;
}

void
g_displayend(Goobie *g, char *arg)
{
	USED(g, arg);
	printsol(".DE\n");
	inpre = 0;
}

void
g_fpush(Goobie *g, char *arg)
{
	USED(arg);
	if(fsp < SSIZE)
		fontstack[fsp] = font;
	fsp++;
	switch(g->name[0]){
	case 'b':
		font = "B";
		break;
	case 'i':
		font = "I";
		break;
	case 'k':		/* kbd */
	case 't':		/* tt */
		font = "(CW";
		break;
	}
	Bprint(&out, "\\f%s", font);
}

void
g_fpop(Goobie *g, char *arg)
{
	USED(g, arg);
	fsp--;
	if(fsp < SSIZE)
		font = fontstack[fsp];
	else
		font = "R";

	Bprint(&out, "\\f%s", font);
}

void
g_indent(Goobie *g, char *arg)
{
	USED(g, arg);
	printsol(".RS\n");
}

void
g_exdent(Goobie *g, char *arg)
{
	USED(g, arg);
	printsol(".RE\n");
}

void
g_dt(Goobie *g, char *arg)
{
	USED(g, arg);
	printsol(".IP \"");
	quoting = 1;
}

void
g_hr(Goobie *g, char *arg)
{
	USED(g, arg);
	printsol(".br\n");
	printsol("\\l'5i'\n");
}


/*
<table border>
<caption><font size="+1"><b>Cumulative Class Data</b></font></caption>
<tr><th rowspan=2>DOSE<br>mg/kg</th><th colspan=2>PARALYSIS</th><th colspan=2>DEATH</th>
</tr>
<tr><th width=80>Number</th><th width=80>Percent</th><th width=80>Number</th><th width=80>Percent</th>
</tr>
<tr align=center>
<td>0.1</td><td><br></td> <td><br></td> <td><br></td> <td><br></td>
</tr>
<tr align=center>
<td>0.2</td><td><br></td> <td><br></td> <td><br></td> <td><br></td>
</tr>
<tr align=center>
<td>0.3</td><td><br></td> <td><br></td> <td><br></td> <td><br></td>
</tr>
<tr align=center>
<td>0.4</td><td><br></td> <td><br></td> <td><br></td> <td><br></td>
</tr>
<tr align=center>
<td>0.5</td><td><br></td> <td><br></td> <td><br></td> <td><br></td>
</tr>
<tr align=center>
<td>0.6</td><td><br></td> <td><br></td> <td><br></td> <td><br></td>
</tr>
<tr align=center>
<td>0.7</td><td><br></td> <td><br></td> <td><br></td> <td><br></td>
</tr>
<tr align=center>
<td>0.8</td><td><br></td> <td><br></td> <td><br></td> <td><br></td>
</tr>
<tr align=center>
<td>0.8 oral</td><td><br></td> <td><br></td> <td><br></td> <td><br></td>
</tr>
</table>
*/

void
g_table(Goobie *g, char *arg)
{
	USED(g, arg);
	printsol(".TS\ncenter ;\n");
}

void
g_tableend(Goobie *g, char *arg)
{
	USED(g, arg);
	printsol(".TE\n");
}

void
g_caption(Goobie *g, char *arg)
{
	USED(g, arg);
}

void
g_captionend(Goobie *g, char *arg)
{
	USED(g, arg);
}
