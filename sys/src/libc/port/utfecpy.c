#include <u.h>
#include <libc.h>

char*
utfecpy(char *to, char *e, char *from)
{
	char *end;

	if(to >= e)
		return to;
	end = memccpy(to, from, '\0', e - to);
	if(end == nil){
		end = e-1;
		if(end>to && end[-1]&0x80){
			if(end-2>=to && (end[-2]&0xE0)==0xC0){
				;
			}else if(end-3>=to && (end[-3]&0xF0)==0xE0){
				;
			}else while(end>to && (*--end&0xC0)==0x80)
				;
		}
		*end = '\0';
	}else{
		end--;
	}
	return end;
}
