#include <u.h>
#include <libc.h>
#include <bin.h>
#include <httpd.h>

/*
 *  go from http with escapes to utf,
 */
char *
httpunesc(HConnect *cc, char *s)
{
	char *t, *v, *p;
	int c;
	Rune r;
	Htmlesc *e;

	v = halloc(cc, UTFmax*strlen(s) + 1);
	for(t = v; c = *s;){
		if(c == '&'){
			if(s[1] == '#' && (c = strtoul(s+1, &p, 10)) != 0 && *p == ';'){
				r = c;
				t += runetochar(t, &r);
				s = p+1;
			} else {
				for(e = htmlesc; e->name != nil; e++)
					if(strncmp(e->name, s, strlen(e->name)) == 0)
						break;
				if(e->name != nil){
					t += runetochar(t, &e->value);
					s += strlen(e->name);
					continue;
				}
			}
		}
		*t++ = c;
		s++;
	}
	*t = 0;
	return v;
}
