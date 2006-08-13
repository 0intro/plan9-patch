#include <u.h>
#include <libc.h>

void
main(int argc, char **argv)
{
	vlong ms;

	if(argc>1)
		for(ms = atoll(argv[1]); ms > 0; ms -= 1000)
			if(ms >= 1000)
				sleep(1000);
			else
				sleep(ms);
	exits(0);
}
