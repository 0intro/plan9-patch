#include <u.h>
#include <libc.h>
#include <String.h>

void	find(char*, Dir*);
void	err(char*);
void	warn(char*);
int	seen(Dir*);

char	*fmt = "%q\n";
int fflag;

void
main(int argc, char *argv[])
{
	int i;

	doquote = needsrcquote;
	quotefmtinstall();

	ARGBEGIN {
	case 'f':	/* ignore errors */
		fflag = 1;
		break;
	} ARGEND
	if(argc==0)
		find(".", dirstat("."));
	else
		for(i=0; i<argc; i++) {
			find(argv[i], dirstat(argv[i]));
			print(fmt, argv[i]);
		}

	exits(0);
}

void
find(char *name, Dir *dir)
{
	int fd, i, n;
	Dir *buf, *d;
	String *file;

	if(dir == nil) {
		warn(name);
		return;
	}

	fd = open(name, OREAD);
	if(fd < 0) {
		warn(name);
		return;
	}

	while((n=dirread(fd, &buf)) > 0) {
		d = buf;
		for(i=0; i<n; i++, d++) {
			if((d->qid.type&QTDIR) == 0) {
				file = s_copy(name);
				if(file == nil)
					err("s_copy");
				s_append(file, "/");
				s_append(file, d->name);
				print(fmt, s_to_c(file));
				s_free(file);
				continue;
			}
			if(strcmp(d->name, ".") == 0 ||
			   strcmp(d->name, "..") == 0 ||
			   seen(d))
				continue;
			file = s_copy(name);
			s_append(file, "/");
			s_append(file, d->name);
			find(s_to_c(file), d);
			if(file == nil)
				err("s_copy");
			print(fmt, s_to_c(file));
			s_free(file);
		}
		free(buf);
	}
	if(n < 0)
		warn(name);
	close(fd);
}

#define	NCACHE	128	/* must be power of two */
typedef	struct	Cache	Cache;
struct	Cache
{
	Dir*	cache;
	int	n;
	int	max;
} cache[NCACHE];

int
seen(Dir *dir)
{
	Dir *dp;
	int i;
	Cache *c;

	c = &cache[dir->qid.path&(NCACHE-1)];
	dp = c->cache;
	for(i=0; i<c->n; i++, dp++)
		if(dir->qid.path == dp->qid.path &&
		   dir->type == dp->type &&
		   dir->dev == dp->dev)
			return 1;
	if(c->n == c->max){
		c->cache = realloc(c->cache, (c->max+=20)*sizeof(Dir));
		if(cache == 0)
			err("malloc failure");
	}
	c->cache[c->n++] = *dir;
	return 0;
}

void
err(char *s)
{
	fprint(2, "find: %s: %r\n", s);
	exits(s);
}

void
warn(char *s)
{
	if(fflag == 0)
		fprint(2, "find: %s: %r\n", s);
	return;
}
