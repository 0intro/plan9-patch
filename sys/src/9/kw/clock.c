/*
 * kirkwood clock
 */
#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#include "ureg.h"

enum {
	Tcycles = CLOCKFREQ / HZ,		/* cycles per clock tick */
	MaxPeriod = Tcycles,
	MinPeriod = MaxPeriod / 100,
};

static void
clockintr(Ureg *ureg, void*)
{
	TIMERREG->timerwd = CLOCKFREQ;		/* reassure the watchdog */
	coherence();
	timerintr(ureg, 0);
	intrclear(Irqbridge, IRQcputimer0);
}

void
clockinit(void)
{
	int s;
	long cyc;
	TimerReg *tmr = TIMERREG;

	tmr->ctl = 0;
	coherence();
	intrenable(Irqbridge, IRQcputimer0, clockintr, nil, "clock");

	s = spllo();			/* risky */
	/* take any deferred clock (& other) interrupts here */
	splx(s);

	/* adjust m->bootdelay, used by delay()? */
	m->ticks = 0;
	m->fastclock = 0;

	tmr->timer0 = Tcycles;
	tmr->ctl = Tmr0enable;		/* just once */
	coherence();

	s = spllo();			/* risky */
	/* one iteration seems to take about 40 ns. */
	for (cyc = Tcycles; cyc > 0 && m->fastclock == 0; cyc--)
		;
	splx(s);

	if (m->fastclock == 0) {
		serialputc('?');
		if (tmr->timer0 == 0)
			panic("clock not interrupting");
		else if (tmr->timer0 == tmr->reload0)
			panic("clock not ticking");
		else
			panic("clock running very slowly");
	}

	tmr->ctl = 0;
	coherence();
	tmr->timer0  = Tcycles;
	tmr->timer1  = ~0;
	tmr->reload1 = ~0;
	tmr->timerwd = CLOCKFREQ;
	coherence();
	tmr->ctl = Tmr0enable | Tmr1enable | Tmr1periodic | TmrWDenable;
	CPUCSREG->rstout |= RstoutWatchdog;
	coherence();
}

void
timerset(Tval next)
{
	TimerReg *tmr = TIMERREG;
	int offset;

	offset = next - fastticks(nil);
	if(offset < MinPeriod)
		offset = MinPeriod;
	else if(offset > MaxPeriod)
		offset = MaxPeriod;
	tmr->timer0 = offset;
	coherence();
}

uvlong
fastticks(uvlong *hz)
{
	uvlong now;
	int s;

	if(hz)
		*hz = CLOCKFREQ;
	s = splhi();
	now = (m->fastclock&0xFFFFFFFF00000000LL) | ~TIMERREG->timer1;
	if(now < m->fastclock)
		now += 0x100000000LL;
	m->fastclock = now;
	splx(s);
	return now;
}

ulong
µs(void)
{
	return fastticks2us(fastticks(nil));
}

void
microdelay(int l)
{
	int i;

	l *= m->delayloop;
	l /= 1000;
	if(l <= 0)
		l = 1;
	for(i = 0; i < l; i++)
		;
}

void
delay(int l)
{
	ulong i, j;

	j = m->delayloop;
	while(l-- > 0)
		for(i=0; i < j; i++)
			;
}

ulong
perfticks(void)
{
	return ~TIMERREG->timer1;
}
