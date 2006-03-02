#include <u.h>
#include <libc.h>
#include <draw.h>
#include <html.h>
#include "impl.h"

Rune win2unicode[]={0x0080,0x0081,0x0082,0x0083,0x0084,0x0085,0x0086,0x0087,0x0088,0x0089,0x008a,0x008b,0x008c,0x008d,0x008e,0x008f,0x0090,0x0091,0x0092,0x0093,0x0094,0x0095,0x0096,0x0097,0x0098,0x0099,0x009a,0x009b,0x009c,0x009d,0x009e,0x009f,0x00a0,0x00a1,0x00a2,0x00a3,0x00a4,0x0490,0x00a6,0x00a7,0x0401,0x00a9,0x0404,0x00ab,0x00ac,0x00ad,0x00ae,0x0407,0x00b0,0x00b1,0x00b2,0x0456,0x0491,0x00b5,0x00b6,0x00b7,0x0451,0x00b9,0x0454,0x00bb,0x00bc,0x00bd,0x00be,0x0457,0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0416,0x0417,0x0418,0x0419,0x041a,0x041b,0x041c,0x041d,0x041e,0x041f,0x0420,0x0421,0x0422,0x0423,0x0424,0x0425,0x0426,0x0427,0x0428,0x0429,0x042a,0x042b,0x042c,0x042d,0x042e,0x042f,0x0430,0x0431,0x0432,0x0433,0x0434,0x0435,0x0436,0x0437,0x0438,0x0439,0x043a,0x043b,0x043c,0x043d,0x043e,0x043f,0x0440,0x0441,0x0442,0x0443,0x0444,0x0445,0x0446,0x0447,0x0448,0x0449,0x044a,0x044b,0x044c,0x044d,0x044e,0x044f};

Rune koi2unicode[]={0x2500,0x2502,0x250C,0x2510,0x2514,0x2518,0x251C,0x2524,0x252C,0x2534,0x253C,0x2580,0x2584,0x2588,0x258C,0x2590,0x2591,0x2592,0x2593,0x2320,0x25A0,0x2219,0x221A,0x2248,0x2264,0x2265,0x00A0,0x2321,0x00B0,0x00B2,0x00B7,0x00F7,0x2550,0x2551,0x2552,0x0451,0x2553,0x2554,0x2555,0x2556,0x2557,0x2558,0x2559,0x255A,0x255B,0x255C,0x255D,0x255E,0x255F,0x2560,0x2561,0x0401,0x2562,0x2563,0x2564,0x2565,0x2566,0x2567,0x2568,0x2569,0x256A,0x256B,0x256C,0x00A9,0x044E,0x0430,0x0431,0x0446,0x0434,0x0435,0x0444,0x0433,0x0445,0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,0x043F,0x044F,0x0440,0x0441,0x0442,0x0443,0x0436,0x0432,0x044C,0x044B,0x0437,0x0448,0x044D,0x0449,0x0447,0x044A,0x042E,0x0410,0x0411,0x0426,0x0414,0x0415,0x0424,0x0413,0x0425,0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,0x041F,0x042F,0x0420,0x0421,0x0422,0x0423,0x0416,0x0412,0x042C,0x042B,0x0417,0x0428,0x042D,0x0429,0x0427,0x042A};

Rune* whitespace = L" \t\n\r";
Rune* notwhitespace = L"^ \t\n\r";

// All lists start out like List structure.
// List itself can be used as list of int.
int
_listlen(List* l)
{
	int n = 0;

	while(l != nil) {
		l = l->next;
		n++;
	}
	return n;
}

// Cons
List*
_newlist(int val, List* rest)
{
	List* ans;

	ans = (List*)emalloc(sizeof(List));
	ans->val = val;
	ans->next = rest;
	return ans;
}

// Reverse a list in place
List*
_revlist(List* l)
{
	List* newl;
	List* nextl;

	newl = nil;
	while(l != nil) {
		nextl = l->next;
		l->next = newl;
		newl = l;
		l = nextl;
	}
	return newl;
}

// The next few routines take a "character class" as argument.
//    e.g., "a-zA-Z", or "^ \t\n"
// (ranges indicated by - except in first position;
//  ^ is first position means "not in" the following class)

// Splitl splits s[0:n] just before first character of class cl.
// Answers go in (p1, n1) and (p2, n2).
// If no split, the whole thing goes in the first component.
// Note: answers contain pointers into original string.
void
_splitl(Rune* s, int n, Rune* cl, Rune** p1, int* n1, Rune** p2, int* n2)
{
	Rune* p;

	p = _Strnclass(s, cl, n);
	*p1 = s;
	if(p == nil) {
		*n1 = n;
		*p2 = nil;
		*n2 = 0;
	}
	else {
		*p2 = p;
		*n1 = p-s;
		*n2 = n-*n1;
	}
}

// Splitr splits s[0:n] just after last character of class cl.
// Answers go in (p1, n1) and (p2, n2).
// If no split, the whole thing goes in the last component.
// Note: answers contain pointers into original string.
void
_splitr(Rune* s, int n, Rune* cl, Rune** p1, int* n1, Rune** p2, int* n2)
{
	Rune* p;

	p = _Strnrclass(s, cl, n);
	if(p == nil) {
		*p1 = nil;
		*n1 = 0;
		*p2 = s;
		*n2 = n;
	}
	else {
		*p1 = s;
		*p2 = p+1;
		*n1 = *p2-s;
		*n2 = n-*n1;
	}
}

// Splitall splits s[0:n] into parts that are separated by characters from class cl.
// Each part will have nonzero length.
// At most alen parts are found, and pointers to their starts go into
// the strarr array, while their lengths go into the lenarr array.
// The return value is the number of parts found.
int
_splitall(Rune* s, int n, Rune* cl, Rune** strarr, int* lenarr, int alen)
{
	int i;
	Rune* p;
	Rune* q;
	Rune* slast;

	if(s == nil || n == 0)
		return 0;
	i = 0;
	p = s;
	slast = s+n;
	while(p < slast && i < alen) {
		while(p < slast && _inclass(*p, cl))
			p++;
		if(p == slast)
			break;
		q = _Strnclass(p, cl, slast-p);
		if(q == nil)
			q = slast;
		assert(q > p && q <= slast);
		strarr[i] = p;
		lenarr[i] = q-p;
		i++;
		p = q;
	}
	return i;
}

// Find part of s that excludes leading and trailing whitespace,
// and return that part in *pans (and its length in *panslen).
void
_trimwhite(Rune* s, int n, Rune** pans, int* panslen)
{
	Rune* p;
	Rune* q;

	p = nil;
	if(n > 0) {
		p = _Strnclass(s, notwhitespace, n);
		if(p != nil) {
			q = _Strnrclass(s, notwhitespace, n);
			assert(q != nil);
			n = q+1-p;
		}
	}
	*pans = p;
	*panslen = n;
}

// _Strclass returns a pointer to the first element of s that is
// a member of class cl, nil if none.
Rune*
_Strclass(Rune* s, Rune* cl)
{
	Rune* p;

	for(p = s; *p != 0; p++)
		if(_inclass(*p, cl))
			return p;
	return nil;
}

// _Strnclass returns a pointer to the first element of s[0:n] that is
// a member of class cl, nil if none.
Rune*
_Strnclass(Rune* s, Rune* cl, int n)
{
	Rune* p;

	for(p = s; n-- && *p != 0; p++)
		if(_inclass(*p, cl))
			return p;
	return nil;
}

// _Strrclass returns a pointer to the last element of s that is
// a member of class cl, nil if none
Rune*
_Strrclass(Rune* s, Rune* cl)
{
	Rune* p;

	if(s == nil || *s == 0)
		return nil;
	p = s + runestrlen(s) - 1;
	while(p >= s) {
		if(_inclass(*p, cl))
			return p;
		p--;
	};
	return nil;
}

// _Strnrclass returns a pointer to the last element of s[0:n] that is
// a member of class cl, nil if none
Rune*
_Strnrclass(Rune* s, Rune* cl, int n)
{
	Rune* p;

	if(s == nil || *s == 0 || n == 0)
		return nil;
	p = s + n - 1;
	while(p >= s) {
		if(_inclass(*p, cl))
			return p;
		p--;
	};
	return nil;
}

// Is c in the class cl?
int
_inclass(Rune c, Rune* cl)
{
	int	n;
	int	ans;
	int	negate;
	int	i;

	n = _Strlen(cl);
	if(n == 0)
		return 0;
	ans = 0;
	negate = 0;
	if(cl[0] == '^') {
		negate = 1;
		cl++;
		n--;
	}
	for(i = 0; i < n; i++) {
		if(cl[i] == '-' && i > 0 && i < n - 1) {
			if(c >= cl[i - 1] && c <= cl[i + 1]) {
				ans = 1;
				break;
			}
			i++;
		}
		else if(c == cl[i]) {
			ans = 1;
			break;
		}
	}
	if(negate)
		ans = !ans;
	return ans;
}

// Is pre a prefix of s?
int
_prefix(Rune* pre, Rune* s)
{
	int	ns;
	int	n;
	int	k;

	ns = _Strlen(s);
	n = _Strlen(pre);
	if(ns < n)
		return 0;
	for(k = 0; k < n; k++) {
		if(pre[k] != s[k])
			return 0;
	}
	return 1;
}

// Number of runes in (null-terminated) s
int
_Strlen(Rune* s)
{
	if(s == nil)
		return 0;
	return runestrlen(s);
}

// -1, 0, 1 as s1 is lexicographically less, equal greater than s2
int
_Strcmp(Rune *s1, Rune *s2)
{
	if(s1 == nil)
		return (s2 == nil || *s2 == 0) ? 0 : -1;
	if(s2 == nil)
		return (*s1 == 0) ? 0 : 1;
	return runestrcmp(s1, s2);
}

// Like Strcmp, but use exactly n chars of s1 (assume s1 has at least n chars).
// Also, do a case-insensitive match, assuming s2
// has no chars in [A-Z], only their lowercase versions.
// (This routine is used for in-place keyword lookup, where s2 is in a keyword
// list and s1 is some substring, possibly mixed-case, in a buffer.)
int
_Strncmpci(Rune *s1, int n1, Rune *s2)
{
	Rune c1, c2;

	for(;;) {
		if(n1-- == 0) {
			if(*s2 == 0)
				return 0;
			return -1;
		}
		c1 = *s1++;
		c2 = *s2++;
		if(c1 >= 'A' && c1 <= 'Z')
			c1 = c1 - 'A' + 'a';
		if(c1 != c2) {
			if(c1 > c2)
				return 1;
			return -1;
		}
	}
}

// emalloc and copy
Rune*
_Strdup(Rune* s)
{
	if(s == nil)
		return nil;
	return _Strndup(s, runestrlen(s));
}

// emalloc and copy n chars of s (assume s is at least that long),
// and add 0 terminator.
// Return nil if n==0.
Rune*
_Strndup(Rune* s, int n)
{
	Rune* ans;

	if(n <= 0)
		return nil;
	ans = _newstr(n);
	memmove(ans, s, n*sizeof(Rune));
	ans[n] = 0;
	return ans;
}
// emalloc enough room for n Runes, plus 1 null terminator.
// (Not initialized to anything.)
Rune*
_newstr(int n)
{
	return (Rune*)emalloc((n+1)*sizeof(Rune));
}

// emalloc and copy s+t
Rune*
_Strdup2(Rune* s, Rune* t)
{
	int ns, nt;
	Rune* ans;
	Rune* p;

	ns = _Strlen(s);
	nt = _Strlen(t);
	if(ns+nt == 0)
		return nil;
	ans = _newstr(ns+nt);
	p = _Stradd(ans, s, ns);
	p = _Stradd(p, t, nt);
	*p = 0;
	return ans;
}

// Return emalloc'd substring s[start:stop],
Rune*
_Strsubstr(Rune* s, int start, int stop)
{
	Rune* t;

	if(start == stop)
		return nil;
	t = _Strndup(s+start, stop-start);
	return t;
}

// Copy n chars to s1 from s2, and return s1+n
Rune*
_Stradd(Rune* s1, Rune* s2, int n)
{
	if(n == 0)
		return s1;
	memmove(s1, s2, n*sizeof(Rune));
	return s1+n;
}

// Like strtol, but converting from Rune* string

#define LONG_MAX	2147483647L
#define LONG_MIN	-2147483648L

long
_Strtol(Rune* nptr, Rune** endptr, int base)
{
	Rune* p;
	long n, nn;
	int c, ovfl, v, neg, ndig;

	p = nptr;
	neg = 0;
	n = 0;
	ndig = 0;
	ovfl = 0;

	/*
	 * White space
	 */
	for(;;p++){
		switch(*p){
		case ' ':
		case '\t':
		case '\n':
		case '\f':
		case '\r':
		case '\v':
			continue;
		}
		break;
	}

	/*
	 * Sign
	 */
	if(*p=='-' || *p=='+')
		if(*p++ == '-')
			neg = 1;

	/*
	 * Base
	 */
	if(base==0){
		if(*p != '0')
			base = 10;
		else{
			base = 8;
			if(p[1]=='x' || p[1]=='X'){
				p += 2;
				base = 16;
			}
		}
	}else if(base==16 && *p=='0'){
		if(p[1]=='x' || p[1]=='X')
			p += 2;
	}else if(base<0 || 36<base)
		goto Return;

	/*
	 * Non-empty sequence of digits
	 */
	for(;; p++,ndig++){
		c = *p;
		v = base;
		if('0'<=c && c<='9')
			v = c - '0';
		else if('a'<=c && c<='z')
			v = c - 'a' + 10;
		else if('A'<=c && c<='Z')
			v = c - 'A' + 10;
		if(v >= base)
			break;
		nn = n*base + v;
		if(nn < n)
			ovfl = 1;
		n = nn;
	}

    Return:
	if(ndig == 0)
		p = nptr;
	if(endptr)
		*endptr = p;
	if(ovfl){
		if(neg)
			return LONG_MIN;
		return LONG_MAX;
	}
	if(neg)
		return -n;
	return n;
}

// Convert buf[0:n], bytes whose character set is chset,
// into a emalloc'd null-terminated Unicode string.
Rune*
toStr(uchar* buf, int n, int chset)
{
	int i;
	int m;
	Rune ch;
	Rune* ans;

	switch(chset) {
	case US_Ascii:
	case ISO_8859_1:
		ans = (Rune*)emalloc((n+1)*sizeof(Rune));
		for(i = 0; i < n; i++)
			ans[i] = buf[i];
		ans[n] = 0;
		break;
	case UTF_8:
		m = 0;
		for(i = 0; i < n; ) {
			i += chartorune(&ch, (char*)(buf+i));
			m++;
		}
		ans = (Rune*)emalloc((m+1)*sizeof(Rune));
		m = 0;
		for(i = 0; i < n; ) {
			i += chartorune(&ch, (char*)(buf+i));
			ans[m++] = ch;
		}
		ans[m] = 0;
		break;
	case KOI8:
		ans=(Rune*)emalloc((n+1)*sizeof(Rune));

		for(i=0;i<n;i++)
			if (buf[i]<=0x80)
				ans[i]=buf[i];
			else
				ans[i]=koi2unicode[buf[i]-0x80];
		ans[n]=0;
		break;
	case WIN_1251:
		ans=(Rune*)emalloc((n+1)*sizeof(Rune));

		for(i=0;i<n;i++)
			if (buf[i]<=0x80)
				ans[i]=buf[i];
			else
				ans[i]=win2unicode[buf[i]-0x80];
		ans[n]=0;
		break;
	default:
		ans = nil;
		assert(0);
	}
	return ans;
}

// Convert buf[0:n], Unicode characters,
// into an emalloc'd null-terminated string in character set chset.
// Use 0x80 for unconvertable characters.
uchar*
fromStr(Rune* buf, int n, int chset)
{
	uchar* ans;
	int i, lim, m;
	Rune ch;
	uchar* p;
	uchar s[UTFmax];

	ans = nil;
	switch(chset) {
	case US_Ascii:
	case ISO_8859_1:
		ans = (uchar*)emalloc(n+1);
		lim = (chset==US_Ascii)? 127 : 255;
		for(i = 0; i < n; i++) {
			ch = buf[i];
			if(ch > lim)
				ch = 0x80;
			ans[i] = ch;
		}
		ans[n] = 0;
		break;
	case UTF_8:
		m = 0;
		for(i = 0; i < n; i++) {
			m += runetochar((char*)s, &buf[i]);
		}
		ans = (uchar*)emalloc(m+1);
		p = ans;
		for(i = 0; i < n; i++)
			p += runetochar((char*)p, &buf[i]);
		*p = 0;
		break;
	case KOI8:
		ans=(uchar*)emalloc(n+1);
		for (i=0;i<n;i++)
		{
			ans[i]=0x80;
			for (m=0;m<0x80;m++)
				if (koi2unicode[m]==buf[i])
				{
					ans[i]=m+0x80;
					break;
				}

			if (ans[i]==0x80)
				ans[i]=buf[i];
		}
		ans[n]=0;
		break;
	case WIN_1251:
		ans=(uchar*)emalloc(n+1);
		for (i=0;i<n;i++)
		{
			ans[i]=0x80;
			for (m=0;m<0x80;m++)
				if (win2unicode[m]==buf[i])
				{
					ans[i]=m+0x80;
					break;
				}

			if (ans[i]==0x80)
				ans[i]=buf[i];
		}
		ans[n]=0;
		break;
	default:
		assert(0);
	}
	return ans;

}

// Convert n to emalloc'd String.
Rune*
_ltoStr(int n)
{
	int m;
	uchar buf[20];

	m = snprint((char*)buf, sizeof(buf), "%d", n);
	return toStr(buf, m, US_Ascii);
}
