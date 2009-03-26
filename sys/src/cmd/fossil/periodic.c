#include "stdinc.h"
#include "dat.h"
#include "fns.h"
#include "error.h"

struct Periodic {
	VtLock *lk;
	int die;
	void (*f)(void*);
	void *a;
	int msec;
};

static void periodicThread(void *a);

Periodic *
periodicAlloc(void (*f)(void*), void *a, int msec)
{
	Periodic *p;

	p = vtMemAllocZ(sizeof(Periodic));
	p->lk = vtLockAlloc();
	p->f = f;
	p->a = a;
	p->msec = msec;
	if(p->msec < 10)
		p->msec = 10;

	vtThread(periodicThread, p);
	return p;
}

void
periodicKill(Periodic *p)
{
	if(p == nil)
		return;
	vtLock(p->lk);
	p->die = 1;
	vtUnlock(p->lk);
}

static void
periodicFree(Periodic *p)
{
	vtLockFree(p->lk);
	vtMemFree(p);
}

static void
periodicThread(void *a)
{
	Periodic *p = a;
	vlong ts, ct, t;

	vtThreadSetName("periodic");

	ct = nsec()/1000000;
	t = ct + p->msec;

	for(;;){
		ts = t - ct;
		if(ts > 1000)
			ts = 1000;
		sleep(ts);
		vtLock(p->lk);
		if(p->die){
			vtUnlock(p->lk);
			break;
		}
		ct = nsec()/1000000;
		if(ct >= t){
			p->f(p->a);
			ct = nsec()/1000000;
			while(t <= ct)
				t += p->msec;
		}
		vtUnlock(p->lk);
	}
	periodicFree(p);
}

