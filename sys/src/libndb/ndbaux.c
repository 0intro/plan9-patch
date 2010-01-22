#include <u.h>
#include <libc.h>
#include <bio.h>
#include <ctype.h>
#include <ndb.h>
#include "ndbhf.h"

/*
 * strip quotes, deals with adjacent quotes
 * implies a single quote.
 * beware: modifies the source string
 */
static int
unquote(char **buf)
{
	char q, *base, *w, *r;

	base = *buf;
	q = *base++;
	r = w = base;
	while(*r && *r != '\n'){
		if(r[0] == q)
			if(r[1] == q)
				r++;
			else
				break;
		*w++ = *r++;
	}
	*buf = r;
	return w - base;
}


/*
 *  parse a single tuple
 */
char*
_ndbparsetuple(char *cp, Ndbtuple **tp)
{
	char *p;
	int len;
	Ndbtuple *t;

	/* a '#' starts a comment lasting till new line */
	EATWHITE(cp);
	if(*cp == '#' || *cp == '\n')
		return 0;

	t = ndbnew(nil, nil);
	setmalloctag(t, getcallerpc(&cp));
	*tp = t;

	/* parse attribute */
	p = cp;
	while(*cp != '=' && !ISWHITE(*cp) && *cp != '\n')
		cp++;
	len = cp - p;
	if(len >= Ndbalen)
		len = Ndbalen-1;
	strncpy(t->attr, p, len);

	/* parse value */
	EATWHITE(cp);
	if(*cp == '='){
		cp++;
		if(*cp == '"' || *cp == '\''){
			p = cp+1;
			len = unquote(&cp);
		} else if(*cp == '#'){
			len = 0;
		} else {
			p = cp;
			while(!ISWHITE(*cp) && *cp != '\n')
				cp++;
			len = cp - p;
		}
		ndbsetval(t, p, len);
	}

	return cp;
}

/*
 *  parse all tuples in a line.  we assume that the 
 *  line ends in a '\n'.
 *
 *  the tuples are linked as a list using ->entry and
 *  as a ring using ->line.
 */
Ndbtuple*
_ndbparseline(char *cp)
{
	Ndbtuple *t;
	Ndbtuple *first, *last;

	first = last = 0;
	while(*cp != '#' && *cp != '\n'){
		t = 0;
		cp = _ndbparsetuple(cp, &t);
		if(cp == 0)
			break;
		if(first){
			last->line = t;
			last->entry = t;
		} else
			first = t;
		last = t;
		t->line = 0;
		t->entry = 0;
		setmalloctag(t, getcallerpc(&cp));
	}
	if(first)
		last->line = first;
	ndbsetmalloctag(first, getcallerpc(&cp));
	return first;
}
