#include <u.h>
#include <libc.h>
#include <bio.h>
#include "hdr.h"
#include "conv.h"

typedef struct Tmap Tmap;
struct Tmap
{
	Rune u;
	Rune t;
};

static Tmap t1[] =
{
	{L'அ', L''},
	{L'ஆ', L''},
	{L'இ', L''},
	{L'ஈ', L''},
	{L'உ', L''},
	{L'ஊ', L''},
	{L'எ', L''},
	{L'ஏ', L''},
	{L'ஐ', L''},
	{L'ஒ', L''},
	{L'ஓ', L''},
	{L'ஔ', L''},
	{L'ஃ', L''}
};

static Rune t2[] =
{
	L'்', 
	L'்',	// filler
	L'ா',
	L'ி',
	L'ீ',
	L'ு',
	L'ூ',
	L'ெ',
	L'ே',
	L'ை',
	L'ொ',
	L'ோ',
	L'ௌ'
};

static Tmap t3[] =
{
	{L'க', L''},
	{L'ங', L''},
	{L'ச', L''},
	{L'ஜ', L''},
	{L'ஞ', L''},
	{L'ட', L''},
	{L'ண', L''},
	{L'த', L''},
	{L'ந', L''},
	{L'ன', L''},
	{L'ப', L''},
	{L'ம', L''},
	{L'ய', L''},
	{L'ர', L''},
	{L'ற', L''},
	{L'ல', L''},
	{L'ள', L''},
	{L'ழ', L''},
	{L'வ', L''},
 	{L'ஶ', L''},
	{L'ஷ', L''},
	{L'ஸ', L''},
	{L'ஹ', L''}
};

static Rune
findbytune(Tmap *tab, int size, Rune t)
{
	int i;

	for(i = 0; i < size; i++)
		if(tab[i].t == t)
			return tab[i].u;
	return Runeerror;
}

static Rune
findbyuni(Tmap *tab, int size, Rune u)
{
	int i;

	for(i = 0; i < size; i++)
		if(tab[i].u == u)
			return tab[i].t;
	return Runeerror;
}

static int
findindex(Rune *rstr, int size, Rune r)
{
	int i;

	for(i = 0; i < size; i++)
		if(rstr[i] == r)
			return i;
	return -1;
}

void
tune_in(int fd, long *x, struct convert *out)
{
	Biobuf b;
	Rune rbuf[N];
	Rune *r, *er, tr;
	int c, i;
	
	USED(x);
	r = rbuf;
	er = rbuf+N-3;
	Binit(&b, fd, OREAD);
	while((c = Bgetrune(&b)) != Beof){
		ninput += b.runesize;
		if(r >= er){
			OUT(out, rbuf, r-rbuf);
			r = rbuf;
		}
		if(c>=L'' && c <= L'' && (i = c%16) < nelem(t2)){
			if(c >= L''){
				*r++ = L'க';
				*r++ = L'்';
				*r++ = L'ஷ';
			}else
				*r++ = findbytune(t3, nelem(t3), c-i+1);
			if(i != 1)
				*r++ = t2[i];
		}else if((tr = findbytune(t1, nelem(t1), c)) != Runeerror)
			*r++ = tr;
		else switch(c){
			case L'':
				*r++ = L'ண'; *r++ = L'ா';
				break;
			case L'':
				*r++ = L'ற'; *r++ = L'ா';
				break;
			case L'':
				*r++ = L'ன'; *r++ = L'ா';
				break;
			case L'':
				*r++ = L'ண'; *r++ = L'ை';
				break;
			case L'':
				*r++ = L'ல'; *r++ = L'ை';
				break;
			case L'':
				*r++ = L'ள'; *r++ = L'ை';
				break;
			case L'':
				*r++ = L'ன'; *r++ = L'ை';
				break;
			case L'':
				*r++ = L'ஶ'; *r++ = L'்'; *r++ = L'ர'; *r++ = L'ீ';
				break;
			default: 
				if(c >= 0xe200 && c <= 0xe3ff){
					if(squawk)
						EPR( "%s: rune 0x%x not in output cs\n", argv0, c);
					nerrors++;
					if(clean)
						break;
					c = BADMAP;
				}
				*r++ = c;
				break;
		}
	}
	if(r > rbuf)
		OUT(out, rbuf, r-rbuf);
	OUT(out, rbuf, 0);
}

void
tune_out(Rune *r, int n, long *x)
{
	static enum { state0, state1, state2, state3, state4, state5, state6, state7 } state = state0;
	static Rune lastr;
	Rune *er, tr;
	char *p;
	int i;

	USED(x);
	nrunes += n;
	er = r+n;
	for(p = obuf; r < er; r++)
		switch(state){
		case state0:
		casestate0:
			if((tr = findbyuni(t3, nelem(t3), *r)) != Runeerror){
				lastr = tr;
				state = state1;
			}else if(*r == L'ஒ'){
				lastr = L'';
				state = state3;
			}else if((tr = findbyuni(t1, nelem(t1), *r)) != Runeerror)
				p += runetochar(p, &tr);
			else
				p += runetochar(p, r);
			break;
		case state1:
		casestate1:
			if((i = findindex(t2, nelem(t2), *r)) != -1){
				if(lastr && lastr != L'�')
					lastr += i-1;
				if(*r ==L'ெ')
					state = state5;
				else if(*r ==L'ே')
					state = state4;
				else if(lastr == L'')
					state = state2;
				else if(lastr == L'')
					state = state6;
				else{
					if(lastr)
						p += runetochar(p, &lastr);
					state = state0;
				}
			}else if(lastr && lastr != L'�' && (*r == L'²' || *r == L'³' || *r == L'⁴')){
				if(squawk)
					EPR( "%s: character <U+%04X, U+%04X> not in output cs\n", argv0, lastr, *r);
				lastr = clean ? 0 : L'�';
				nerrors++;
			}else{
				if(lastr)
					p += runetochar(p, &lastr);
				state = state0;
				goto casestate0;
			}
			break;
		case state2:
			if(*r == L'ஷ'){
				lastr = L'';
				state = state1;
				break;
			}
			p += runetochar(p, &lastr);
			state = state0;
			goto casestate0;
		case state3:
			state = state0;
			if(*r == L'ௗ'){
				p += runetochar(p, L"");
				break;
			}
			p += runetochar(p, &lastr);
			goto casestate0;
		case state4:
			state = state0;
			if(*r == L'ா'){
				if(lastr){
					if(lastr != L'�')
						lastr += 3;
					p += runetochar(p, &lastr);
				}
				break;
			}
			if(lastr)
				p += runetochar(p, &lastr);
			goto casestate0;
		case state5:
			state = state0;
			if(*r == L'ா' || *r == L'ௗ'){
				if(lastr){
					if(lastr != L'�')
						lastr += *r == L'ா' ? 3 : 5;
					p += runetochar(p, &lastr);
				}
				break;
			}
			if(lastr)
				p += runetochar(p, &lastr);
			goto casestate0;
		case state6:
			if(*r == L'ர'){
				state = state7;
				break;
			}
			p += runetochar(p, &lastr);
			state = state0;
			goto casestate0;
		case state7:
			if(*r == L'ீ'){
				p += runetochar(p, L"");
				state = state0;
				break;
			}
			p += runetochar(p, &lastr);
			lastr = L'';
			state = state1;
			goto casestate1;
		}
	if(n == 0 && state != state0){
		if(lastr)
			p += runetochar(p, &lastr);
		state = state0;
	}
	noutput += p-obuf;
	write(1, obuf, p-obuf);
}
