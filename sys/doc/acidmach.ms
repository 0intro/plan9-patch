.TL
Multiprocessor Kernel Debugging Using Acid
.AU
Steven Stallion
stallion@coraid.com
.SH
Introduction
.LP
This document describes a method of debugging multiprocessor kernels
using
.I acid (1).
While in-situ debugging is yet to be fully realized, the mechanism
detailed herein provides superior postmortem visibility over previous
approaches.
It is expected that this body of work will result in a debugging
environment supportive of both in-situ and postmortem analysis in the
future.
This approach encourages use of existing tools and culminates in a new
.I acid
library, which provides a foundation for discovery and analysis of
multiprocessor related defects.
.LP
The reader should not consider this document an introductory text but
merely an exposition of completed work [1].
.SH
Mechanism
.LP
A panicking kernel will issue a nonmaskable inter-processor interrupt
(IPI) to all processors, excluding the caller.
Upon receiving the trap, each
.CW Mach
records a pointer to the current
.CW Ureg
structure and the stack pointer to the
.CW dbgreg
and
.CW dbgsp
members, respectively.
After which, the processor enters a halt state with interrupts
disabled.
The debugging processor will maintain a value of zero for both members
as a hint to use current register state at debug time.
Use of a nonmaskable interrupt yields the ability to record state
regardless of processor context; interrupt processing no longer
interferes with stack generation.
.LP
If
.CW consdebug
is non-nil,
.CW panic
will branch to a debug routine, typically
.CW rdb .
The system is then prepared to accept remote debugging commands,
ostensibly via
.I rdbfs (4).
To simplify setting
.CW consdebug
at boot, a new configuration variable, named
.CW rdb
was introduced to obviate the need for the
.CW ^T^Td
control sequence (see
.I cons (3)).
.LP
To further simplify remote debugging,
.CW rdb
was modified to return if a serial break is received.
.I Rdbfs
was also updated to send a serial break upon receiving a kill
message, allowing
.I acid
to terminate the debugging session and transitively reboot the system
using the
.CW kill
builtin function.
.SH
Using the Library Functions
.LP
The
.CW mach
library provides a number of functions the user may employ to examine
multiprocessor state.
.CW Mach
was developed as a companion to the
.CW kernel
library.
As such, both libraries must be defined on the command line when
starting
.I acid .
Normal
.CW kernel
initialization rules apply;
.CW kinit
must be called to establish the proper mapping for the kernel prior to
calling functions defined in
.CW mach .
.LP
The following example attaches to a remote kernel with
.I rdbfs
and gathers basic information using the
.CW mach
library:
.P1
% rdbfs /mnt/consoles/sys
attach /mnt/consoles/sys
% acid -k -l kernel -l mach -r 9pc
9pc:386 plan 9 boot image
/sys/lib/acid/port
/sys/lib/acid/386
/sys/lib/acid/kernel
/sys/lib/acid/mach
acid: kinit()
	rc("cd /sys/src/9/pc; mk proc.acid")
	include("/sys/src/9/pc/proc.acid")
acid: rc("cd /sys/src/9/pc; mk proc.acid")
8c -FTVw -a -I.  ../port/proc.c >proc.acid
acid: include("/sys/src/9/pc/proc.acid")
acid: machno()
0
acid: machs()
0x80017000 0 up 0x00000000
0x8003d000 1 up 0x00000000
.P2
.SH
Library Functions
.LP
The
.CW mach
library is located in the directory
.CW /sys/lib/acid .
As with other libraries, these functions may be overridden,
personalized, or added to by code defined in
.CW $home/lib/acid .
The implementation of these functions can be examined using the
.CW whatis
operator and then modified during debugging sessions.
.de Ip
.KS
.LP
.tl '\f2\\$1\fP\ \ \f(CW\\$2(\f2\\$3\f(CW)\f1''\\$4'
.IP
..
.de Ex
.KE
.KS
.IP
.ft CW
.ta 4n +4n +4n +4n +4n +4n +4n +4n +4n +4n +4n +4n +4n +4n +4n +4n
.nf
.in +4n
.br
..
.de Ee
.fi
.ft 1
.br
.KE
..
.\"
.\"
.\"
.Ip \f(CW{}\fB mach Mach "Print summary for Mach
.CW mach
prints a one line summary for the given
.I Mach .
The first printed column is the address of
.I Mach ,
followed by the
.CW machno ,
and ends with a summary of the currently scheduled process (see
.CW proc ).
.Ex
acid: mach(machp[1])
0x8003d000 1 up 0x00000000
.Ee
.\"
.\"
.\"
.Ip \f(CW{}\fB machgpr Mach "Print general purpose registers for Mach
.CW machgpr
prints the general purpose registers for the given
.I Mach .
While
.CW machgpr
may be used interactively, this function is typically only called by
.CW machregs .
.Ex
acid: machgpr(machp[1])
AX 0xc9121c18 BX 0x8025f34c CX 0x000000d8 DX 0x00000000
DI 0x80279b44 SI 0x0005d203 BP 0x80016000
.Ee
.\"
.\"
.\"
.Ip \f(CW{}\fB machlstk Mach "Stack trace with local variables for Mach
.CW machlstk
produces a long format stack trace for the given
.I Mach ,
similar to
.CW lstk .
Unlike
.CW lstk ,
the
.CW :
operator should not be used to address variables on processors other
than the debugging
.CW Mach
(see
.CW machno ).
This is a limitation imposed by the current implementation of
.I acid
and
.I libmach .
.Ex
acid: machlstk(machp[1])
runproc()+0x53 proc.c:531
	start=0xc9121c18
	p=0x80279948
	rq=0x8025f34c
	i=0x5d203
sched()+0x165 proc.c:164
	p=0x80279948
schedinit()+0x90 proc.c:107
squidboy(apic=0x8026b144)+0x96 mp.c:421
0x80003091 ?file?:0
.Ee
.\"
.\"
.\"
.Ip \f(CW{}\fB machno "" "Display debugging Mach number
.CW machno
prints the number of the debugging
.CW Mach .
.Ex
acid: machno()
0
.Ee
.\"
.\"
.\"
.Ip \f(CW{}\fB machregs Mach "Print registers for Mach
.CW machregs
prints the contents of both the general and special purpose registers
for the given
.I Mach .
.CW machregs
calls
.CW machspr
then
.CW machgpr
to display the contents of the registers.
.Ex
acid: machregs(machp[1])
PC 0x80196e10 runproc+0x53 proc.c:531
SP 0x80016f80 ECODE 0x801006f8 EFLAG 0x00000202
CS 0x00000010 DS 0x80010008 SS 0x0003b000
GS 0x0000001b FS 0x0000001b ES 0x00000008
TRAP 0x00000002 nonmaskable interrupt
AX 0xc9121c18 BX 0x8025f34c CX 0x000000d8 DX 0x00000000
DI 0x80279b44 SI 0x0005d203 BP 0x80016000
.Ee
.\"
.\"
.\"
.Ip \f(CW{}\fB machs "" "Print summaries for all Machs
.CW machs
prints summaries for all
.CW Mach s
in the system.
.Ex
acid: machs()
0x80017000 0 up 0x00000000
0x8003d000 1 up 0x00000000
.Ee
.\"
.\"
.\"
.Ip \f(CW{}\fB machspr Mach "Print special purpose registers for Mach
.CW machspr
prints the special purpose registers for the given
.I Mach .
While
.CW machspr
may be used interactively, this function is typically only called by
.CW machregs .
.Ex
acid: machspr(machp[1])
PC 0x80196e10 runproc+0x53 proc.c:531
SP 0x80016f80 ECODE 0x801006f8 EFLAG 0x00000202
CS 0x00000010 DS 0x80010008 SS 0x0003b000
GS 0x0000001b FS 0x0000001b ES 0x00000008
TRAP 0x00000002 nonmaskable interrupt
.Ee
.\"
.\"
.\"
.Ip \f(CW{}\fB machstacks "" "Stack traces for all Machs
.CW machstacks
prints a stack trace for all
.CW Mach s
in the system, similar to
.CW stacks .
.Ex
acid: machstacks()
=====================================================
0x80017000 0 up 0x00000000
runproc()+0x14d proc.c:530
sched()+0x165 proc.c:164
schedinit()+0x90 proc.c:107
main()+0x158 main.c:130
idle l.s:233
=====================================================
0x8003d000 1 up 0x00000000
runproc()+0x53 proc.c:531
sched()+0x165 proc.c:164
schedinit()+0x90 proc.c:107
squidboy(apic=0x8026b144)+0x96 mp.c:421
0x80003091 ?file?:0
.Ee
.\"
.\"
.\"
.Ip \f(CW{}\fB machstk Mach "Stack trace for Mach
.CW machstk
produces a short format stack trace for the given
.I Mach ,
similar to
.CW stk .
Unlike
.CW stk ,
the
.CW :
operator should not be used to address variables on processors other
than the current
.CW Mach
(see
.CW machno ).
This is a limitation imposed by the current implementation of
.I acid
and
.I libmach .
.Ex
acid: machstk(machp[1])
runproc()+0x53 proc.c:531
sched()+0x165 proc.c:164
schedinit()+0x90 proc.c:107
squidboy(apic=0x8026b144)+0x96 mp.c:421
0x80003091 ?file?:0
.Ee
.\"
.\"
.\"
.Ip \f(CW{}\fB machunwind Mach "Dump stack contents for Mach
.CW machunwind
dumps the contents of the stack for the given
.I Mach .
This is a function of last resort; it is primarily used to debug the
above functions.
.Ex
acid: machunwind(machp[1])
\&...
0x8003dfd8: schedinit+0x90
0x8003dfdc: 0x80279948
0x8003dfe0: microdelay+0x3c
0x8003dfe4: 0x32e8
0x8003dfe8: 0x0
0x8003dfec: squidboy+0x96
0x8003dff0: 0x64
0x8003dff4: 0x0
0x8003dff8: 0x80003091
0x8003dffc: mplapic+0xb0
.Ee
.SH
Support Functions
.LP
These functions provide utility to other library functions.
.\"
.\"
.\"
.Ip integer machaddr "Mach, integer" "Convert address for Mach
.CW machaddr
converts the given
.I integer
address to a global address.
If the address is mapped to processor-local memory,
.CW machaddr
will provide an alternative that can be addressed by any processor.
This function is idempotent; any address, regardless of mapping, may
be passed to this function.
.Ex
acid: MACHADDR = KZERO+0x16000;
acid: print(machaddr(machp[1], MACHADDR)\eX)
0x8003d000
.Ee
.\"
.\"
.\"
.Ip integer machpc Mach "Find program counter for Mach
.CW machpc
provides the program counter for the given
.I Mach .
.Ex
acid: print(machpc(machp[1])\eX)
0x80196e10
.Ee
.\"
.\"
.\"
.Ip integer machsp Mach "Find stack pointer for Mach
.CW machsp
provides the stack pointer for the given
.I Mach .
.Ex
acid: print(machsp(machp[1])\eX)
0x80016f80
.Ee
.\"
.\"
.\"
.Ip Ureg machureg Mach "Find Ureg structure for Mach
.CW machureg
provides the
.I Ureg
structure for the given
.I Mach .
.Ex
acid: print(machureg(machp[1])\eX)
0x8003df3c
.Ee
.SH
Redefined Functions
.LP
A handful of functions defined in other modules are redefined to
augment behavior on multiprocessors.
This necessitates the
.CW mach
library be defined after augmented libraries on the command line.
.\"
.\"
.\"
.Ip \f(CW{}\fB proclstk Proc "Stack trace with local variables for Proc
.CW proclstk
produces a long format stack trace for the given
.I Proc ,
similar to
.CW lstk .
Unlike
.CW lstk ,
the
.CW :
operator should not be used to address variables unless the process is
scheduled on the current
.CW Mach
(see
.CW machno ).
This is a limitation imposed by the current implementation of
.I acid
and
.I libmach .
This function was added to supplement the redefined
.CW procstk
function below.
.Ex
acid: proclstk(0x8027ca08)
gotolabel(label=0x80016030)+0x0 l.s:1000
sched()+0x160 proc.c:164
	p=0x286
sysrendezvous(arg=0x8027cc64)+0x143 sysproc.c:836
	rendval=0x0
	tag=0x624bc
	l=0x8b1ce87c
	val=0x7c86d798
	p=0x8027d3c8
syscall(ureg=0x8b20a1a4)+0x238 trap.c:726
	sp=0xcfffee9c
	scallnr=0x22
	startns=0x0
	ret=0xffffffff
	i=0x1
	stopns=0x0
	s=0x0
_syscallintr()+0x18 plan9l.s:44
0x8b20a1a4 ?file?:0
.Ee
.\"
.\"
.\"
.Ip \f(CW{}\fB procstk Proc "Stack trace for Proc
.CW procstk
produces a short format stack trace for the given
.I Proc ,
similar to
.CW stk .
Unlike
.CW stk ,
the
.CW :
operator should not be used to address variables unless the process is
scheduled on the current
.CW Mach
(see
.CW machno ).
This is a limitation imposed by the current implementation of
.I acid
and
.I libmach .
This function overrides the definition in
.CW kernel .
This was necessary as
.CW kernel
assumes any given process terminates in a call to
.CW gotolabel .
This is not always the case on a multiprocessor where the process may
be actively scheduled at debug time.
.Ex
acid: procstk(0x8027ca08)
gotolabel(label=0x80016030)+0x0 l.s:1000
sched()+0x160 proc.c:164
sysrendezvous(arg=0x8027cc64)+0x143 sysproc.c:836
syscall(ureg=0x8b20a1a4)+0x238 trap.c:726
_syscallintr()+0x18 plan9l.s:44
0x8b20a1a4 ?file?:0
.Ee
.\"
.\"
.\"
.Ip string reason integer "Return cause of Mach stoppage
.CW reason
uses machine-dependent information to generate a string explaining why
a
.CW Mach
has stopped.
The
.I integer
argument is the value of an architecture dependent status register.
This function overrides the builtin definition.
This was necessary as the builtin function would discard the
.I integer
argument and always consult the status register on the debugging
.CW Mach .
.Ex
acid: print(reason(machureg(machp[1]).trap))
nonmaskable interrupt
.Ee
.SH
Future Work
.LP
Only
.CW pc
kernels are supported.
.LP
In-situ debugging is not supported.
.LP
Floating point is not supported.
This is further complicated by support for XSAVE/XRESTOR and
YMM register state in
.CW pc
kernels.
.LP
.I Acid
and its constituent libraries assume uniprocessor, which causes
complications in kernel context.  Use of redefined functions have
largely addressed these issues.
.LP
Users must be aware of processor-local address ranges.
A good understanding of kernel memory mapping is essential.
The addition of per-processor address translation to
.I acid
and
.I libmach
could ease this burden considerably.
.LP
The somewhat portable nature of the
.CW kernel
library is at odds with the
.CW pc -specific
.CW mach
library.
Once remaining portability issues are resolved, both libraries could
be merged.
.SH
Acknowledgements
.LP
Portions of this document use descriptions and formatting from the
``Acid Manual'', by Phil Winterbottom.
.SH
References
.IP [1]
P. Winterbottom,
``Acid: A Debugger Built from A Language'',
.I
USENIX Proc.  of the Winter 1994 Conf.,
.R
San Francisco, CA.
