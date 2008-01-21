#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <readpassphrase.h>
#include "sys9.h"

char *
readpassphrase(const char *prompt, char *buf, size_t nbuf, int flags)
{
	int c, cfd;
	char *p;
	FILE *fi;
	void (*sig)(int);

	if (!(flags & RPP_STDIN) && (fi = fopen("/dev/cons", "r")) != NULL){
		setbuf(fi, NULL);
		if(!(flags & RPP_ECHO_ON))
			if((cfd = _OPEN("/dev/consctl", OWRITE)) >= 0)
				_WRITE(cfd, "rawon", 5);
	} else {
		if (flags & RPP_REQUIRE_TTY) {
			errno = ENOTTY;
			return NULL;
		}
		fi = stdin;
	}
	sig = signal(SIGINT, SIG_IGN);
	if(!(flags & RPP_STDIN)) {
		fprintf(stderr, "%s", prompt);
		fflush(stderr);
	}

	for (p = buf; (c = getc(fi)) != '\n' && c !=' \r' && c != EOF; )
		if (p < buf + nbuf - 1) {
			if (flags & RPP_SEVENBIT)
				c &= 0xff;
			if (isalpha(c)) {
				if (flags & RPP_FORCELOWER)
					c = tolower(c);
				if (flags & RPP_FORCEUPPER)
					c = toupper(c);
			}
			*p++ = c;
		}
	*p = '\0';

	if (!(flags & RPP_STDIN)) {
		fprintf(stderr, "\n");
		fflush(stderr);
	}
	if (!(flags & RPP_ECHO_ON) && cfd) {
		_WRITE(cfd, "rawoff", 6);
		_CLOSE(cfd);
	}
	signal(SIGINT, sig);
	if (fi != stdin)
		fclose(fi);
	return buf;
}
