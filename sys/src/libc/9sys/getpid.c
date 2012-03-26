#include	<u.h>
#include	<libc.h>

int
getpid(void)
{
	char b[20];
	int f;

	b[0] = 0;
	f = open("#c/pid", 0);
	if(f >= 0) {
		read(f, b, sizeof(b));
		close(f);
	}
	return atol(b);
}
