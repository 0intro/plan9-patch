#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <mouse.h>

static Image *back, *bord, *high;

double
rad(double i)
{

	return (i * PI) / 180;
}

double
dist(Point p0, Point p1)
{

	return sqrt((p0.x - p1.x) * (p0.x - p1.x) + (p0.y - p1.y) * (p0.y - p1.y));
}

Point
cente(Point p0, Point p1, Point p2)
{
	double l, d;
	Point r0, r1;

	l = dist(p2, p0);
	r0.x = p2.x + (((p0.x - p2.x) / l) * l / 2);
	r0.y = p2.y + (((p0.y - p2.y) / l) * l / 2);

	l = dist(p1, p0);
	r1.x = p1.x + (((p0.x - p1.x) / l) * l / 2);
	r1.y = p1.y + (((p0.y - p1.y) / l) * l / 2);

	l = p2.y - r1.y - p1.y + r0.y;
	d = (double)(r0.y - r1.y) / l;

	r1.x = r0.x + d * (p1.x - r0.x);
	r1.y = r0.y + d * (p1.y - r0.y);

	return r1;
}

void
draw_pie(Point p, int r, Image *b, Image *w, Image *h, Menu *m, int a, int n)
{
	Point p0, p1;
	double d, e, sr;
	char *item;

	e = 360 / n;
	d = 0;
	sr = -1;

	ellipse(screen, p, r, r, 1, h, ZP);
	fillellipse(screen, p, r - 1, r - 1, w, ZP);
	if(a > 0)
		fillarc(screen, p, r, r, b, ZP, (a - 1) * e, e);

	p0 = p;
	p1 = p;

	p0.x += cos(rad(d)) * r;
	p0.y += sin(rad(d)) * r;
	p1.x += cos(rad(d + e)) * r;
	p1.y += sin(rad(d + e)) * r;
	
	sr = dist(cente(p, p0, p1), p);

	d = e / 2;

	while(--n >= 0)
	{
		p0 = p;
		item = strdup(m->item[n]);
		if(strlen(item) > 8)
		{
			item[8] = '\0';
			item[7] = '.';
			item[6] = '.';
			item[5] = '.';
		}

		p0.x += cos(rad(d)) * sr;
		p0.y += sin(rad(d)) * sr;
	
		p0.x -= stringwidth(font, item) / 2;
		p0.y -= font->height / 2;

		string(screen, p0, (n == (a - 1)) ? display->white : display->black, ZP, font, item);
		free(item);
		d += e;
	}

	return;
}

int
piemenuhit(int but, Mousectl *mc, Menu *menu, Screen *scr)
{
	Rectangle menur;
	Image *b, *backup, *back, *high, *bord;
	int maxwid, nitem, i, t;
	double cdeli, cpart, radius;
	Point /*mov,*/ p;
	char *item, set;

	back = allocimagemix(display, DPalegreen, DWhite);
	high = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DDarkgreen);
	bord = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DMedgreen);
	set = 'y';

	if(back == nil || high == nil || bord == nil)
	{
		set = 'n';
		freeimage(back);
		freeimage(high);
		freeimage(bord);
		back = display->white;
		high = display->black;
		bord = display->black;
	}

	replclipr(screen, 0, screen->r);
	maxwid = 0;
	nitem = 0;
	while(item = menu->item ? menu->item[nitem] : (*menu->gen)(nitem))
	{
		item = strdup(item);
		if(strlen(item) > 8)
			item[8] = '\0';

		i = stringwidth(font, item);
		if(i > maxwid)
			maxwid = i;
		free(item);
		nitem++;
	}
	if(menu->lasthit < 0 || menu->lasthit >= nitem)
		menu->lasthit = -1;

	p = mc->xy;
	radius = ((font->height / 2) + maxwid);
	radius += radius * nitem / 8;
	menur.min = Pt(p.x - radius - 1, p.y - radius - 1);
	menur.max = Pt(p.x + radius + 2, p.y + radius + 2);
	
	cpart = 360 / nitem;
	cdeli = cpart / 2;
	
	if(scr){
		b = allocwindow(scr, menur, Refbackup, DNofill);
		if(b == nil)
			b = screen;
		backup = nil;
	}else{
		b = screen;
		backup = allocimage(display, Rect(0, 0, Dx(menur), Dy(menur)), screen->chan, 0, DNofill);
		if(backup)
			draw(backup, backup->r, screen, nil, menur.min);
	}

	/*if(menu->lasthit != -1)
	{
		mov = p;
		mov.x += cos(rad(cdeli + cpart * menu->lasthit)) * (radius * 0.5);
		mov.y += sin(rad(cdeli + cpart * menu->lasthit)) * (radius * 0.5);
		moveto(mc, mov);
	}*/
	
	draw_pie(p, radius, high, back, bord, menu, menu->lasthit, nitem);

	while(mc->buttons & (1<<(but-1))){
		if(dist(p, mc->xy) < radius && dist(p, mc->xy) != 0)
		{
			cdeli = acos((p.x - mc->xy.x) / dist(p, mc->xy));
			if(mc->xy.y >= p.y)
				cdeli += rad(180);
			if(mc->xy.y < p.y)
				cdeli = rad(180) - cdeli;

			i = -1;
			t = -1;
			while(++t <= nitem)
			{
				if(cdeli < rad(t * cpart) && cdeli <= rad((t + 1) * cpart))
				{
					i = t;
					break;
				}
			}

			if(i != menu->lasthit || (menu->lasthit == -1 && i != -1))
			{
				menu->lasthit = i;
				draw_pie(p, radius, high, back, bord, menu, menu->lasthit, nitem);
			}
		} else {
			if(menu->lasthit != -1)
			{
				menu->lasthit = -1;
				draw_pie(p, radius, high, back, bord, menu, menu->lasthit, nitem);
			}
		}
		readmouse(mc);
	}
	if(b != screen)
		freeimage(b);
	if(backup){
		draw(screen, menur, backup, nil, ZP);
		freeimage(backup);
	}
	replclipr(screen, 0, screen->clipr);
	flushimage(display, 1);

	if(set == 'y')
	{
		freeimage(back);
		freeimage(bord);
		freeimage(high);
	}

	return menu->lasthit - 1;
}
