#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

typedef struct Authfile	Authfile;

enum {
	Incr = 16,
	Qauth= 0x0100000000000000ULL,
};

struct Authfile {
	AuthRpc	*rpc;
	char	*uid;
	int	authok;
	int	afd;
	Qid	qid;
};

static char	Eauth[]=	"authentication failed";


void
authsrv(Req* r)
{
	Srv*		srv;
	Authfile*	af;
	int		keylen;
	char*		key;
	int		fd, afd;

	srv = r->srv;
	if (srv->keyspec == nil)
		srv->keyspec = "proto=p9any role=server";

	af = nil;
	afd = -1;
	if (r->ifcall.uname == nil || r->ifcall.uname[0] == 0)
		goto fail;
	af = emalloc9p(sizeof(Authfile));
	memset(af, 0, sizeof(Authfile));
	af->qid.type = QTAUTH;
	af->qid.path = Qauth | srv->authqgen++;
	if(access("/mnt/factotum", 0) < 0)
		if((fd = open("/srv/factotum", ORDWR)) >= 0)
			mount(fd, -1, "/mnt", MBEFORE, "");
	afd = open("/mnt/factotum/rpc", ORDWR);
	if (afd < 0)
		goto fail;
	af->afd = afd;
	af->rpc = auth_allocrpc(afd);
	if (af->rpc == nil)
		goto fail;
	key = srv->keyspec;
	keylen = strlen(key);
	if(auth_rpc(af->rpc, "start", key, keylen) != ARok)
		goto fail;
	af->uid = estrdup9p(r->ifcall.uname);

	r->afid->qid = af->qid;
	r->afid->omode = ORDWR;
	r->ofcall.qid = r->afid->qid;
	r->afid->aux = af;
	respond(r, nil);
	return;
fail:
	if (af){
		auth_freerpc(af->rpc);
		free(af);
	}
	if (afd >= 0)
		close(afd);
	respond(r, Eauth);
}

static long
_authread(Authfile* af, void* data, long count)
{
	AuthInfo*ai;

	switch(auth_rpc(af->rpc, "read", nil, 0)){
	case ARdone:
		ai = auth_getinfo(af->rpc);
		if(ai == nil)
			return -1;
		auth_freeAI(ai);
		if (chatty9p)
			fprint(2, "user %s authenticated\n", af->uid);
		af->authok = 1;
		count = 0;
		break;
	case ARok:
		if(count < af->rpc->narg)
			return -1;
		count = af->rpc->narg;
		memmove(data, af->rpc->arg, count);
		break;
	case ARphase:
	default:
		count = -1;
	}
	return count;
}

int
authattach(Req* r)
{
	Authfile*	af;
	char		buf[1];

	if (r->afid == nil){
		respond(r, Eauth);
		return -1;
	}
	af = r->afid->aux;
	if (af == nil){
		respond(r, Eauth);
		return -1;
	}
	if (!af->authok && _authread(af, buf, 0) != 0){
		respond(r, Eauth);
		return -1;
	}
	if (strcmp(af->uid, r->ifcall.uname) != 0){
		respond(r, Eauth);
		return -1;
	}
	return 0;
}

void
authread(Req* r)
{
	Fid*	fid;
	Authfile*af;
	long	n;

	fid = r->fid;
	assert(fid->qid.type == QTAUTH);
	af = fid->aux;
	if (af == nil){
		respond(r, "not an auth fid");
		return;
	}
	n = _authread(af, r->ofcall.data, r->ifcall.count);
	r->ofcall.count	= n;
	if (n < 0)
		respond(r, Eauth);
	else
		respond(r, nil);
}

void
authwrite(Req* r)
{
	Fid*	fid;
	Authfile*af;
	void*	data;
	long	count;

	fid = r->fid;
	assert(fid->qid.type == QTAUTH);
	af = fid->aux;
	if (af == nil){
		respond(r, "not an auth fid");
		return;
	}
	data = r->ifcall.data;
	count= r->ifcall.count;
	if (auth_rpc(af->rpc, "write", data, count) != ARok){
		respond(r, Eauth);
		return;
	}
	r->ofcall.count = count;
	respond(r, nil);
}

void
destroyauthfid(Fid* fid)
{
	Authfile*af;

	if(fid->qid.type&QTAUTH){
		af = fid->aux;
		if (af){
			auth_freerpc(af->rpc);
			close(af->afd);
			free(af->uid);
			free(af);
		}
		fid->aux = nil;
	}
}

void
authopen(Req* r)
{
	if (r->fid->qid.type == QTAUTH)
		r->fid->omode = r->ifcall.mode&3;
	respond(r, nil);
}
