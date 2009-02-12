/*
 * pANS stdio -- vfprintf
 */
#include "iolib.h"
int
vfprintf(FILE *f, const char *s, va_list args)
{
	int ret = _vfprintf(f,s,args);
	return ferror(f) ? -1 : ret;
}
