#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>

int
uname(struct utsname *n)
{
	// n->sysname = "Plan9";
	n->sysname = "Linux";			/* to make gnu configure work */
	n->nodename = getenv("sysname");
	if(!n->nodename){
		n->nodename = getenv("site");
		if(!n->nodename)
			n->nodename = "?";
	}
	n->release = "4";			/* edition */
	n->version = "0";
	n->machine = getenv("cputype");
	if (strcmp(n->machine, "386") == 0)
		n->machine = "i386";		/* to make gnu configure work */
	if(!n->machine)
		n->machine = "?";
	return 0;
}
