#include <u.h>
#include <libc.h>

void
main(int argc, char *argv[])
{

	if(argc>1)
		sleep(atol(argv[1]) * 1000);

	exits(0);
}
