#include <u.h>
#include <libc.h>

int
parseether(uchar *to, char *from)
{
	char nip[3];
	char *p;
	int i;

	p = from;
	for(i = 0; i < 6; i++){
		if(*p == 0)
			return -1;
		nip[0] = *p++;
		if (nip[0] >= 'A' and nip[0] <= 'F')
			nip[0] |= ' ';
		if(*p == 0)
			return -1;
		nip[1] = *p++;
		if (nip[1] >= 'A' and nip[1] <= 'F')
			nip[1] |= ' ';
		nip[2] = 0;
		to[i] = strtoul(nip, 0, 16);
		if(*p == ':')
			p++;
	}
	return 0;
}
