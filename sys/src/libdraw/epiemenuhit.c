#include <u.h>
#include <libc.h>
#include <draw.h>
#include <event.h>

static Image *back, *bord, *high;

double
erad(double i)
{

	return (i * PI) / 180;
}

double
edist(Point p0, Point p1)
{

	return sqrt((p0.x - p1.x) * (p0.x - p1.x) + (p0.y - p1.y) * (p0.y - p1.y));
}

Point
ecente(Point p0, Point p1, Point p2)
{
	double l, d;
	Point r0, r1;

	l = edist(p2, p0);
	r0.x = p2.x + (((p0.x - p2.x) / l) * l / 2);
	r0.y = p2.y + (((p0.y - p2.y) / l) * l / 2);

	l = edist(p1, p0);
	r1.x = p1.x + (((p0.x - p1.x) / l) * l / 2);
	r1.y = p1.y + (((p0.y - p1.x) / l) * l / 2);

	l = p2.x - r1.x - p1.x + r0.x;
	d = (double)(r0.x - r1.x) / l;

	r1.x = r0.x + d * (p1.x - r0.x);
	r1.y = r0.y + d * (p1.y - r0.y);

	return r1;
}

void
edraw_pie(Point p, int r, Image *b, Image *w, Image *h, Menu *m, int a, int n)
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

	p0.x += cos(erad(d)) * r;
	p0.y += sin(erad(d)) * r;
	p1.x += cos(erad(d + e)) * r;
	p1.y += sin(erad(d + e)) * r;

	if(n > 1)
		sr = edist(ecente(p, p0, p1), p);
	else
		sr = 0;

	d = e / 2;

	while(--n >= 0){
		p0 = p;
		item = strdup(m->item[n]);
		if(strlen(item) > 8){
			item[8] = '\0';
			item[7] = '.';
			item[6] = '.';
			item[5] = '.';
		}

		p0.x += cos(erad(d)) * sr;
		p0.y += sin(erad(d)) * sr;
	
		p0.x -= stringwidth(font, item) / 2;
		p0.y -= font->height / 2;

		string(screen, p0, (n == (a - 1)) ? display->white : display->black, ZP, font, item);
		free(item);
		d += e;
	}

	return;
}

int
epiemenuhit(int but, Mouse *m, Menu *menu)
{
	Rectangle menur;
	Image *b, *back, *high, *bord;
	int maxwid, nitem, i, t;
	double cdeli, cpart, radius;
	Point p;
	char *item, set;

	back = allocimagemix(display, DPalegreen, DWhite);
	high = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DDarkgreen);
	bord = allocimage(display, Rect(0,0,1,1), screen->chan, 1, DMedgreen);
	set = 1;

	if(back == nil || high == nil || bord == nil){
		set = 0;
		back = display->white;
		high = display->black;
		bord = display->black;
	}

	replclipr(screen, 0, screen->r);
	maxwid = 0;
	nitem = 0;
	while(item = menu->item ? menu->item[nitem] : (*menu->gen)(nitem)){
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

	p = m->xy;
	radius = ((font->height / 2) + maxwid);
	radius += radius * nitem / 8;
	if(p.x + radius > screen->r.max.x)
		p.x = screen->r.max.x - radius;
	if(p.x - radius < screen->r.min.x)
		p.x = screen->r.min.x + radius;
	if(p.y + radius > screen->r.max.y)
		p.y = screen->r.max.y - radius;
	if(p.y - radius < screen->r.min.y)
		p.y = screen->r.min.y + radius;
	emoveto(p);

	menur.min = Pt(p.x - radius - 1, p.y - radius - 1);
	menur.max = Pt(p.x + radius + 2, p.y + radius + 2);
	
	cpart = 360 / nitem;
	cdeli = cpart / 2;
	
	b = allocimage(display, Rect(0, 0, Dx(menur), Dy(menur)), screen->chan, 0, DNofill);
	if(b == 0)
		b = screen;
	draw(b, b->r, screen, nil, menur.min);
	
	edraw_pie(p, radius, high, back, bord, menu, menu->lasthit, nitem);

	while(m->buttons & (1<<(but-1))){
		if(edist(p, m->xy) < radius && edist(p, m->xy) != 0){
			cdeli = acos((p.x - m->xy.x) / edist(p, m->xy));
			if(m->xy.y >= p.y)
				cdeli += erad(180);
			if(m->xy.y < p.y)
				cdeli = erad(180) - cdeli;

			i = -1;
			t = -1;
			while(++t <= nitem){
				if(cdeli < erad(t * cpart) && cdeli <= erad((t + 1) * cpart)){
					i = t;
					break;
				}
			}

			if(i != menu->lasthit || (menu->lasthit == -1 && i != -1)){
				menu->lasthit = i;
				edraw_pie(p, radius, high, back, bord, menu, menu->lasthit, nitem);
			}
		} else {
			if(menu->lasthit != -1){
				menu->lasthit = -1;
				edraw_pie(p, radius, high, back, bord, menu, menu->lasthit, nitem);
			}
		}
		flushimage(display, 1);
		*m = emouse();
	}

	draw(screen, menur, b, nil, ZP);
	if(b != screen)
		freeimage(b);

	replclipr(screen, 0, screen->clipr);

	if(set){
		freeimage(back);
		freeimage(bord);
		freeimage(high);
	}

	return menu->lasthit - 1;
}
