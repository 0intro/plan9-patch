#include <u.h>
#include <libc.h>

int
needsrcquote(int c)
{
	if(c <= ' ')
		return 1;
	if(c < 0x80 && strchr("`^#*[]=|\\?${}()'<>&;", c))
		return 1;
	return 0;
}
