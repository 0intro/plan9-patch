#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <ctype.h>

/*
The cooked mode is implemented as an interpreter. In /sys/src/libdraw/cook.c:85 
is the program which implements the interpretation of events in a minilanguage. 
D and U mean up and down of a button and lower case 
letters are variables representing buttons. For example DiDjUjUi means pressing
one button, pressing another, releasing the second one pressed and then releasing the
first. T means the time passed is less than the quantum of time. 
M means the mouse is moved more than the quantum of movement. TTT> means
three quantum times or more.
Events generated can consume the state, or be preffixes (colouring one mouse struct or
all the generated in the event).

*/

#define MCHORDBITS(b,nth)	(((b)&0xf)<<(20 +(nth)*4))

enum {
	TIMERSTEP 	= 100,		/* in ms */
	SMALL 		= 1,		/* in pixels */
	MAXTOK		= 10,		/* for translation of events flag->TOKEN*/

	NOTMATCH=-1,
	IISPREFP=-2,
/* 
 * as explained in mouse(2), take care with MOUSEFLAGS
 */
	N =   0,
	L =   1,
	M =   2,
	R =   4,

	T_CANCEL 	= 2000
};

enum SMDATATYPE{
	TIMER,
	MOUSE
};

typedef struct Smdata Smdata;
struct Smdata {
	enum SMDATATYPE type;
	uint now;			/* miliseconds */
	Mouse;
};


int	verbstate;
char	state[1024];

extern Channel	*cookc;			/* chan(Mouse)[0] */
extern Channel	*modec;			/* chan(int)[0] */
extern int	mousemode;
static Smdata	smdata;

static Channel	*cntdownc = nil;	/* chan(int)[0], countdown start/end */

typedef struct Tatom Tatom;
struct Tatom {
	int mskold;
	int msknew;
	int negold;
	int negnew;
	char *tok;
};

static Tatom ttable[]={
	{L,L,0,1,"U1"},
	{M,M,0,1,"U2"},
	{R,R,0,1,"U3"},
	{L,L,1,0,"D1"},
	{M,M,1,0,"D2"},
	{R,R,1,0,"D3"},
};

typedef struct Pref Pref;
struct Pref {
	char *preffix;
	int isdone;	//has been done
	int ispref;  //when !preffix intepret this match as nop
	int isconsumer;	 //consume from the array
	int flags;
};

/*
	!ispreffix & !isconsumer == ispreffixonce == 00
	events,0,ispreffix,isconsumer,flags,   longest first.
	BUG: probably most are unnecessary.
	Order is important in consuming events.
*/

static Pref preffixes[]={
	{"DiT",0,0,0,MCLICK}, //click preffixonce
	{"DiUiTT>",0,0,1,MCLICK|MEND},	//click
	{"DiUiM",0,0,1,MCLICK|MEND},	//click
	{"DiUiTDiUi",0,0,1,MDOUBLE|MCLICK|MEND}, //double click
	{"DiDjDk",0,0,1,MDOUBLE|MCLICK|MCHORD|MEND}, //triple click-> double click
	{"DiM",0,1,0,MSELECT}, //slide preffix all
	{"DiMUi",0,0,1,MSELECT|MEND}, //slide
	{"DiDj",0,0,0,MCHORD},	//Chord preffix once
	{"DiDjM",0,0,1,MCLICK|MCHORD|MEND}, //Chord 
	{"DiDjUxUy",0,0,1,MCLICK|MCHORD|MEND}, //Chord 
	{"DiDjUi",0,0,1,MCLICK|MCHORD|MEND}, //Chord 
	{"DiDjUxM",0,0,1,MCLICK|MCHORD|MEND}, //Chord 
	{"DiDjMUx",0,0,1,MCLICK|MCHORD|MEND}, //Chord 
	{"DiDjMDx",0,0,1,MCLICK|MCHORD|MEND}, //Chord 
	{"DiDjDxM",0,0,1,MCLICK|MCHORD|MEND}, //Chord 
	{"DiDjUjDk",0,0,1,MDOUBLE|MCLICK|MCHORD|MEND}, //Chord Chord
	{"DiMDj",0,0,0,MSELECT|MCHORD}, //SLChord preffix once
	{"DiMDjUxUy",0,0,1,MSELECT|MCHORD|MEND}, //SLChord 
	{"DiMDjM",0,0,1,MSELECT|MCHORD|MEND}, //SLChord 
	{"DiMDjUjDk",0,0,1,MSELECT|MDOUBLE|MCLICK|MCHORD|MEND}, //Slide Chord Chord
//{"DiMDjUjDkM",0,0,1,MSELECT|MDOUBLE|MCLICK|MCHORD|MEND}, //Slide Chord Chord
};


static uint
msec(void)
{
	return nsec()/1000000;
}


static char *
transtable(Mouse mold, Mouse mnew)
{
	Tatom *t;
	char *data;
	int bold,bnew;
	int cndold,cndnew;
	int i;

	bold=mold.buttons;
	bnew=mnew.buttons;

	data=malloc(MAXTOK*nelem(ttable)+1);

	if(!data)
		sysfatal("transtable: Error mallocating data %r");
	else
		data[0]='\0';

	for(i=0;i<nelem(ttable);i++){
		t=&ttable[i];
		cndold=bold&t->mskold;
		cndnew=bnew & t->msknew;
		if(t->negold)
			cndold=!cndold;
		if(t->negnew)
			cndnew=!cndnew;
		if(cndold && cndnew)
			strcat(data,t->tok);
	}
	if(data[0])
		return data;
	else
		return nil;
}


static int
ismove(Mouse m1, Mouse m2)
{
	Rectangle near;
	int x0, y0, x1, y1;

	/* build a rectangle centered in ma->xy with sides aprox 2*SMALL */
	x0 = (m1.xy.x > SMALL) ? m1.xy.x-SMALL : m1.xy.x ;
	y0 = (m1.xy.y > SMALL) ? m1.xy.y-SMALL : m1.xy.y ;
	x1 = m1.xy.x + SMALL ;
	y1 = m1.xy.y + SMALL ;
	near = Rect(x0, y0, x1, y1);

	return(!ptinrect(m2.xy, near));
}


static int
commonpref(char *s1, char *s2)
{
	int len,i;
	
	len=strspn(s1,s2);
	for(i=0;i<len;i++){
		if(s1[i]!=s2[i])
			break;
	}
	return(i);
}

//returns flags codified on 
static int
isvar(char *pat, char *in, char *vars)
{
	int idx,val;

	//print("isvar(\"%s\",\"%s\")\n",pat,in);
	idx=(int)*pat-(int)'a';
	val=(int)*in-(int)'0';
	if(islower(*pat) && isdigit(*in)){
		if(!vars[idx]){
			vars[idx]=*in;
			return(val);
		}
		else if(vars[idx] && (vars[idx]==*in)){
			return(val);
		}
		else
			return(0);
	}
	else
		return(0);

}

static int
chordb(int flags, int b)
{
	if (!(flags&MCHORD0))
		return flags|MCHORDBITS(b, 0);
	if ((flags&MCHORD0) == MCHORDBITS(b, 0))
		return flags;
	if (!(flags&MCHORD1))
		return flags|MCHORDBITS(b, 1);
	if ((flags&MCHORD1) == MCHORDBITS(b, 1))
		return flags;
	if (!(flags&MCHORD2))
		return flags|MCHORDBITS(b, 2);
	if ((flags&MCHORD2) == MCHORDBITS(b, 2))
		return flags;
	return flags;
}

static long
imatch(char *pattern, char *input, int *flags)
{
	char vars[(int)'z'-(int)'a'+1]; //possible indexes
	char *pat, *in;
	int lmatch,lin,lpat;
	int res, ninrow,fl, isteing, justt;   //teing means matching in=T against pat=none


	ninrow=0;
	//if(verbose)
	//	print("imatch(\"%s\",\"%s\")\n",pattern,input);

	pat=pattern;
	in=input;
	lin=strlen(input);
	isteing=0;  
	justt=0;
	memset(vars,0,sizeof(vars));
	lmatch=0;


	for(;;){
		if(*pat!='M'){
			res=commonpref(pat,in);
			lmatch+=res;
			pat+=res;
			in+=res;
			lpat=strlen(pat);
			if(lmatch>=lin && justt && lpat>0){
				//		print("!length lmatch=%d lin=%d\n",lmatch,lin);
				return(NOTMATCH);
			}
			else if(lmatch>=lin && lpat>0 && (*pat!='>')){
				//		print("!length lmatch=%d lpat=%d\n",lmatch,lpat);
				return(IISPREFP);
			}
			else if(lmatch>=lin && !justt){
				//		print("!length lmatch=%d lin=%d\n",lmatch,lin);
				return(lmatch);
			}
			
		} 
		else
			res=0;

		if(!lmatch && (*in=='T')){
			justt=1;
		}


		if(res && (in[-1]=='T')){
			isteing=1;
		}

		//print("*pat:%c, res: %d\n",*pat,res);
		switch(*pat){
		case 'M':
			
			if((*in=='T')&& !isteing){
				in++;
				lmatch++;
				break;
			}else if (*in=='M'){
				in++;
				ninrow++;
				lmatch++;
				justt=0;
				break;
			}
			else if((*in!='M') && (ninrow!=0)){
				pat++;
				ninrow=0;
				justt=0;
			}
			else if(*in=='\0'){
				return(IISPREFP);
			}
			else
				return(NOTMATCH);
			isteing=0;
		break;
		case 'T':
			pat++;
			justt=0;
		break;
		case '>':
			if((lmatch>0) && (*in=='T') && (in[-1]=='T')&& (pat[-1]=='T')){
				in++;
				lmatch++;
				justt=0;
			}
			else if((lmatch>0) && (in[-1]=='T')&& (pat[-1]=='T') && !justt){
				pat++;
				justt=0;
			} 
			else if(justt)
				return(IISPREFP);
			else
				return(NOTMATCH);
			isteing=0; //not teing, matched an explicit rule
		break;
		default:	//variables
			if((*in=='T') && !isteing){
				in++;
				lmatch++;
			} 
			else if((lmatch>0)&&(*pat=='\0')){
				return(lmatch);
			}
			else if(fl=isvar(pat,in,vars)){
				*flags|=0x1<<(fl-1);
				*flags |= chordb(*flags, 0x1<<(fl-1)); //XXX
				in++;
				pat++;
				lmatch++;
				justt=0;
			}
			else{
				return(NOTMATCH);
			}
			isteing=0;
		break;
		}
	}
	return(NOTMATCH);
}


static char *
translate(Smdata *i,Mouse mold,int old)	
{
	char data[10],*res, *b;
	Mouse mnew;


	data[0]='\0';
	mnew = i->Mouse;
	if(i->type==TIMER){
		if ((i->now-old) < T_CANCEL) {
				strcat(data,"T");
		}
		
	}
	else{
		//print("!timer\n");
		if(ismove(mold,mnew)){
			strcat(data,"M");
		}
		if(b=transtable(mold,mnew)){
			strcat(data,b);
			free(b);
		}
	}

/*	if(i->type==TIMER){
		if ((i->now-old) > T_CANCEL)
			strcat(data,"T?");
	}
*/
	res=strdup(data);
	
	
	return(res);
}

static void
clean(void)
{
	int j;
	for(j=0;j<nelem(preffixes);j++)
		preffixes[j].isdone=0;
}

static void
consume(int len)
{
	memmove(state,state+len,strlen(state)+1);
}

/* 
 * if an event is detected, *m is actualized with the cooked values 
 * and "1" is returned. else *m is leaved intact and "0" is returned
 */
static int
cooker(Smdata *i)	
{
		
	static uint old;	/* msec() output */
	static Mouse mold;  /*start and end*/
	int yetnotmatch, matchall, matchpref, keepstate,output;
	Mouse m;	
	char *data;
	int j,lmatch,fl,slen;
	Pref *pref;

	fl=0;
	output=0;
	keepstate=0;
	m=i->Mouse;

	data=translate(i,mold,old);
	if(i->type!=TIMER){
		mold=m;
	}

	i->Mouse.buttons&=!MBUTTONS;
	if((i->type==TIMER) && (data[0]!='?')){
		old=i->now;
	}
	if(!data && (data[0]=='\0'))
		return(0);
	if((i->type!=TIMER) || (data[0]!='?'))
		nbsend(cntdownc, nil);	//activate timer if we get something

	strcat(state,data);
	free(data);
	slen=strlen(state);
	if(verbstate && (i->type!=TIMER))
		print("STATE: '%s'\n",state);
	if(state[0]=='\0')
		return(0);
	for(j=0;j<nelem(preffixes);j++){
		fl=0;
		pref=&preffixes[j];
		lmatch=imatch(pref->preffix,state,&fl);
		matchall=(lmatch==slen)&&(lmatch>0);
		yetnotmatch=(lmatch==IISPREFP);
		matchpref=(lmatch<slen)&&(lmatch>0);
		i->Mouse.buttons|=fl;
		fl=0;

		//preffix for all
		if((matchall||matchpref) && pref->ispref ){
			i->Mouse.buttons|=pref->flags;
			pref->isdone=1;
			if(verbstate)
				print("PREF %s %s\n",pref->preffix,state);
			output=1;
		}
		if(matchpref && !pref->ispref && !pref->isconsumer && !pref->isdone)
		{	
			if(verbstate)
				print("ONCE PREF %s %s\n",pref->preffix,state);
			pref->isdone=1;
			i->Mouse.buttons|=pref->flags;
			output=1;
		}
		if(matchall && pref->isconsumer){
			if(verbstate)
				print("MATCHALL %s\n",pref->preffix);
			i->Mouse.buttons|=pref->flags;
			if(pref->isconsumer){
				if(verbstate)
					print("CONSUME %s\n",state);
				consume(lmatch);
			}
			if(!pref->ispref)
				clean();
			
			output=1;
		}

		if(yetnotmatch){  //is on the way to matching
			keepstate=1;
			continue;
		}
		
	}
	if(!keepstate){
		if(verbstate && (strlen(state)>=2))
			print("NOMATCH: '%s'\n",state);
		consume(strlen(state));
		clean();
	}
	if(output){
		/* UGLY: clear MCHORD bits
		 * when event is not a chord.
		 */
		if (!(i->Mouse.buttons&MCHORD))
			i->Mouse.buttons &= ~MCHORDALL;
		return(1);
	} else
		return(0);
}


/*
 * not only cook but also implements mode changes
 */
static void
cookthread(void *arg)
{
	enum {
			ACDOWN,
			ACOOK,
			AMODE,
			NALT
	};

	Mousectl	*mc;
	Mouse 		m;
	int 		mode;
	static Alt alts[NALT+1];

	mc = arg;
	threadsetname("cookthread");

	alts[ACDOWN].c 	= cntdownc;
	alts[ACDOWN].v 	= nil;
	alts[ACDOWN].op	= CHANRCV;
	alts[ACOOK].c 	= cookc;
	alts[ACOOK].v 	= &m;
	alts[ACOOK].op	= CHANRCV;
	alts[AMODE].c	= modec;
	alts[AMODE].v	= &mode;
	alts[AMODE].op	= CHANRCV;
	alts[NALT].op	= CHANEND;

	for (;;) {
		switch(alt(alts)) {
			case ACDOWN:
				smdata.type = TIMER;
				smdata.now = msec();
				if (cooker(&smdata)) {
					send(mc->c, &(smdata.Mouse));
					/*
					 * mc->Mouse is updated after send so it doesn't have 
					 * wrong value if we block during send.
					 * This means that programs should receive from mc->Mouse 
					 * (see readmouse() above) if they want full synchrony.
					 */
					mc->Mouse = smdata.Mouse;
				}
				break;
			case ACOOK:
				smdata.type = MOUSE;
				smdata.Mouse = m;
				if (cooker(&smdata)) {
					send(mc->c, &(smdata.Mouse));
					mc->Mouse = smdata.Mouse;
				}
				break;
			case AMODE:
				mousemode = mode;
				send(modec, &mousemode);
				break;			
		} /* end of switch */
	} /* end of for */
}


static void
cntdownproc(void*)
{
	rfork(RFFDG);
	threadsetname("cntdownproc");
	recv(cntdownc, nil);
	for(;;) {
		sleep(TIMERSTEP);
		send(cntdownc, nil);
		recv(cntdownc, nil);
	}
}

/*
 * mode change do not care if the apication is
 * expecting for some MEND flag so take care when
 * you call setmousemode(MRAW) .
 */ 
int
setmousemode(Mousectl *mc, int mode)
{
	switch(mode) {
	case MCOOKED:
		consume(strlen(state));
		clean();
		/* create thread & proc on first call to cooked mode */
		if (cntdownc==nil) { 
			cntdownc = chancreate(sizeof(int), 0);
			cookc = chancreate(sizeof(Mouse), 0);
			proccreate(cntdownproc, nil, 8192);
			threadcreate(cookthread, mc, 8192);
		} /* fall through */
	case MRAW:
		send(modec, &mode);
		recv(modec, &mode);
		return(mode);
		break;
	default:
		return(-1);
	}
}
