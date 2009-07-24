
//
// ATI Radeon [789]???
// see /sys/src/cmd/aux/vga/radeon.c
//

#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/error.h"

#define Image	IMAGE
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include "screen.h"

#include "/sys/src/cmd/aux/vga/radeon.h"
#include "/sys/src/cmd/aux/vga/radeon_pciids.h"

static int debug = 1;

enum {
	Kilo	= 1024,
	Meg	= Kilo * Kilo,
};

// Pci Match

static Pcidev*
radeonpci(void)
{
	static Pcidev *p = nil;
	struct pciids *ids;

	if (p != nil)
		return p;

	p = pcimatch(nil, ATI_PCIVID, 0);
	if (p == nil)
		return nil;

	for (ids = radeon_pciids; ids->did; ids++) {
		if (ids->did == p->did)
			return p;
	}

	p = nil;
	return p;
}

// mmio access

static void
OUTREG8(ulong mmio, ulong offset, uchar val)
{
	((uchar*) KADDR((mmio + offset)))[0] = val;
}

static void
OUTREG(ulong mmio, ulong offset, ulong val)
{
	((ulong*) KADDR((mmio + offset)))[0] = val;
}

static ulong
INREG(ulong mmio, ulong offset)
{
	return ((ulong*) KADDR((mmio + offset)))[0];
}

static void
OUTREGP(ulong mmio, ulong offset, ulong val, ulong mask)
{
	ulong	tmp;

	tmp = INREG(mmio, offset);
	tmp &= mask;
	tmp |= val;
	OUTREG(mmio, offset, tmp);
}

static void
OUTPLL(ulong mmio, ulong offset, ulong val)
{
	uchar	tmp;

	tmp = (offset & 0x3f) | RADEON_PLL_WR_EN;
	OUTREG8(mmio, RADEON_CLOCK_CNTL_INDEX, tmp);
	OUTREG(mmio, RADEON_CLOCK_CNTL_DATA, val);
}

static ulong
INPLL(ulong mmio, ulong offset)
{
	ulong	data;

	OUTREG8(mmio, RADEON_CLOCK_CNTL_INDEX, offset & 0x3f);
	data = INREG(mmio, RADEON_CLOCK_CNTL_DATA);

	return data;
}

static void
OUTPLLP(ulong mmio, ulong offset, ulong val, ulong mask)
{
	ulong	tmp;
	
	tmp = INPLL(mmio, offset);
	tmp &= mask;
	tmp |= val;
	OUTPLL(mmio, offset, tmp);
}

// Init

static void
radeonlinear(VGAscr*, int, int)
{
}

static void
radeonenable(VGAscr* scr)
{
	Pcidev *p;

	if (scr->mmio)
		return;

	p = radeonpci();
	if (p == nil)
		return;
	scr->id = p->did;
	scr->pci = p;

	scr->mmio = vmap(p->mem[2].bar & ~0x0f, p->mem[2].size);
	if(scr->mmio == 0)
		return;
	addvgaseg("radeonmmio", p->mem[2].bar & ~0x0f, p->mem[2].size);

	vgalinearpci(scr);
	if(scr->apsize)
	  addvgaseg("radeonscreen", scr->paddr, scr->apsize);
}

// Cursor

static void
radeoncurload(VGAscr* scr, Cursor* curs)
{
	int	x, y;
	ulong *p;

	if(scr->mmio == nil)
		return;

	p = (ulong*) KADDR(scr->storage);

	for(y = 0; y < 64; y++){
		int	cv, sv;

		if (y < 16) {
			cv = curs->clr[2*y] << 8 | curs->clr[2*y+1];
			sv = curs->set[2*y] << 8 | curs->set[2*y+1];
		} else
		{
			cv = sv = 0;
		}

		for(x = 0; x < 64; x++){
			ulong col = 0;
			int c, s;

			if (x < 16) {
				c = (cv >> (15 - x)) & 1;
				s = (sv >> (15 - x)) & 1;
			} else
			{
				c = s = 0;
			}

			switch(c | (s<<1)) {
			case 0:
				col = 0x00000000;
				break;
			case 1:
				col = 0xffffffff;	// white
				break;
			case 2:
			case 3:
				col = 0xff000000;	// black
				break;
			}

			*p++ = col;
		}
	}

	scr->offset.x = curs->offset.x;
	scr->offset.y = curs->offset.y;
}

static int
radeoncurmove(VGAscr* scr, Point p)
{
	int	x, y, ox, oy;
	static ulong storage = 0;

	if(scr->mmio == nil)
		return 1;

	if (storage == 0)
		storage = (scr->apsize - 1*Meg);

	x = p.x + scr->offset.x;
	y = p.y + scr->offset.y;
	ox = oy = 0;

	if (x < 0) {
		ox = -x - 1;
		x = 0;
	}
	if (y < 0) {
		oy = -y - 1;
		y = 0;
	}

	OUTREG((ulong) scr->mmio, RADEON_CUR_OFFSET, storage + oy * 256);
	OUTREG((ulong) scr->mmio, RADEON_CUR_HORZ_VERT_OFF, ((ox & 0x7fff) << 16) | (oy & 0x7fff));
	OUTREG((ulong) scr->mmio, RADEON_CUR_HORZ_VERT_POSN, ((x & 0x7fff) << 16) | (y & 0x7fff));

	return 0;
}

static void
radeoncurdisable(VGAscr* scr)
{
	if(scr->mmio == nil)
		return;

	OUTREGP((ulong) scr->mmio, RADEON_CRTC_GEN_CNTL, 0, ~RADEON_CRTC_CUR_EN);
}

static void
radeoncurenable(VGAscr* scr)
{
	ulong	storage;

	if(scr->mmio == 0)
		return;

	radeoncurdisable(scr);
	storage = (scr->apsize - 1*Meg);
	scr->storage = (ulong) KADDR(scr->paddr + storage);
	radeoncurload(scr, &arrow);
	radeoncurmove(scr, ZP);

	OUTREGP((ulong) scr->mmio, RADEON_CRTC_GEN_CNTL,
		RADEON_CRTC_CUR_EN | (2<<20), ~(RADEON_CRTC_CUR_EN | (3<<20)));	
}

// Hw Blank

static void
radeonblank(VGAscr* scr, int blank)
{
	ulong mask;
	char* cp;

	if (scr->mmio == 0)
		return;

//	iprint("radeon: hwblank(%d)\n", blank);

	mask = RADEON_CRTC_DISPLAY_DIS | 
		RADEON_CRTC_HSYNC_DIS | 
		RADEON_CRTC_VSYNC_DIS;

	if (blank == 0) {
		OUTREGP((ulong) scr->mmio, RADEON_CRTC_EXT_CNTL, 0, ~mask);
		return;
	}

	cp = getconf("*dpms");
	if (cp) {
		if (cistrcmp(cp, "standby") == 0) {
			OUTREGP((ulong) scr->mmio, RADEON_CRTC_EXT_CNTL,
				RADEON_CRTC_DISPLAY_DIS | RADEON_CRTC_HSYNC_DIS, ~mask);
		} else if (cistrcmp(cp, "suspend") == 0) {
			OUTREGP((ulong) scr->mmio, RADEON_CRTC_EXT_CNTL,
				RADEON_CRTC_DISPLAY_DIS | RADEON_CRTC_VSYNC_DIS, ~mask);
		} else if (cistrcmp(cp, "off") == 0) {
			OUTREGP((ulong) scr->mmio, RADEON_CRTC_EXT_CNTL, mask, ~mask);
		}
		return;
	}
	
	OUTREGP((ulong) scr->mmio, RADEON_CRTC_EXT_CNTL, mask, ~mask);
}

// Hw Accel

static void
radeonwaitfifo(VGAscr* scr, int entries)
{
	int i;

	for (i = 0; i < 2000000; i++) {
		int slots;

		slots = INREG((ulong) scr->mmio, RADEON_RBBM_STATUS) & 
			RADEON_RBBM_FIFOCNT_MASK;
		if (slots >= entries) {
			return;
		}
	}

	iprint("radeon: fifo timeout\n");
}

static void
radeonwaitidle(VGAscr* scr)
{
    radeonwaitfifo(scr, 64);

    for (;;) {
		int	i;

		for (i = 0; i < 2000000; i++) {
			ulong status;
			
			status = INREG((ulong) scr->mmio, RADEON_RBBM_STATUS);
			if (status & RADEON_RBBM_ACTIVE)
				continue;

			return;
	    }

		iprint("radeon: idle timed out: %uld entries, stat=0x%.8ulx\n",
		     INREG((ulong) scr->mmio, RADEON_RBBM_STATUS) & RADEON_RBBM_FIFOCNT_MASK,
		     INREG((ulong) scr->mmio, RADEON_RBBM_STATUS));
	}
}

static ulong dp_gui_master_cntl = 0;

static int
radeonfill(VGAscr* scr, Rectangle r, ulong color)
{
	if(scr->mmio == nil)
		return 0;

	radeonwaitfifo(scr, 6);
    OUTREG((ulong) scr->mmio, RADEON_DP_GUI_MASTER_CNTL, 
		dp_gui_master_cntl | RADEON_GMC_BRUSH_SOLID_COLOR
		| RADEON_GMC_SRC_DATATYPE_COLOR | RADEON_ROP3_P);
    OUTREG((ulong) scr->mmio, RADEON_DP_BRUSH_FRGD_CLR, color);
    OUTREG((ulong) scr->mmio, RADEON_DP_WRITE_MASK, 0xffffffff);
    OUTREG((ulong) scr->mmio, RADEON_DP_CNTL, 
		(RADEON_DST_X_LEFT_TO_RIGHT | RADEON_DST_Y_TOP_TO_BOTTOM));
	OUTREG((ulong) scr->mmio, RADEON_DST_Y_X, (r.min.y << 16) | r.min.x);
	OUTREG((ulong) scr->mmio, RADEON_DST_WIDTH_HEIGHT, (Dx(r) << 16) | Dy(r));

	radeonwaitidle(scr);

	return 1;
}

static int
radeonscroll(VGAscr* scr, Rectangle dst, Rectangle src)
{
	int		xs, ys, xd, yd, w, h;
	ulong	dp_cntl = 0x20;

	if(scr->mmio == nil)
		return 0;

//	iprint("radeon: hwscroll(dst:%R, src:%R)\n", dst, src);

	xd = dst.min.x;
	yd = dst.min.y;
	xs = src.min.x;
	ys = src.min.y;
	w = Dx(dst);
	h = Dy(dst);

	if (ys < yd) {
		ys += h - 1;
		yd += h - 1;
	} else
		dp_cntl |= RADEON_DST_Y_TOP_TO_BOTTOM;

	if (xs < xd) {
		xs += w - 1;
		xd += w - 1;
	} else
		dp_cntl |= RADEON_DST_X_LEFT_TO_RIGHT;

	radeonwaitfifo(scr, 6);
    OUTREG((ulong) scr->mmio, RADEON_DP_GUI_MASTER_CNTL, 
		dp_gui_master_cntl
		| RADEON_GMC_BRUSH_NONE
		| RADEON_GMC_SRC_DATATYPE_COLOR
		| RADEON_DP_SRC_SOURCE_MEMORY
		| RADEON_ROP3_S);
    OUTREG((ulong) scr->mmio, RADEON_DP_WRITE_MASK, 0xffffffff);
    OUTREG((ulong) scr->mmio, RADEON_DP_CNTL, dp_cntl);
    OUTREG((ulong) scr->mmio, RADEON_SRC_Y_X, (ys << 16) | xs);
	OUTREG((ulong) scr->mmio, RADEON_DST_Y_X, (yd << 16) | xd);
	OUTREG((ulong) scr->mmio, RADEON_DST_WIDTH_HEIGHT, (w << 16) | h);

	radeonwaitidle(scr);

//	iprint("radeon: hwscroll(xs=%d ys=%d xd=%d yd=%d w=%d h=%d)\n",
//		xs, ys, xd, yd, w, h);

	return 1;
}

static void
radeondrawinit(VGAscr* scr)
{
	ulong	bpp, dtype, i, pitch;
	ulong	clock_cntl_index, mclk_cntl, rbbm_soft_reset;

	if (scr->mmio == 0)
		return;

	switch (scr->gscreen->depth) {
	case 6:
    case 8:  dtype = 2; bpp = 1; break;
    case 15: dtype = 3; bpp = 2; break;
    case 16: dtype = 4; bpp = 2; break;
    case 32: dtype = 6; bpp = 4; break;
    default:
		return;
    }

	// disable 3D
    OUTREG((ulong) scr->mmio, RADEON_RB3D_CNTL, 0);

	// flush engine
    OUTREGP((ulong) scr->mmio, RADEON_RB2D_DSTCACHE_CTLSTAT, 
		RADEON_RB2D_DC_FLUSH_ALL,
	    ~RADEON_RB2D_DC_FLUSH_ALL);
    for (i = 0; i < 2000000; i++) {
		if (!(INREG((ulong) scr->mmio, RADEON_RB2D_DSTCACHE_CTLSTAT) & 
			RADEON_RB2D_DC_BUSY))
	    	break;
    }

	// reset 2D engine
    clock_cntl_index = INREG((ulong) scr->mmio, RADEON_CLOCK_CNTL_INDEX);

    mclk_cntl = INPLL((ulong) scr->mmio, RADEON_MCLK_CNTL);
    OUTPLL((ulong) scr->mmio, RADEON_MCLK_CNTL, 
		(mclk_cntl |
			RADEON_FORCEON_MCLKA |
			RADEON_FORCEON_MCLKB |
			RADEON_FORCEON_YCLKA |
			RADEON_FORCEON_YCLKB |
			RADEON_FORCEON_MC |
			RADEON_FORCEON_AIC));
    rbbm_soft_reset = INREG((ulong) scr->mmio, RADEON_RBBM_SOFT_RESET);

	OUTREG((ulong) scr->mmio, RADEON_RBBM_SOFT_RESET, 
		(rbbm_soft_reset |
			RADEON_SOFT_RESET_CP |
			RADEON_SOFT_RESET_HI |
			RADEON_SOFT_RESET_SE |
			RADEON_SOFT_RESET_RE |
			RADEON_SOFT_RESET_PP |
			RADEON_SOFT_RESET_E2 |
			RADEON_SOFT_RESET_RB));
	INREG((ulong) scr->mmio, RADEON_RBBM_SOFT_RESET);
	OUTREG((ulong) scr->mmio, RADEON_RBBM_SOFT_RESET, 
		(rbbm_soft_reset &
		~(RADEON_SOFT_RESET_CP |
			RADEON_SOFT_RESET_HI |
			RADEON_SOFT_RESET_SE |
			RADEON_SOFT_RESET_RE |
			RADEON_SOFT_RESET_PP |
			RADEON_SOFT_RESET_E2 |
			RADEON_SOFT_RESET_RB)));
	INREG((ulong) scr->mmio, RADEON_RBBM_SOFT_RESET);

    OUTPLL((ulong) scr->mmio, RADEON_MCLK_CNTL, mclk_cntl);
    OUTREG((ulong) scr->mmio, RADEON_CLOCK_CNTL_INDEX, clock_cntl_index);

	// init 2D engine
    radeonwaitfifo(scr, 1);
	OUTREG((ulong) scr->mmio, RADEON_RB2D_DSTCACHE_MODE, 0);

    pitch = (Dx(scr->gscreen->r) * bpp);
    radeonwaitfifo(scr, 4);
    OUTREG((ulong) scr->mmio, RADEON_DEFAULT_PITCH, pitch);
	OUTREG((ulong) scr->mmio, RADEON_DST_PITCH, pitch);
	OUTREG((ulong) scr->mmio, RADEON_SRC_PITCH, pitch);
	OUTREG((ulong) scr->mmio, RADEON_DST_PITCH_OFFSET_C, 0);

	radeonwaitfifo(scr, 3);
	OUTREG((ulong) scr->mmio, RADEON_DEFAULT_OFFSET, 0);
	OUTREG((ulong) scr->mmio, RADEON_DST_OFFSET, 0);
	OUTREG((ulong) scr->mmio, RADEON_SRC_OFFSET, 0);

    radeonwaitfifo(scr, 1);
	OUTREGP((ulong) scr->mmio, RADEON_DP_DATATYPE, 0, 
		~RADEON_HOST_BIG_ENDIAN_EN);

	radeonwaitfifo(scr, 1);
    OUTREG((ulong) scr->mmio, RADEON_DEFAULT_SC_BOTTOM_RIGHT, 
		(RADEON_DEFAULT_SC_RIGHT_MAX
			| RADEON_DEFAULT_SC_BOTTOM_MAX));

    dp_gui_master_cntl = 
		(dtype << RADEON_GMC_DST_DATATYPE_SHIFT)
		| RADEON_GMC_SRC_PITCH_OFFSET_CNTL
		| RADEON_GMC_DST_PITCH_OFFSET_CNTL
		| RADEON_GMC_CLR_CMP_CNTL_DIS;
	radeonwaitfifo(scr, 1);
    OUTREG((ulong) scr->mmio, RADEON_DP_GUI_MASTER_CNTL,
		dp_gui_master_cntl
		| RADEON_GMC_BRUSH_SOLID_COLOR
		| RADEON_GMC_SRC_DATATYPE_COLOR);

    radeonwaitfifo(scr, 7);
    OUTREG((ulong) scr->mmio, RADEON_DST_LINE_START,    0);
    OUTREG((ulong) scr->mmio, RADEON_DST_LINE_END,      0);
    OUTREG((ulong) scr->mmio, RADEON_DP_BRUSH_FRGD_CLR, 0xffffffff);
    OUTREG((ulong) scr->mmio, RADEON_DP_BRUSH_BKGD_CLR, 0x00000000);
    OUTREG((ulong) scr->mmio, RADEON_DP_SRC_FRGD_CLR,   0xffffffff);
    OUTREG((ulong) scr->mmio, RADEON_DP_SRC_BKGD_CLR,   0x00000000);
    OUTREG((ulong) scr->mmio, RADEON_DP_WRITE_MASK,     0xffffffff);

	radeonwaitidle(scr);

	scr->fill	= radeonfill;
	scr->scroll	= radeonscroll;
	hwaccel		= 1;

	scr->blank	= radeonblank;
	hwblank 	= 1;
}

// Hw overlay

static void
radeonovlctl(VGAscr* scr, Chan* c, void* data, int len)
{
	USED(scr, c, data, len);
}

static int
radeonovlwrite(VGAscr* scr, void* data, int len, vlong opt)
{
	USED(scr, data, len, opt);
	return -1;
}

static void
radeonflush(VGAscr* scr, Rectangle r)
{
	USED(scr, r);
}

// Export

VGAdev vgaradeondev = {
	"radeon",				/* name */

	radeonenable,			/* enable */
	0,						/* disable */
	0,						/* page */
	radeonlinear,			/* linear */

	radeondrawinit,			/* drawinit */
#ifdef RADEON_HW_ACCEL
	radeonfill,				/* fill */

	radeonovlctl,			/* ovlctl */
	radeonovlwrite,			/* ovlwrite */
	radeonflush,			/* flush */
#endif
};

VGAcur vgaradeoncur = {
	"radeonhwgc",			/* name */

	radeoncurenable,		/* enable */
	radeoncurdisable,		/* disable */
	radeoncurload,			/* load */
	radeoncurmove,			/* move */

	0						/* doespanning */
};
