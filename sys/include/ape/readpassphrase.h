#ifndef _READPASSPHRASE_H
#define _READPASSPHRASE_H
#ifndef _BSD_EXTENSION
    This header file is an extension to ANSI/POSIX
#endif

#define RPP_ECHO_OFF    0x00
#define RPP_ECHO_ON     0x01
#define RPP_REQUIRE_TTY 0x02
#define RPP_FORCELOWER  0x04
#define RPP_FORCEUPPER  0x08
#define RPP_SEVENBIT    0x10
#define RPP_STDIN       0x20

extern char * readpassphrase(const char *, char *, size_t, int);

#endif /* _READPASSPHRASE_H */
