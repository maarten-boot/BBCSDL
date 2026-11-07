/******************************************************************\
*       BBC BASIC: make the data module's tables little-endian     *
*       (BBC_LEMEM builds; see console/rosco/tools/mkdata.py)       *
\******************************************************************/

#include "bbcplat.h"

#ifdef BBC_LEMEM
extern unsigned char *bbdata_lefix_tab[] ;
extern int bbdata_lefix_cnt ;

// Reverse the bytes of each listed word (byte-wise: they may be unaligned).
void bbdata_lefix (void)
{
	static int done = 0 ;
	int i ;
	if (done)
		return ;
	done = 1 ;
	for (i = 0 ; i < bbdata_lefix_cnt ; i++)
	    {
		unsigned char *p = bbdata_lefix_tab[i], t ;
		t = p[0] ; p[0] = p[3] ; p[3] = t ;
		t = p[1] ; p[1] = p[2] ; p[2] = t ;
	    }
}
#endif
