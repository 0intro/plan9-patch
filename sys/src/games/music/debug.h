extern int debug;

enum {
	DBGSERVER	= 0x01,
	DBGCONTROL	= 0x02,
	DBGCTLGRP	= 0x04,
	DBGPICKLE	= 0x08,
	DBGSTATE	= 0x10,
	DBGPLAY		= 0x20,
	DBGPUMP		= 0x40,
	DBGTHREAD	= 0x80,
	DBGFILEDUMP	= 0x100,
};
