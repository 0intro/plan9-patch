#pragma src "/sys/src/libdraw"

typedef struct	Channel Channel;
typedef struct	Cursor Cursor;
typedef struct	Menu Menu;
typedef struct 	Mousectl Mousectl;

#pragma varargck	type "M"	int
extern	int	Mfmt(Fmt*);

enum {
	MBUTTONS	= 7,		/* ones on buttons bits */

	// cooked event flags
	MCLICK		= 0x00000100,
	MDOUBLE		= 0x00000200,
	MSELECT		= 0x00000400,
	MCHORD		= 0x00000800,
	MEND		= 0x00001000,
	MFLAGS		= 0x00001f00,
	MCHORD0		= 0x00700000,	// 1st chord button
	MCHORD1		= 0x07000000,	// 2nd chord button
	MCHORD2		= 0x70000000,	// 3rd chord button
	MCHORDALL	= 0xfff00000,	// chord button order

	// setmousemode args
	MRAW		= 0,
	MCOOKED		= 1
};

#define MCHORDB(b,nth)	(((b)>>(20 +(nth)*4))&0xf)

struct	Mouse
{
	int	buttons;	/* bit array: LMR=124 and flags */
	Point	xy;
	ulong	msec;
};

struct Mousectl
{
	Mouse;
	Channel	*c;	/* chan(Mouse) */
	Channel	*resizec;	/* chan(int)[2] */
			/* buffered in case client is waiting for a mouse action before handling resize */

	char	*file;
	int		mfd;		/* to mouse file */
	int		cfd;		/* to cursor file */
	int		pid;		/* of slave proc */
	Image*	image;		/* of associated window/display */
};

struct Menu
{
	char	**item;
	char	*(*gen)(int);
	int		lasthit;
};


/*
 * Mouse
 */
extern Mousectl*	initmouse(char*, Image*);
extern int			setmousemode(Mousectl*, int);
extern void			moveto(Mousectl*, Point);
extern int			readmouse(Mousectl*);
extern void			closemouse(Mousectl*);
extern void			setcursor(Mousectl*, Cursor*);
extern void			drawgetrect(Rectangle, int);
extern Rectangle	getrect(int, Mousectl*);
extern int	 		menuhit(int, Mousectl*, Menu*, Screen*);
extern int verbstate;
