/*
 * Plan 9 versions of system-specific functions
 *	By convention, exported routines herein have names beginning with an
 *	upper case letter.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#undef IN
#include "rc.h"
#include "exec.h"
#include "io.h"
#include "fns.h"
#include "getflags.h"

enum {
	Maxshebang = 1024
};

struct {
	int e;
	char *s;
} Winerrs[] = {				/* some more common errors translated to more friendly messages */
	{ ERROR_HANDLE_EOF,				"end of file" },
	{ ERROR_INVALID_HANDLE,			"invalid handle" },
	{ ERROR_SHARING_VIOLATION,		"sharing violation" },
	{ ERROR_FILE_NOT_FOUND,			"file does not exist" },
	{ ERROR_PATH_NOT_FOUND,			"path does not exist" },
	{ ERROR_BAD_PATHNAME,			"bad path" },
	{ ERROR_TOO_MANY_OPEN_FILES,	"too many open files" },
	{ ERROR_ACCESS_DENIED,			"permission denied" },
	{ ERROR_INVALID_NAME,			"filename syntax" },
	{ ERROR_OUTOFMEMORY,			"out of memory" },
	{ ERROR_NOT_ENOUGH_MEMORY,		"not enough emmory" },
	{ ERROR_WRITE_PROTECT,			"read only" },
	{ ERROR_BROKEN_PIPE,			"broken pipe" },
	{ ERROR_NO_MORE_SEARCH_HANDLES,	"too many open directories" },
	{ ERROR_ALREADY_EXISTS,			"already exists" },
	{ ERROR_BAD_EXE_FORMAT,			"bad executable format" },
	{ ERROR_SEEK_ON_DEVICE,			"seek on device" },
	{ ERROR_GEN_FAILURE,			"general failure" },
	{ ERROR_NOT_SUPPORTED,			"not supported" },
	{ ERROR_NOT_READY,				"device not ready" },
	{ ERROR_BAD_NETPATH,			"network path not found" },
	{ ERROR_BAD_NET_NAME,			"host not known" },
	{ ERROR_HANDLE_DISK_FULL,		"disk full" },
	{ 0,							nil },
};

char *Signame[] = {
	"sigexit",	"sighup",	"sigint",	"sigquit",
	"sigalrm",	"sigkill",	"sigfpe",	"sigterm",
	0
};

char *Rcmain = "undefined";


void execfinit(void);

builtin Builtin[] = {
	"cd",		execcd,
	"whatis",	execwhatis,
	"eval",		execeval,
	"exec",		execexec,	/* but with popword first */
	"exit",		execexit,
	"shift",	execshift,
	"wait",		execwait,
	".",		execdot,
	"finit",	execfinit,
	"flag",		execflag,
	0
};

#define	SEP	';'
char *xenviron;

/*
 * Windows preserves case but ignores it, even in environment variables.
 * We need to be able to find these vars so we force their
 * case to that below as they are parsed from the environment.
 */
char *Force[] = {
	"path",
	"pathext",
	nil
};


static struct {
	int pid;
	HANDLE hand;
} Child[10];


static int
cistrcmp(char *s1, char *s2)
{
	int c1, c2;

	while(*s1){
		c1 = *(uchar*)s1++;
		c2 = *(uchar*)s2++;

		if(c1 == c2)
			continue;

		if(c1 >= 'A' && c1 <= 'Z')
			c1 -= 'A' - 'a';

		if(c2 >= 'A' && c2 <= 'Z')
			c2 -= 'A' - 'a';

		if(c1 != c2)
			return c1 - c2;
	}
	return -*s2;
}

struct word*
enval(char *s)
{
	char *t, c;
	struct word *v;

	for(t = s;*t && *t != SEP;t++)
		continue;

	c=*t;
	*t='\0';
	v = newword(s, c=='\0'?(struct word *)0:enval(t+1));
	*t = c;
	return v;
}

static void
reslash(char *s, char new)
{
	char *p;

	for(p = s; *p; p++)
		if(*p == '\\' || *p == '/')
			*p = new;
}

int
cmpvar(const void *aa, const void *ab)
{
	struct var * const *a = aa, * const *b = ab;

	return strcmp((*a)->name, (*b)->name);
}

char *
exportenv(void)
{
	char *env, *p, *q;
	struct var **h, *v, **idx;
	struct word *a;
	int nvar = 0, nchr = 0, sep, i;
int x;
	/*
	 * Slightly kludgy loops look at locals then globals.
	 * locals no longer exist - geoff
	 */
	for(h = gvar-1; h != &gvar[NVAR]; h++)
	for(v = h >= gvar? *h: runq->local; v ;v = v->next){
		if(v==vlook(v->name) && v->val){
			nvar++;
			nchr+=strlen(v->name)+1;
			for(a = v->val;a;a = a->next)
				nchr+=strlen(a->word)+1;
		}
		if(v->fn){
			nvar++;
			nchr+=strlen(v->name)+strlen(v->fn[v->pc-1].s)+8;
		}
	}

	idx = (struct var **)emalloc(nvar * sizeof(struct var *));

	i = 0;
	for(h = gvar-1; h != &gvar[NVAR]; h++)
	for(v = h >= gvar? *h: runq->local; v ;v = v->next){
		if((v==vlook(v->name) && v->val) || v->fn)
			idx[i++] = v;
	}

	qsort((void *)idx, nvar, sizeof idx[0], cmpvar);

	env = (char *)emalloc(nvar+nchr+1);

	p = env;
	for(i = 0; i < nvar; i++){
		v = idx[i];
		if((v==vlook(v->name)) && v->val){
			q = v->name;
			while(*q) *p++=*q++;
			sep='=';
			for(a = v->val;a;a = a->next){
				*p++=sep;
				sep = SEP;
				q = a->word;
				while(*q) *p++=*q++;
			}
			*p++='\0';
		}
		if(v->fn){
			*p++='f'; *p++='n'; *p++='#';
			q = v->name;
			while(*q) *p++=*q++;
			*p++='=';
			q = v->fn[v->pc-1].s;
			while(*q) *p++=*q++;
			*p++='\0';
		}
	}
	*p = 0;

	return env;	
}
	
/*
 * had to change the parsing from plan9/unix code as windows
 * tends to use braces in variables and variable names with inpunity.
 * FIXME: should handle UTF really
 */
void
Vinit(void)
{
	int a;
	struct word *w;
	char *p, *s, **f, *env;
	static char buf[MAX_PATH];

	env = GetEnvironmentStrings();
	xenviron = env;
	for(; env && *env; env = strchr(env, 0)+1){
		if((s = strchr(env, '=')) == NULL)
			continue;
		if(strncmp(env, "fn#", 3) == 0)		/* ignore functions */
			continue;
		*s='\0';
		for(f = Force; *f; f++)
			if(cistrcmp(*f, env) == 0)
				break;
		if(*f){
			reslash(s+1, '/');
			setvar(*f, enval(s+1));
		}else{
			setvar(env, enval(s+1));
		}
		*s='=';
	}

	/* look for rcmain in $path */
	for(w = vlook("path")->val; w; w = w->next){
		snprintf(buf, sizeof(buf), "%s/rcmain", w->word);
		a = GetFileAttributes(buf);
		if(a != -1 && a != FILE_ATTRIBUTE_DIRECTORY)
			break;
	}
	if(w == nil){
		pfmt(err, "rc: cannot find 'rcmain' in $path :\n");
		exit(0);
	}
	Rcmain = buf;
}

char *xenv;

/*
 * called once per func read from env, returns after each,
 * but calls Xreturn() before returning when the list is empty
 */
void
Xrdfn(void)
{
	int len;
	char *s, *nxt;

	for(; xenv && *xenv; xenv = nxt){
		nxt = strchr(xenv, 0) +1;
		if(strncmp(xenv, "fn#", 3) != 0)		/* ignore variables */
			continue;
		if((s = strchr(xenv, '=')) == NULL)
			continue;

		len = strlen(xenv);
		*s=' ';
		xenv[2]=' ';
		xenv[len]='\n';
		execcmds(opencore(xenv, len+1));
		xenv[len]='\0';
		xenv[2]='#';
		*s='=';
		xenv = nxt;
		return;
	}
	Xreturn();
}


union code rdfns[4];

void
execfinit(void)
{
	static int first = 1;
	if(first){
		rdfns[0].i = 1;
		rdfns[1].f = Xrdfn;
		rdfns[2].f = Xjump;
		rdfns[3].i = 1;
		first = 0;
	}
	Xpopm();
	xenv = xenviron;
	start(rdfns, 1, runq->local);
}

static int
addchild(int pid, HANDLE hand)
{
	int i;
	
	for(i = 0; i < nelem(Child); i++) {
		if(Child[i].hand == 0) {
			Child[i].pid = pid;
			Child[i].hand = hand;
			return 1;
		}
	}
	pfmt(err, "adchild: child table full\n");
	return 0;
}

/* NB: return -1 only on interrupt, may cause a retry */
int
Waitfor(int pid, int persist)
{
	int i;
	HANDLE h;
	ulong status;
	char buf[32];

	if(pid == 0)
		return 0;

	for(i = 0; i < nelem(Child); i++)
		if(Child[i].pid == pid){
			h = Child[i].hand;
			break;
		}

	/* we don't know about this one - let the system try to find it */
	if(h == nil){
		h = OpenProcess(PROCESS_ALL_ACCESS, 0, pid);
		if(h == nil){
			pfmt(err, "%d cannot open process\n", pid);
			return 0;
		}
	}

	status = 1;
	WaitForSingleObject(h, INFINITE);
	GetExitCodeProcess(h, &status);

	CloseHandle(h);
	for(i = 0; i < nelem(Child); i++)
		if(Child[i].pid == pid){
			Child[i].pid = 0;
			Child[i].hand = nil;
			break;
		}

	if(status){
		inttoascii(buf, status);
		setstatus(buf);
		return 0;
	}
	setstatus("");
	return 0;
}

void
Updenv(void)
{
}

void
Execute(word *args, word *path)
{
	pfmt(err, "rc: exec not supported\n");
}

int
Abspath(char *w)
{
	if(strncmp(w, "./", 2)==0)
		return 1;
	if(strncmp(w, ".\\", 2)==0)
		return 1;
	if(strncmp(w, "../", 3)==0)
		return 1;
	if(strncmp(w, "..\\", 3)==0)
		return 1;
	if(strncmp(w, "/", 1)==0)
		return 1;
	if(strncmp(w, "\\", 1)==0)
		return 1;
	if(isalpha(w[0]) && w[1] == ':') 
		return 1;
	return 0;
}

int
Globsize(char *p)
{
	int isglob, globlen;

	isglob = 0;
	globlen = FILENAME_MAX+1;

	for(; *p; p++){
		if(*p == GLOB){
			p++;
			if(*p != GLOB)
				isglob++;
			if(*p == '*')
				globlen += FILENAME_MAX;
			else
				globlen += 1;
		}
		else
			globlen++;
	}
	if(isglob)
		return globlen;
	return 0;
}


enum { Ndirs = 50 };
typedef struct {
	HANDLE h;
	int first;
	WIN32_FIND_DATA data;
} Dir;
static Dir Dirs[Ndirs];

int
Opendir(char *path)
{
	long n;
	Dir *dp;
	char fullpath[MAX_PATH];


	for(dp = Dirs; dp < &Dirs[Ndirs]; dp++)
		if(dp == nil){
			snprintf(fullpath, MAX_PATH, "%s\\*.*", path);
			dp->h = FindFirstFile(fullpath, &dp->data);
			if(dp->h == INVALID_HANDLE_VALUE){
				dp->h = 0;						/* paranoia */
				return -1;
			}
			dp->first = 1;
			return dp-Dirs;
		}
	return -1;
}

static int
validfile(WIN32_FIND_DATA *wfd, int onlydirs)
{
	char *s;

	s = wfd->cFileName;
	if(! (wfd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && onlydirs)
		return 0;
	if(s[0] == '.' && s[1] == 0)
		return 0;
	if(s[0] == '.' && s[1] == '.' && s[2] == 0)
		return 0;
	return 1;
}

/*
 * onlydirs is advisory -- it means you only
 * need to return the directories.  it's okay to
 * return files too (e.g., on unix where you can't
 * tell during the readdir), but that just makes 
 * the globber work harder.
 */
int
Readdir(int f, void *p, int onlydirs)
{
	int n;
	WIN32_FIND_DATA *wfd;

	if(f < 0 || f >= Ndirs)
		return -1;

	wfd = &Dirs[f].data;
	if(Dirs[f].first){
		Dirs[f].first = 0;
		if(validfile(wfd, onlydirs)){
			strcpy(p, wfd->cFileName);
			return 1;
		}
	}

	while(FindNextFile(Dirs[f].h, wfd) != 0){
		if(! validfile(wfd, onlydirs))
			continue;
		strcpy(p, wfd->cFileName);
		return 1;
	}
	return 0;
}

void
Closedir(int f)
{
	if(f < 0 || f >= Ndirs)
		return;

	FindClose(Dirs[f].h);
	Dirs[f].h = 0;
}

int interrupted = 0;

static BOOL WINAPI
gettrap(DWORD code)
{
	int i;

	switch(code){
	case CTRL_C_EVENT:
		trap[SIGINT]++;
		break;
	case CTRL_BREAK_EVENT:
		trap[SIGQUIT]++;
		break;
	case CTRL_CLOSE_EVENT:	/* window close or "End Task"  task manager */
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		trap[SIGTERM]++;
		break;
	default:
		pfmt(err, "rc: %d unexpected trap code\n", code);
		break;
	}
	ntrap++;
	if(ntrap >= NSIG){
		pfmt(err, "rc: Too many traps (trap %d), aborting\n", code);
		return 0;		/* default action (exit) */
	}
	
	interrupted = 1;
	for(i = 0; i < nelem(Child); i++)
		if(Child[i].hand != nil)
			GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, Child[i].pid);
	return 1;			/* continue */
}

void
Trapinit(void)
{
	SetConsoleCtrlHandler(gettrap, TRUE);
}

void
Unlink(char *name)
{
	remove(name);
}

long
Write(int fd, void *buf, long cnt)
{
	return write(fd, buf, cnt);
}

long
Read(int fd, void *buf, long cnt)
{
	int n;

	n = read(fd, buf, cnt);
	if(interrupted)
		return -1;
	return n;
}

long
Seek(int fd, long cnt, long whence)
{
	return lseek(fd, cnt, whence);
}


/* used by whatis only */
int
Executable(char *file)
{
	int a;
	FILE *fp;
	ulong type;
	struct word *w;
	char *ext, buf[4];

	if(GetBinaryType(file, &type))
		return 1;

	if((ext = strrchr(file, '.')) != nil)
		for(w = vlook("pathext")->val; w; w = w->next)
			if(cistrcmp(ext, w->word) == 0)
				return 1;

	if((fp = fopen(file, "r")) == nil)
		return 0;
	if(fread(buf, 1, sizeof(buf), fp) <= 0){
		fclose(fp);
		return 0;
	}
	fclose(fp);
	if(strncmp(buf, "#!", 2) == 0)
		return 1;
	return 0;
}

int
Creat(char *file)
{
	return creat(file, 0644);
}

int
Dup(int a, int b)
{
	return dup2(a, b);
}

int
Dup1(int a)
{
	return dup(a);
}

void
Exit(char *stat)
{
	int n = 0;

	while(*stat){
		if(*stat != '|'){
			if(*stat < '0' || '9' < *stat)
				exit(1);
			else n = n*10 + *stat - '0';
		}
		stat++;
	}
	exit(n);
}

int
Eintr(void)
{
	return interrupted;
}

void
Noerror(void)
{
	interrupted = 0;
}
 
int
Isatty(int fd)
{
	switch(GetFileType((HANDLE *)_get_osfhandle(fd))){
	case FILE_TYPE_CHAR:	/* console or maybe a uart */
	case FILE_TYPE_PIPE:	/* ssh session */
		return 1;
	case FILE_TYPE_DISK:	/* redirection */
	case FILE_TYPE_REMOTE:	/* unused they say */
	case FILE_TYPE_UNKNOWN:
	default:
		return 0;
	}
}

void
Abort(void)
{
	pfmt(err, "aborting\n");
	flush(err);
	exit(2);
}

void
Memcpy(void *a, void *b, long n)
{
	memmove(a, b, n);
}

void*
Malloc(ulong n)
{
	return malloc(n);
}

int
needsrcquote(int c)
{
	if(c <= ' ')
		return 1;
	if(strchr("`^#*[]=|\\?${}()'<>&;", c))
		return 1;
	return 0;
}

void
errstr(char *buf, int len)
{
	int e, i;
	char *p, *q;

	e = GetLastError();
	if(e == ERROR_SUCCESS){
		*buf = 0;
		return;
	}

	for(i = 0; Winerrs[i].e; i++)
		if(Winerrs[i].e == e){
			strncpy(buf, Winerrs[i].s, len);
			buf[len -1] = 0;
			return;
		}

	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, nil, e, 0, buf, len, 0);
	for(p=q=buf; *p; p++) {
		if(*p == '\r')
			continue;
		if(*p == '\n')
			*q++ = ' ';
		else
			*q++ = *p;
	}
}

char **
mkargv(struct word *a)
{
	int n;
	char **argv, **argp;

	n = count(a)+2;
	if(n < 8)			/* plenty of room for shebangs */
		n = 8;
	argv = (char **)emalloc(n*sizeof(char *));
	memset(argv, 0, n*sizeof(char *));


	argp = argv+1;	/* leave one at front for runcoms */
	for(;a;a = a->next)
		*argp++=a->word;
	*argp = 0;
	return argv;
}

static int
setpath(char *path, char *file)
{
	char *p, *last, tmp[MAX_PATH+1];
	int n;

	if(strlen(file) >= MAX_PATH){
		pfmt(err, "%s: file name too long", file);
		return -1;
	}
	strcpy(tmp, file);

	for(p=tmp; *p; p++) {
		if(*p == '/')
			*p = '\\';
	}

	if(tmp[0] != 0 && tmp[1] == ':') {
		if(tmp[2] == 0) {
			tmp[2] = '\\';
			tmp[3] = 0;
		} else if(tmp[2] != '\\') {
			/* don't allow c:foo - only c:\foo */
			pfmt(err, "%s: illegal file name", file);
			return -1;
		}
	}

	path[0] = 0;
	n = GetFullPathName(tmp, MAX_PATH, path, &last);
	if(n >= MAX_PATH) {
		pfmt(err, "%s: expanded path too long", file);
		return -1;
	}
	if(n == 0 && tmp[0] == '\\' && tmp[1] == '\\' && tmp[2] != 0) {
		strcpy(path, tmp);
		return -1;
	}

	if(n == 0) {
		pfmt(err, "%s: bad file name", tmp);
		return -1;
	}

	for(p=path; *p; p++) {
		if(*p < 32 || *p == '*' || *p == '?') {
			pfmt(err, "%s: wildcards in path", path);
			return -1;
		}
	}

	/* get rid of trailling \ */
	if(path[n-1] == '\\') {
		if(n <= 2) {
			pfmt(err, "%s: illegal path", path);
			return -1;
		}
		path[n-1] = 0;
		n--;
	}

	if(path[1] == ':' && path[2] == 0) {
		path[2] = '\\';
		path[3] = '.';
		path[4] = 0;
		return -1;
	}

	if(path[0] != '\\' || path[1] != '\\')
		return 0;

	for(p=path+2,n=0; *p; p++)
		if(*p == '\\')
			n++;
	if(n == 0)
		return -1;
	if(n == 1)
		return -1;
	return 0;
}

static int
shargs(char *s, int n, char **ap)
{
	int i;

	s += 2;
	n -= 2;		/* skip #! */
	for(i=0; s[i]!='\n'; i++)
		if(i == n-1)
			return 0;
	s[i] = 0;

	if(flag['p'])
		pfmt(err, "got %s\n", s);

	*ap = 0;
	i = 0;
	for(;;) {
		while(*s==' ' || *s=='\t')
			s++;
		if(*s == 0)
			break;
		i++;
		*ap++ = s;
		*ap = 0;
		while(*s && *s!=' ' && *s!='\t')
			s++;
		if(*s == 0)
			break;
		else
			*s++ = 0;
	}
	return i;
}

static int
exetype(char *path, char *file, char **bangv)
{
	int a, n, hasext;
	FILE *fp;
	ulong type;
	struct word *w;
	char *ext, *p;
	static char line[Maxshebang];

	/* has it a valid looking extension? */
	hasext = 0;
	if((ext = strrchr(file, '.')) != 0){
		for(w = vlook("pathext")->val; w; w = w->next)
			if(cistrcmp(ext, w->word) == 0){
				hasext++;
				snprintf(path, MAX_PATH, "%s", file);
				if(flag['p'])
					pfmt(err, "srch: %s good ext\n", path);
				a = GetFileAttributes(path);
				if(a != -1 && a != FILE_ATTRIBUTE_DIRECTORY)
					return 0;
			}
	}

	/* is it an rc script, or a windows executable? */
	snprintf(path, MAX_PATH, "%s", file);
	if(flag['p'])
		pfmt(err, "srch: %s raw name\n", path);
	if((fp = fopen(path, "r")) != nil)
		if((n = fread(line, 1, sizeof(line), fp)) > 0){
			fclose(fp);

			if(strncmp(line, "MZ", 2) == 0)
				return 0;

			if(strncmp(line, "#!", 2) == 0)
				if(shargs(line, n, bangv) > 0){
					if(strcmp(bangv[0], "/bin/rc") == 0)
						bangv[0] = argv0;
					snprintf(path, MAX_PATH, "%s", bangv[0]);
					return 0;
				}
	}

	/* try appending a known extensions (.BAT files etc)*/
	if(! hasext)
		for(w = vlook("pathext")->val; w; w = w->next){
			snprintf(path, MAX_PATH, "%s%s", file, w->word);
			if(flag['p'])
				pfmt(err, "srch: %s add ext\n", path);
			a = GetFileAttributes(path);
			if(a != -1 && a != FILE_ATTRIBUTE_DIRECTORY)
				return 0;
		}
	return -1;
}



/*
 * windows quoting rules - I think
 * Words are seperated by space or tab
 * Words containing a space or tab can be quoted using "
 * 2N backslashes + " ==> N backslashes and end quote
 * 2N+1 backslashes + " ==> N backslashes + literal "
 * N backslashes not followed by " ==> N backslashes
 */
static char *
dblquote(char *cmd, char *s)
{
	int nb;
	char *p;

	for(p=s; *p; p++)
		if(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '"')
			break;

	if(p == s){					/* empty arg */
		strcpy(cmd, "\"\"");
		return cmd+2;
	}

	if(*p == 0){				/* easy case */
		strcpy(cmd, s);
		return cmd+(p-s);
	}

	*cmd++ = '"';
	for(;;) {
		for(nb=0; *s=='\\'; nb++)
			*cmd++ = *s++;

		if(*s == 0) {			/* trailing backslashes -> 2N */
			while(nb-- > 0)
				*cmd++ = '\\';
			break;
		}

		if(*s == '"') {			/* literal quote -> 2N+1 backslashes */
			while(nb-- > 0)
				*cmd++ = '\\';
			*cmd++ = '\\';		/* escape the quote */
		}
		*cmd++ = *s++;
	}

	*cmd++ = '"';
	*cmd = 0;

	return cmd;
}

static char *
proccmd(char **bangv, char **argv)
{
	int i, n;
	char *cmd, *p;

	/* conservatively calculate length of command;
	 * backslash expansion can cause growth in dblquote().
	 */
	n = 0;
	for(i=0; bangv[i]; i++)
		n += (bangv[i])? 2*strlen(bangv[i]): 2;
	for(i=0; argv[i]; i++)
		n += (argv[i])? 2*strlen(argv[i]): 2;
	n++;
	
	cmd = emalloc(n);
	p = cmd;
	for(i=0; bangv[i]; i++){
		p = dblquote(p, bangv[i]);
		*p++ = ' ';
	}
	for(i=0; argv[i]; i++){
		p = dblquote(p, argv[i]);
		*p++ = ' ';
	}
	if(p != cmd)
		p--;
	*p = 0;

	return cmd;
}

int
pipe(int *fd)
{
	/*
	 * If you want binary pipes in to/out of
	 * rc then you will probably want to change
	 * the definition of ifs in rcmain to include
	 * a carriage return. I cannot see why you would
	 * want to do this, but perhaps its a lack of vision.
	 * -Steve
	 */
	return _pipe(fd, 8192, _O_TEXT);
}

static HANDLE
fdexport(int fd)
{
	HANDLE h, r;

	if(fd < 0)
		return INVALID_HANDLE_VALUE;

	h = (HANDLE)_get_osfhandle(fd);
	if(h < 0)
		return INVALID_HANDLE_VALUE;

	if(!DuplicateHandle(GetCurrentProcess(), h,
				GetCurrentProcess(), &r, DUPLICATE_SAME_ACCESS,
				1, DUPLICATE_SAME_ACCESS))
		return INVALID_HANDLE_VALUE;
	return r;
}

int
ForkExecute(char *name, char **argv, int sin, int sout, int serr)
{
	int r;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	char path[MAX_PATH], *bangv[1024], *cmd, *env;

	bangv[0] = 0;
	if(exetype(path, name, bangv) == -1)
		return -1;

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;
	si.wShowWindow = SW_SHOW;
	si.hStdInput = fdexport(sin);
	si.hStdOutput = fdexport(sout);
	si.hStdError = fdexport(serr);

	env = exportenv();
	cmd = proccmd(bangv, argv);

	if(flag['d'])
		pfmt(err, "proc: path='%s' cmd='%s'\n", path, cmd);
	r = CreateProcess(path, cmd, nil, nil, TRUE, CREATE_NEW_PROCESS_GROUP, env, nil, &si, &pi);

	/* allow child to run */
	Sleep(0);

	free(cmd);
	free(env);

	CloseHandle(si.hStdInput);
	CloseHandle(si.hStdOutput);
	CloseHandle(si.hStdError);

	if(!r){
		setstatus("cannot create process");
		return 0;
	}

	CloseHandle(pi.hThread);
	if(addchild(pi.dwProcessId, pi.hProcess) == 0)
		return 0;

	return pi.dwProcessId;
}
