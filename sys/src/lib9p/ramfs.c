#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <auth.h>
#include <thread.h>
#include <9p.h>

typedef struct Ramfile	Ramfile;

struct Ramfile {
	char*	data;
	int	ndata;
};


static char	Ebad[] = "something bad happened";
static char	Enomem[] = "no memory";

static void
fsattach(Req* r)
{
	if (r->srv->auth != nil && authattach(r) < 0)
		return;
	respond(r, nil);
}

static void
fsopen(Req* r)
{
	Ramfile*rf;
	File*	file;

	if (r->fid->qid.type&QTAUTH){
		authopen(r);
		return;
	}
	file = r->fid->file;
	if (r->ifcall.mode&OTRUNC){
		rf = file->aux;
		if(rf){
			rf->ndata = 0;
			file->length = 0;
		}
	}
	respond(r, nil);
}

static void
fscreate(Req* r)
{
	File *file;
	char* name;
	char* uid;
	int mode;
	Ramfile *rf;
	File* f;

	file = r->fid->file;
	name = r->ifcall.name;
	uid = r->fid->uid;
	mode = r->fid->file->mode & 0x777 & r->ifcall.perm;
	mode |= (r->ifcall.perm & ~0x777);
	if(f = createfile(file, name, uid, mode, 0)){
		rf = emalloc9p(sizeof *rf);
		f->aux = rf;
		closefile(r->fid->file);
		r->fid->file = f;
		r->ofcall.qid = f->qid;
		respond(r, nil);
	} else
		responderrstr();
}

static void
fsread(Req* r)
{
	File* file;
	long count;
	vlong offset;
	Ramfile*rf;

	if (r->fid->qid.type&QTAUTH){
		authread(r);
		return;
	}
	file = r->fid->file;
	rf = file->aux;
	offset = r->ifcall.offset;
	count = r->ifcall.count;
	if (offset >= rf->ndata){
		count = 0;
	} else {
		if(offset+count >= rf->ndata)
			count = rf->ndata - offset;
		memmove(r->ofcall.data, rf->data+offset, count);
	}
	r->ofcall.count = count;
	respond(r, nil);
}

static void
fswrite(Req* r)
{
	File* file;
	long count;
	vlong offset;
	Ramfile*rf;
	void*	v;

	if (r->fid->qid.type&QTAUTH){
		authwrite(r);
		return;
	}
	file = r->fid->file;
	rf = file->aux;
	offset = r->ifcall.offset;
	count = r->ifcall.count;
	if(offset+count >= rf->ndata){
		v = realloc(rf->data, offset+count);
		if(v == nil){
			respond(r, Enomem);
			return;
		} else {
			rf->data = v;
			rf->ndata = offset+count;
			file->length = rf->ndata;
		}
	}
	if (r->ofcall.count = count)
		memmove(rf->data+offset, r->ifcall.data, count);
	respond(r, nil);
}

static void
fswstat(Req* r)
{
	if (r->d.name && r->d.name[0] && strcmp(r->fid->file->name, r->d.name)){
		free(r->fid->file->name);
		r->fid->file->name = estrdup9p(r->d.name);
	}
	if (r->d.uid && r->d.uid[0]){
		free(r->fid->file->uid);
		r->fid->file->uid = estrdup9p(r->d.uid);
	}
	if (r->d.gid && r->d.gid[0]){
		free(r->fid->file->gid);
		r->fid->file->gid = estrdup9p(r->d.gid);
	}
	if (~(ulong)r->d.mode)
		r->fid->file->mode = r->d.mode;
	respond(r, nil);
}

static Srv sfs=
{
	.auth	=	authsrv,
	.attach	=	fsattach,
	.open	=	fsopen,
	.create	=	fscreate,
	.read	=	fsread,
	.wstat	=	fswstat,
	.write	=	fswrite,
	.destroyfid = 	destroyauthfid,
};

static void
freefile(File *f)
{
	Ramfile *rf;

	rf = f->aux;
	if(rf){
		f->aux = nil;
		free(rf->data);
		free(rf);
	}
}

void
usage(void)
{
	fprint(2, "usage: %s [-aD] [-s srv] [-m mnt] [-n addr]\n", argv0);
	exits("usage");
}

/* Example not using the thread library:
 * 	E , x/threadmain/c/main
 *	E , x/threadnetsrv/c/netsrv
 *	E , x/threadpostmountsrv/c/postmountsrv
 */

void
threadmain(int argc, char **argv)
{
	char*	mnt;
	char*	srv;
	char*	addr;
	int	pid;

	srv = nil;
	mnt = nil;
	addr = nil;
	ARGBEGIN{
	case 'a':
		sfs.auth = nil;
		break;
	case 'D':
		chatty9p++;
		break;
	case 's':
		srv = EARGF(usage());
		break;
	case 'm':
		mnt = EARGF(usage());
		break;
	case 'n':
		addr = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	if(argc!= 0)
		usage();
	if (srv == nil && mnt == nil && addr == nil)
		mnt = "/tmp";
	if (!chatty9p)
		rfork(RFNOTEG);
	sfs.tree =  alloctree(nil, nil, DMDIR|0755, freefile);
	if (addr != nil){
		pid = threadnetsrv(&sfs, addr);
		if (chatty9p)
			fprint(2, "proc %d listening\n", pid);
	}
	if (srv != nil || mnt != nil)
		threadpostmountsrv(&sfs, srv, mnt, MREPL|MCREATE);
}
