/*
 * pANS stdio -- vprintf
 */
#include "iolib.h"
int vprintf(const char *fmt, va_list args){
	int ret = _vfprintf(stdout, fmt, args);
	return ferror(stdout) ? -1 : ret;
}
