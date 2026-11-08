/*****************************************************************\
*       BBC BASIC Minimal Console Version: rosco-m68k (bare metal)  *
*       Derived from bbccon.c, Copyright (C) R. T. Russell, 2021-2025*
*                                                                 *
*       bbcrosco.c Main program, initialisation, keyboard, files  *
*       for the rosco_m68k with the rosco firmware and newlib:    *
*       - console on the firmware's serial port (mcCheckchar,     *
*         mcReadchar), polled: no threads, no termios;            *
*       - TIME from the firmware's 100 Hz tick (_TIMER_100HZ);    *
*       - files through newlib (SD card under /sd/);              *
*       - no SYS libraries, no inline assembler (yet).            *
*       See architecture/Spec.md S5.                              *
\*****************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <rosco_m68k/machine.h>
#include "bbccon.h"
#define HISTORY 100  // Number of items in command history
#define ESCTIME 200  // Milliseconds to wait for escape sequence
#define QRYTIME 1000 // Milliseconds to wait for cursor query response
#define QSIZE 32     // Twice longest expected escape sequence
#define myftell ftell
#define myfseek fseek
#define PLATFORM "rosco-m68k"
#define WM_TIMER 275
#define RTLD_DEFAULT NULL

#undef MAX_PATH
#define MAX(X,Y) (((X) > (Y)) ? (X) : (Y))

// Program constants:
#define SCREEN_WIDTH  640
#define SCREEN_HEIGHT 500
#define MAX_PATH 260
#define AUDIOLEN 441 * 4

// Global variables (external linkage):

void *userRAM = NULL ;
void *progRAM = NULL ;
void *userTOP = NULL ;
const int bLowercase = 0 ;    // Dummy
const char szVersion[] = "BBC BASIC for "PLATFORM" Console "VERSION ;
const char szNotice[] = "(C) Copyright R. T. Russell, "YEAR ;
char *szLoadDir ;
char *szLibrary ;
char *szUserDir ;
char *szTempDir ;
char *szCmdLine ;
intptr_t MaximumRAM = MAXIMUM_RAM ;
int UserTimerID ;
unsigned int palette[256] ;
void *TTFcache[1] ;

// Array of VDU command lengths:
static int vdulen[] = {
   0,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   1,   2,   5,   0,   0,   1,   9,   8,   5,   0,   1,   4,   4,   0,   2 } ;

// Keycode translation table:
static unsigned char xkey[64] = {
   0, 139, 138, 137, 136,   0, 131,   0, 130,   0,   0,   0,   0,   0,   0,   0,
 145, 146, 147, 148,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
   0,   0, 134, 135,   0, 132, 133,   0,   0,   0,   0,   0,   0,   0,   0, 149,
   0, 150, 151, 152, 153, 154,   0, 155, 156,   0,   0,   0,   0,   0,   0,   0 } ;

// Declared in bbcans.c:
void oscli (char *) ;
void xeqvdu (int, int, int) ;
char *setup (char *, char *, char *, char, unsigned char *) ;

// Declared in bbmain.c:
void error (int, const char *) ;
void text (const char *) ;	// Output NUL-terminated string
void crlf (void) ;		// Output a newline

// Declared in bbeval.c:
unsigned int rnd (void) ;	// Return a pseudo-random number

// Interpreter entry point:
int basic (void *, void *, void *) ;

// Forward references:
unsigned char osbget (void*, int*) ;
void osbput (void*, unsigned char) ;
void quiet (void) ;

// Dummy functions:
void gfxPrimitivesSetFont(void) { } ;
void gfxPrimitivesGetFont(void) { } ;
void RedefineChar(void) { } ;

// File scope variables:
static unsigned char inputq[QSIZE] ;
static volatile int inpqr = 0, inpqw = 0 ;

// Get from STDIN queue:
static int getinp (unsigned char *pinp)
{
	unsigned char bl = inpqr ;
	if (bl != inpqw)
	    {
		*pinp = inputq[bl] ;
		inpqr = (bl + 1) % QSIZE ;
		return 1 ;
	    }
	return 0 ;
}

// Move waiting characters from the serial port to the STDIN queue
// (on other platforms a thread does this).  With a DUART (rosco_m68k
// Classic v2) its channel A is read directly, as the firmware's own
// RECVCHAR does (it polls status bit 0, then reads the receive buffer):
// in r68k the firmware calls and the DUART model are separate input
// paths, and a character the DUART model takes would otherwise be lost.
#define DUART_A 0xF00001	// channel A base (SRA at +2, RBA at +6)
static volatile unsigned char *duart = NULL ;

static void pollinp (void)
{
	while (duart ? (duart[2] & 1) : mcCheckchar ())
	    {
		unsigned char bl = inpqw ;
		unsigned char al = (bl + 1) % QSIZE ;
		if (al == inpqr)
			return ;	// queue full: leave it in the UART
		inputq[bl] = duart ? duart[6] : (unsigned char) mcReadchar () ;
		inpqw = al ;
	    }
}

// A little-endian int from the VDU queue (byte-wise: any CPU, any alignment):
static int vqint (unsigned char *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int) p[3] << 24) ;
}

// Put event into event queue, unless full:
int putevt (heapptr handler, int msg, int wparam, int lparam)
{
	unsigned char bl = evtqw ;
	unsigned char al = bl + 8 ;
	int index = bl >> 2 ;
	if ((al == evtqr) || (eventq == NULL))
		return 0 ;
	eventq[index] = lparam ;
	eventq[index + 1] = msg ;
	eventq[index + 64] = wparam ;
	eventq[index + 65] = handler ;
	evtqw = al ;
	return 1 ;
}

// Get event from event queue, unless empty:
static heapptr getevt (void)
{
	heapptr handler ;
	unsigned char al = evtqr ;
	int index = al >> 2 ;
	flags &= ~ALERT ;
	if ((al == evtqw) || (eventq == NULL))
		return 0 ;
	lParam = eventq[index] ;
	iMsg = eventq[index + 1] ;
	wParam = eventq[index + 64] ;
	handler = eventq[index + 65] ;
	al += 8 ;
	evtqr = al ;
	if (al != evtqw)
		flags |= ALERT ;
	return handler ;
}

// Put keycode to keyboard queue:
static int putkey (char key)
{
	unsigned char bl = kbdqw ;
	unsigned char al = bl + 1 ;
	if ((key == 0x1B) && !(flags & ESCDIS))
	    {
		flags |= ESCFLG ;
		return 0 ;
	    }
	if (al != kbdqr)
	    {
		keybdq[(int) bl] = key ;
		kbdqw = al ;
		return 1 ;
	    }
	return 0 ;
}

// Get keycode (if any) from keyboard queue:
int getkey (unsigned char *pkey)
{
	unsigned char bl = kbdqr ;
	if (bl != kbdqw)
	    {
		*pkey = keybdq[(int) bl] ;
		kbdqr = bl + 1 ;
		return 1 ;
	    }
	return 0 ;
}

// Get millisecond tick count (the firmware's 100 Hz tick):
unsigned int GetTicks (void)
{
	return _TIMER_100HZ * 10 ;
}

static int kbchk (void)
{
	pollinp () ;
	return (inpqr != inpqw) ;
}

static unsigned char kbget (void)
{
	unsigned char ch = 0 ;
	getinp (&ch) ;
	return ch ;
}

static int kwait (unsigned int timeout)
{
	int ready ;
	timeout += GetTicks () ;
	while (!(ready = kbchk ()) && (GetTicks() < timeout))
		pollinp () ;
	return ready ;
}

// Returns 1 if the cursor position was read successfully or 0 if it was aborted
// (in which case *px and *py will be unchanged) or if px and py are both NULL.
int stdin_handler (int *px, int *py)
{
	int wait = (px != NULL) || (py != NULL) ;
	static char report[16] ;
	static char *p = report, *q = report ;
	unsigned char ch ;

	if (wait)
	    {
		printf ("\033[6n") ;
		fflush (stdout) ;
	    }

	do
	    {
		if (kbchk ())
		    {
			ch = kbget () ;
				
			if (ch == 0x1B)
			    {
				if (p != report)
					q = p ;
				*p++ = 0x1B ;
			    }
			else if (p != report)
			    {
				if (p < report + sizeof(report))
					*p++ = ch ;
				if ((ch >= 'A') && (ch <= '~') && (ch != '[') && (ch != 'O') &&
					((*(q + 1) == '[') || (*(q + 1) == 'O')))
				    {
					p = q ;
					q = report ;
								
					if (ch == 'R')
					    {
						int row = 0, col = 0 ;
						if (sscanf (p, "\033[%d;%dR", &row, &col) == 2)
						    {
							if (px) *px = col - 1 ;
							if (py) *py = row - 1 ;
							return 1 ;
						    }
					    }
					if (ch == '~')
					    {
						int key ;
						if (sscanf (p, "\033[%d~", &key))
							putkey (xkey[key + 32]) ;
					    }
					else
						putkey (xkey[ch - 64]) ;
				    }
			    }
			else
			    {
				putkey (ch) ;
				if (((kbdqr - kbdqw - 1) & 0xFF) < 50)
				    {
					p = report ;
					q = report ;
					wait = 0 ; // abort
				    }
			    }
		    }
		if ((wait || (p != report)) && !kwait (wait ? QRYTIME : ESCTIME))
		    {
			q = report ;
			while (q < p) putkey (*q++) ;
			p = report ;
			q = report ;
			wait = 0 ; // abort
		    }
	    }
	while (wait || (p != report)) ;
	return 0 ;
}

// Get text cursor (caret) coordinates:
void getcsr(int *px, int *py)
{
	if (!stdin_handler (px, py))
	    {
		if (px != NULL) *px = -1 ; // Flag unable to read
		if (py != NULL) *py = -1 ;
	    }
}

// SOUND Channel,Amplitude,Pitch,Duration
void sound (short chan, signed char ampl, unsigned char pitch, unsigned char duration)
{
}

// ENVELOPE N,T,PI1,PI2,PI3,PN1,PN2,PN3,AA,AD,AS,AR,ALA,ALD
void envel (signed char *env)
{
}

// Disable sound generation:
void quiet (void)
{
}

// Get pixel RGB colour:
int vtint (int x, int y)
{
	error (255, "Sorry, not implemented") ;
	return -1 ;
}

// Get current MODE number:
int getmodeno (void)
{
	return modeno ;
}

// Get nearest palette index:
int vpoint (int x, int y)
{
	error (255, "Sorry, not implemented") ;
	return -1 ;
}

int vgetc (int x, int y)
{
	error (255, "Sorry, not implemented") ;
	return -1 ;
}

int osbyte (int al, int xy)
{
	error (255, "Sorry, not implemented") ;
	return -1 ;
}

void osword (int al, void *xy)
{
	error (255, "Sorry, not implemented") ;
	return ;
}

// Get string width in graphics units:
int widths (unsigned char *s, int l)
{
	error (255, "Sorry, not implemented") ;
	return -1 ;
}

// ADVAL(n)
int adval (int n)
{
	if (n == -1)
	    	return (kbdqr - kbdqw - 1) & 0xFF ;
	error (255, "Sorry, not implemented") ;
	return -1 ;
}

// APICALL (SYS): plain calls, as in the llvm build
long long apicall_ (long long (*APIfunc) (size_t, size_t, size_t, size_t, size_t, size_t,
			size_t, size_t, size_t, size_t, size_t, size_t,
			double, double, double, double, double, double, double, double), PARM *p)
{
		return APIfunc (p->i[0], p->i[1], p->i[2], p->i[3], p->i[4], p->i[5],
				p->i[6], p->i[7], p->i[8], p->i[9], p->i[10], p->i[11],
			p->f[0], p->f[1], p->f[2], p->f[3], p->f[4], p->f[5], p->f[6], p->f[7]) ;
}
double fltcall_ (double (*APIfunc) (size_t, size_t, size_t, size_t, size_t, size_t,
			size_t, size_t, size_t, size_t, size_t, size_t,
			double, double, double, double, double, double, double, double), PARM *p)
{
		return APIfunc (p->i[0], p->i[1], p->i[2], p->i[3], p->i[4], p->i[5],
				p->i[6], p->i[7], p->i[8], p->i[9], p->i[10], p->i[11],
			p->f[0], p->f[1], p->f[2], p->f[3], p->f[4], p->f[5], p->f[6], p->f[7]) ;
}

// Call a function in the context of the GUI thread:
size_t guicall (void *func, PARM *parm)
{
	return apicall_ (func, parm) ;
}

// Check for Escape (if enabled) and kill:
void trap (void)
{
	stdin_handler (NULL, NULL) ;

	if (flags & KILL)
		error (-1, NULL) ; // Quit
	if (flags & ESCFLG)
	    {
		flags &= ~ESCFLG ;
		kbdqr = kbdqw ;
		quiet () ;
		if (exchan)
		    {
			fclose (exchan) ;
			exchan = NULL ;
		    }
		error (17, NULL) ; // 'Escape'
	    }
}

// Test for escape, kill, pause, single-step, flash and alert:
heapptr xtrap (void)
{
	trap () ;
	if (flags & ALERT)
		return getevt () ;
	return 0 ;
}

// Report a 'fatal' error:
void faterr (const char *msg)
{
	error (512, "") ;
}

// Read keyboard or F-key expansion:
static int rdkey (unsigned char *pkey)
{
	if (keyptr)
	    {
		*pkey = *keyptr++ ;
		if (*keyptr == 0)
			keyptr = NULL ;
		return 1 ;
	    }

	while (getkey (pkey))
	    {
		int keyno = 0 ;

		if ((*pkey >= 128) && (*pkey < 156))
			keyno = *pkey ^ 144 ;
		if (keyno >= 24)
			keyno -= 12 ;
		if (*pkey == 127)
		    {
			*pkey = 8 ;
			keyno = 24 ;
		    }
		if (keyno)
		    {
			keyptr = *((unsigned char **)keystr + keyno) ;
			if (keyptr)
			    {
				*pkey = *keyptr++ ;
				if (*keyptr == 0)
					keyptr = NULL ;
			    }
		    }
		return 1 ;
	    }
	return 0 ;
}

// Wait a maximum period for a keypress, or test key asynchronously:
int oskey (int wait)
{
	if (wait >= 0)
	    {
		unsigned int start = GetTicks () ;
		while (1)
		    {
			unsigned char key ;
			trap () ;
			if (rdkey (&key))
				return (int) key ;
			if ((unsigned int)(GetTicks () - start) >= wait * 10)
				return -1 ;
			pollinp () ;
		    }
	    }

	if (wait == -256)
		return 's' ;

	return 0 ;
}

// Wait for keypress:
unsigned char osrdch (void)
{
	unsigned char key ;
	if (exchan)
	{
		if (fread (&key, 1, 1, exchan))
			return key ;
		fclose (exchan) ;
 		exchan = NULL ;
	}

	if (optval >> 4)
		return osbget ((void *)(size_t)(optval >> 4), NULL) ;

	while (!rdkey (&key))
	{
		pollinp () ;
		trap () ;
	}
	return key ;
}

// Output byte to VDU stream:
void oswrch (unsigned char vdu)
{
	unsigned char *pqueue = vduq ;

	pollinp () ;	// the UART holds only a few characters

	if (optval & 0x0F)
	    {
		osbput ((void *)(size_t)(optval & 0x0F), vdu) ;
		return ;
	    }

	if (spchan)
	    {
		if (0 == fwrite (&vdu, 1, 1, spchan))
		    {
			fclose (spchan) ;
			spchan = NULL ;
		    }
	    }

	if (vduq[10] != 0)		// Filling queue ?
	    {
		vduq[9 - vduq[10]] = vdu ;
		vduq[10] -- ;
		if (vduq[10] != 0)
			return ;
		vdu = vduq[9] ;
		if (vdu >= 0x80)
		    {
			xeqvdu (vdu << 8, 0, 0) ;
			int ecx = (vdu >> 4) & 3 ;
			if (ecx == 0) ecx++ ;
			pqueue -= ecx - 9 ;
				for ( ; ecx > 0 ; ecx--)
					xeqvdu (*pqueue++ << 8, 0, 0) ;
			fflush (stdout) ;
			return ;
		    }
	    }
	else if (vdu >= 0x20)
	    {
		if ((vdu >= 0x80) && (vflags & UTF8))
		    {
			char ah = (vdu >> 4) & 3 ;
			if (ah == 0) ah++ ;
			vduq[10] = ah ;	// No. of bytes to follow
			vduq[9] = vdu ;
			return ;
		    }
		xeqvdu (vdu << 8, 0, 0) ;
		fflush (stdout) ;
		return ;
	    }
	else
	    {
		vduq[10] = vdulen[(int) vdu] ;
		vduq[9] = vdu ;
		if (vduq[10] != 0)
			return ;
	    }

// Mapping of VDU queue to UserEvent parameters,
// VDU 23 (defchr) has the most parameter bytes:
//
//          0  ^
// status-> 0  | ev.user.code
//          V  |
//          h  v
//          g  ^
//          f  | ev.user.data1
//          e  |
//          d  v
//          c  ^
//          b  | ev.user.data2
//          a  |
//  vduq->  n  v

	xeqvdu (vqint (pqueue + 8) & 0xFFFF, vqint (pqueue + 4), vqint (pqueue)) ;
}

// Prepare for outputting an error message:
void reset (void)
{
	vduq[10] = 0 ;	// Flush VDU queue
	keyptr = NULL ;	// Cancel *KEY expansion
	optval = 0 ;	// Cancel I/O redirection
	reflag = 0 ;	// *REFRESH ON
}

// Input and edit a string :
void osline (char *buffer)
{
	static char *history[HISTORY] = {NULL} ;
	static int empty = 0 ;
	int current = empty ;
	char *eol = buffer ;
	char *p = buffer ;
	*buffer = 0x0D ;
	int n ;

	while (1)
	    {
		unsigned char key ;

		key = osrdch () ;
		switch (key)
		    {
			case 0x0A:
			case 0x0D:
				n = (char *) memchr (buffer, 0x0D, 256) - buffer ;
				if (n == 0)
					return ;
				if ((current == (empty + HISTORY - 1) % HISTORY) &&
						(0 == memcmp (buffer, history[current], n)))
					return ;
				history[empty] = malloc (n + 1) ;
				memcpy (history[empty], buffer, n + 1) ;
				empty = (empty + 1) % HISTORY ;
				if (history[empty])
				    {
					free (history[empty]) ;
					history[empty] = NULL ;
				    }
				return ;

			case 8:
			case 127:
				if (p > buffer)
				    {
					char *q = p ;
					do p-- ; while ((vflags & UTF8) && (*(signed char*)p < -64)) ;
					oswrch (8) ;
					memmove (p, q, buffer + 256 - q) ;
				    }
				break ;

			case 21:
				while (p > buffer)
				    {
					char *q = p ;
					do p-- ; while ((vflags & UTF8) && (*(signed char*)p < -64)) ;
					oswrch (8) ;
					memmove (p, q, buffer + 256 - q) ;
				    }
				break ;

			case 130:
				while (p > buffer)
				    {
					oswrch (8) ;
					do p-- ; while ((vflags & UTF8) && (*(signed char*)p < -64)) ;
				    }
				break ;

			case 131:
				while (*p != 0x0D)
				    {
					oswrch (9) ;
					do p++ ; while ((vflags & UTF8) && (*(signed char*)p < -64)) ;
				    }
				break ;

			case 134:
				vflags ^= IOFLAG ;
				if (vflags & IOFLAG)
					printf ("\033[1 q") ;
				else
					printf ("\033[3 q\033[7 q") ;
				break ;

			case 135:
				if (*p != 0x0D)
				    {
					char *q = p ;
					do q++ ; while ((vflags & UTF8) && (*(signed char*)q < -64)) ;
					memmove (p, q, buffer + 256 - q) ;
				    }
				break ;

			case 136:
				if (p > buffer)
				    {
					oswrch (8) ;
					do p-- ; while ((vflags & UTF8) && (*(signed char*)p < -64)) ;
				    }
				break ;

			case 137:
				if (*p != 0x0D)
				    {
					oswrch (9) ;
					do p++ ; while ((vflags & UTF8) && (*(signed char*)p < -64)) ;
				    }
				break ;

			case 138:
			case 139:
				if (key == 138)
					n = (current + 1) % HISTORY ;
				else
					n = (current + HISTORY - 1) % HISTORY ;
				if (history[n])
				    {
					char *s = history[n] ;
					while (*p != 0x0D)
					    {
						oswrch (9) ;
						do p++ ; while ((vflags & UTF8) &&
							(*(signed char*)p < -64)) ;
					    }
					while (p > buffer)
					    {
						oswrch (127) ;
						do p-- ; while ((vflags & UTF8) &&
							 (*(signed char*)p < -64)) ;
				 	   }
					while ((*s != 0x0D) && (p < (buffer + 255)))
					    {
						oswrch (*s) ;
						*p++ = *s++ ;
					    }
					*p = 0x0D ;
					current = n ;
				    }
				break ;

			case 9:
			case 132:
			case 133:
			case 140:
			case 141:
				break ;

			default:
				if (p < (buffer + 255))
				    {
					if (key != 0x0A)
					    {
						oswrch (key) ;
					    }
					if (key >= 32)
					    {
						memmove (p + 1, p, buffer + 255 - p) ;
						*p++ = key ;
						*(buffer + 255) = 0x0D ;
						if ((*p != 0x0D) && (vflags & IOFLAG))
						    {
							char *q = p ;
							do q++ ; while ((vflags & UTF8) &&
								(*(signed char*)q < -64)) ;
							memmove (p, q, buffer + 256 - q) ;
						    }
					    }
				    }
		    }

		if (*p != 0x0D)
		    {
			oswrch (23) ;
			oswrch (1) ;
			for (n = 8 ; n != 0 ; n--)
				oswrch (0) ;
		    }
		n = 0 ;
		while (*p != 0x0D)
		    {
			oswrch (*p++) ;
			n++ ;
		    }
		for (int i = 0; i < (eol - p); i++)
			oswrch (32) ;
		for (int i = 0; i < (eol - p); i++)
			oswrch (8) ;
		eol = p ;
		while (n)
		    {
			if (!(vflags & UTF8) || (*(signed char*)p >= -64))
				oswrch (8) ;
			p-- ;
			n-- ;
		    }
		if (*p != 0x0D)
		    {
			oswrch (23) ;
			oswrch (1) ;
			oswrch (1) ;
			for (n = 7 ; n != 0 ; n--)
				oswrch (0) ;
		    }
	    }
}

// Get TIME
int getime (void)
{
	unsigned int n = GetTicks () ;
	if (n < lastick)
		timoff += 0x19999999 ;
	lastick = n ;
	return n / 10 + timoff ;
}

int getims (void)
{
	char *at ;
	time_t tt ;

	time (&tt) ;
	at = ctime (&tt) ;
	memcpy (accs + 0, at + 0, 4) ; // Day-of-week
	memcpy (accs + 4, at + 8, 4) ; // Day-of-month
	memcpy (accs + 7, at + 4, 4) ; // Month
	memcpy (accs + 11, at + 20, 4) ; // Year
	if (*(accs + 4) == ' ') *(accs + 4) = '0' ;
	memcpy (accs + 16, at + 11, 8) ; // Time
	accs[3] = '.' ;
	accs[15] = ',' ;
	return 24 ;
}

// Put TIME
void putime (int n)
{
	lastick = GetTicks () ;
	timoff = n - lastick / 10 ;
}

// Wait for a specified number of centiseconds:
// On some platforms specifying a negative value causes a task switch
void oswait (int cs)
{
	unsigned int start = GetTicks () ;
	if (cs < 0)
		return ;
	cs *= 10 ;
	do
	    {
		trap () ;
		pollinp () ;
	    }
	while ((unsigned int)(GetTicks () - start) < cs) ;
}


// MOUSE x%, y%, b%
void mouse (int *px, int *py, int *pb)
{
	if (px) *px = 0 ;
	if (py) *py = 0 ;
	if (pb) *pb = 0 ;
}

// MOUSE ON [type]
void mouseon (int type)
{
}

// MOUSE OFF
void mouseoff (void)
{
}

// MOUSE TO x%, y%
void mouseto (int x, int y)
{
}

// Get address of an API function:
void *sysadr (char *name)
{
	void *addr = NULL ;
	if (addr != NULL)
		return addr ; 
	return NULL ; // no dynamic libraries on the rosco
}

// Call an emulated OS subroutine (if CALL or USR to an address < 0x10000)
int oscall (int addr)
{
	int al = LE32LOAD(&stavar[1]) ;
	void *xy = (void *) ((size_t)LE32LOAD(&stavar[24]) | ((size_t)LE32LOAD(&stavar[25]) << 8)) ;
	switch (addr)
	    {
		case 0xFFE0: // OSRDCH
			return (int) osrdch () ;

		case 0xFFE3: // OSASCI
			if (al != 0x0D)
			    {
				oswrch (al) ;
				return 0 ;
			    }

		case 0xFFE7: // OSNEWL
			crlf () ;
			return 0 ;

		case 0xFFEE: // OSWRCH
			oswrch (al) ;
			return 0 ;

		case 0xFFF1: // OSWORD
			memcpy (xy + 1, &bbcfont[*(unsigned char*)(xy) << 3], 8) ;
			return 0 ;

		case 0xFFF4: // OSBYTE
			return (vgetc (0x80000000, 0x80000000) << 8) ;

		case 0xFFF7: // OSCLI
			oscli (xy) ;
			return 0 ; 

		default:
			error (8, NULL) ; // 'Address out of range'
	    }
	return 0 ;
}

// Request memory allocation above HIMEM:
heapptr oshwm (void *addr, int settop)
{
#ifdef _WIN32
	if ((addr < userRAM) ||
	    (addr > (userRAM + MaximumRAM)) ||
	    (NULL == VirtualAlloc (userRAM, addr - userRAM,
			MEM_COMMIT, PAGE_EXECUTE_READWRITE)))
		return 0 ;
#else
	if ((addr < userRAM) ||
	    (addr > (userRAM + MaximumRAM)))
		return 0 ;
#endif
	else
	    {
		if (settop && (addr > userTOP))
			userTOP = addr ;
		return (size_t) addr ;
	    }
}

// Get a file context from a channel number:
static FILE *lookup (void *chan)
{
	FILE *file = NULL ;

	if ((chan >= (void *)1) && (chan <= (void *)(MAX_PORTS + MAX_FILES)))
		file = (FILE*) filbuf[(size_t)chan] ;
	else
		file = (FILE*) chan ;

	if (file == NULL)
		error (222, "Invalid channel") ;
	return file ;
}

// Load a file into memory:
void osload (char *p, void *addr, unsigned int max)
{
	int n ;
	FILE *file ;
	if (NULL == setup (path, p, ".bbc", '\0', NULL))
		error (253, "Bad string") ;
	file = fopen (path, "rb") ;
	if (file == NULL)
		error (214, "File or path not found") ;
	n = fread (addr, 1, max, file) ;
	fclose (file) ;
	if (n == 0)
		error (189, "Couldn't read from file") ;
}

// Save a file from memory:
void ossave (char *p, void *addr, unsigned int len)
{
	int n ;
	FILE *file ;
	if (NULL == setup (path, p, ".bbc", '\0', NULL))
		error (253, "Bad string") ;
	file = fopen (path, "w+b") ;
	if (file == NULL)
		error (214, "Couldn't create file") ;
	n = fwrite (addr, 1, len, file) ;
	fclose (file) ;
	if (n < len)
		error (189, "Couldn't write to file") ;
}

// Open a file:
void *osopen (int type, char *p)
{
	int chan, first, last ;
	FILE *file ;
	if (setup (path, p, ".bbc", '\0', NULL) == NULL)
		return 0 ;
	if (type == 0)
		file = fopen (path, "rb") ;
	else if (type == 1)
		file = fopen (path, "w+b") ;
	else
		file = fopen (path, "r+b") ;
	if (file == NULL)
		return NULL ;

#ifdef _WIN32
	if (strchr (path+3, ':'))
#else
	if (0 == memcmp (path, "/dev", 4))
#endif
	    {
		first = 1 ;
		last = MAX_PORTS ;
	    }
	else
	    {
		first = MAX_PORTS + 1 ;
		last = MAX_PORTS + MAX_FILES ;
	    }

	for (chan = first; chan <= last; chan++)
	    {
		if (filbuf[chan] == 0)
		    {
			filbuf[chan] = file ;
			if (chan > MAX_PORTS)
				memset (&fcbtab[chan - MAX_PORTS - 1], 0, sizeof (FCB)) ;
			return (void *)(size_t)chan ;
		    }
	    }
	fclose (file) ;
	error (192, "Too many open files") ;
	return NULL ; // never happens
}

// Read file to 256-byte buffer:
static void readb (FILE *context, unsigned char *buffer, FCB *pfcb)
{
	int amount ;
	if (context == NULL)
		error (222, "Invalid channel") ;
//	if (pfcb->p != pfcb->o) Windows requires fseek to be called ALWAYS
	myfseek (context, (pfcb->p - pfcb->o) & 0xFF, SEEK_CUR) ;
#ifdef _WIN32
	long long ptr = myftell (context) ;
#endif
	amount = fread (buffer, 1, 256, context) ;
#ifdef _WIN32
	myfseek (context, ptr + amount, SEEK_SET) ; // filetest.bbc fix (32-bit)
#endif
	pfcb->p = 0 ;
	pfcb->o = amount & 0xFF ;
	pfcb->w = 0 ;
	pfcb->f = (amount != 0) ;
	while (amount < 256)
		buffer[amount++] = 0 ;
	return ;
}

// Write 256-byte buffer to file:
static int writeb (FILE *context, unsigned char *buffer, FCB *pfcb)
{
	int amount ;
	if (pfcb->f >= 0)
	    {
		pfcb->f = 0 ;
		return 0 ;
	    }
	if (context == NULL)
		error (222, "Invalid channel") ;
	if (pfcb->f & 1)
		myfseek (context, pfcb->o ? -pfcb->o : -256, SEEK_CUR) ;
#ifdef _WIN32
	long long ptr = myftell (context) ;
#endif
	amount = fwrite (buffer, 1, pfcb->w ? pfcb->w : 256, context) ;
#ifdef _WIN32
	myfseek (context, ptr + amount, SEEK_SET) ; // assemble.bbc asmtst64.bba fix
#endif
	pfcb->o = amount & 0xFF ;
	pfcb->w = 0 ;
	pfcb->f = 1 ;
	return (amount == 0) ;
}

// Close a single file:
static int closeb (void *chan)
{
	int result ;
	if ((chan > (void *)MAX_PORTS) && (chan <= (void *)(MAX_PORTS+MAX_FILES)))
	    {
		int index = (size_t) chan - MAX_PORTS - 1 ;
		unsigned char *buffer = (unsigned char *) filbuf[0] + index * 0x100 ;
		FCB *pfcb = &fcbtab[index] ;
		FILE *handle = (FILE *) filbuf[(size_t) chan] ;
		if (writeb (handle, buffer, pfcb))
			return 1 ;
	    }
	result = fclose (lookup (chan)) ;
	if ((chan >= (void *)1) && (chan <= (void *)(MAX_PORTS + MAX_FILES)))
		filbuf[(size_t)chan] = 0 ;
	return result ;
}

// Read a byte:
unsigned char osbget (void *chan, int *peof)
{
	unsigned char byte = 0 ;
	if (peof != NULL)
		*peof = 0 ;
	if (chan <= (void *)MAX_PORTS)
	    {
#ifdef _WIN32
		intptr_t file = _get_osfhandle (fileno (lookup (chan))) ;
		ReadFile ((HANDLE) file, &byte, 1, NULL, NULL) ;
#else
		read (fileno (lookup (chan)), &byte, 1) ;
#endif
		return byte ;
	    }
	if ((chan > (void *)MAX_PORTS) && (chan <= (void *)(MAX_PORTS+MAX_FILES)))
	    {
		int index = (size_t) chan - MAX_PORTS - 1 ;
		unsigned char *buffer = (unsigned char *) filbuf[0] + index * 0x100 ;
		FCB *pfcb = &fcbtab[index] ;
		if (pfcb->p == pfcb->o)
		    {
			FILE *handle = (FILE *) filbuf[(size_t) chan] ;
			if (writeb (handle, buffer, pfcb))
				error (189, "Couldn't write to file") ;
			readb (handle, buffer, pfcb) ;
			if ((pfcb->f & 1) == 0)
			    {
				if (peof != NULL)
					*peof = 1 ;
				return 0 ;
			    } 
		    }
		return buffer[pfcb->p++] ;
	    }
	if ((0 == fread (&byte, 1, 1, lookup (chan))) && (peof != NULL))
		*peof = 1 ;
	return byte ;
}

// Write a byte:
void osbput (void *chan, unsigned char byte)
{
	if (chan <= (void *) MAX_PORTS)
	    {
#ifdef _WIN32
		intptr_t file = _get_osfhandle (fileno (lookup (chan))) ;
		WriteFile ((HANDLE) file, &byte, 1, NULL, NULL) ;
#else
		write (fileno (lookup (chan)), &byte, 1) ;
#endif
		return ;
	    }
	if ((chan > (void *)MAX_PORTS) && (chan <= (void *)(MAX_PORTS+MAX_FILES)))
	    {
		int index = (size_t) chan - MAX_PORTS - 1 ;
		unsigned char *buffer = (unsigned char *) filbuf[0] + index * 0x100 ;
		FCB *pfcb = &fcbtab[index] ;
		if (pfcb->p == pfcb->o)
		    {
			FILE *handle = (FILE *) filbuf[(size_t) chan] ;
			if (writeb (handle, buffer, pfcb))
				error (189, "Couldn't write to file") ;
			readb (handle, buffer, pfcb) ;
		    }
		buffer[pfcb->p++] = byte ;
		pfcb->w = pfcb->p ;
		pfcb->f |= 0x80 ;
		return ;
	    }
	if (0 == fwrite (&byte, 1, 1, lookup (chan)))
		error (189, "Couldn't write to file") ;
}

// Get file pointer:
long long getptr (void *chan)
{
	myfseek (lookup (chan), 0, SEEK_CUR) ;
	long long ptr = myftell (lookup (chan)) ;
	if (ptr == -1)
		error (189, "Couldn't read file pointer") ;
	if ((chan > (void *)MAX_PORTS) && (chan <= (void *)(MAX_PORTS+MAX_FILES)))
	    {
		FCB *pfcb = &fcbtab[(size_t) chan - MAX_PORTS - 1] ;
		if (pfcb->o)
			ptr -= pfcb->o ;
		else if (pfcb->f & 1)
			ptr -= 256 ;
		if (pfcb->p)
			ptr += pfcb->p ;
		else if (pfcb->f & 0x81)
			ptr += 256 ;
	    }
	return ptr ;
}

// Set file pointer:
void setptr (void *chan, long long ptr)
{
	if ((chan > (void *)MAX_PORTS) && (chan <= (void *)(MAX_PORTS+MAX_FILES)))
	    {
		int index = (size_t) chan - MAX_PORTS - 1 ;
		unsigned char *buffer = (unsigned char *) filbuf[0] + index * 0x100 ;
		FCB *pfcb = &fcbtab[index] ;
		FILE *handle = (FILE *) filbuf[(size_t) chan] ;
		if (writeb (handle, buffer, pfcb))
			error (189, "Couldn't write to file") ;
		*(int *)pfcb = 0 ;
	    }
	if (-1 == myfseek (lookup (chan), ptr, SEEK_SET))
		error (189, "Couldn't set file pointer") ;
}

// Get file size:
long long getext (void *chan)
{
	FILE *file = lookup (chan) ;
	if (chan <= (void *)MAX_PORTS)
	    {
#ifdef _WIN32
		COMSTAT cs = {0} ;
		ClearCommError ((HANDLE) _get_osfhandle (fileno (file)), NULL, &cs) ;
		return cs.cbInQue ;
#elif defined(__linux__) || defined(__APPLE__)
		int waiting = 0 ;
		ioctl (fileno (file), FIONREAD, &waiting) ;
		return waiting ;
#else
		return 0 ;
#endif
	    }
	long long newptr = getptr (chan) ;
	myfseek (file, 0, SEEK_CUR) ;
	long long ptr = myftell (file) ;
	myfseek (file, 0, SEEK_END) ;
	long long size = myftell (file) ;
	if ((ptr == -1) || (size == -1))
		error (189, "Couldn't set file pointer") ;
	myfseek (file, ptr, SEEK_SET) ;
	if (newptr > size)
		return newptr ;
	return size ;
}

// Set file size (if possible):
void setext (void *chan, long long ptr)
{
	error (255, "Sorry, not implemented") ;
}

// Get EOF status:
long long geteof (void *chan)
{
	if ((chan > (void *)MAX_PORTS) && (chan <= (void *)(MAX_PORTS+MAX_FILES)))
	    {
		FCB *pfcb = &fcbtab[(size_t) chan - MAX_PORTS - 1] ;
		if ((pfcb->p != 0) && (pfcb->o == 0) && ((pfcb->f) & 1))
			return 0 ;
	    }
	return -(getptr (chan) >= getext (chan)) ;
}

// Close file (if chan = 0 all open files closed and errors ignored):
void osshut (void *chan)
{
	if (chan == NULL)
	    {
		int chan ;
		for (chan = MAX_PORTS + MAX_FILES; chan > 0; chan--)
		    {
			if (filbuf[chan])
				closeb ((void *)(size_t)chan) ; // ignore errors
		    }
		return ;
	    }
	if (closeb (chan))
		error (189, "Couldn't close file") ;
}

// Start interpreter:
int entry (void *immediate)
{

	accs = (char*) userRAM ;		// String accumulator
	buff = (char*) accs + ACCSLEN ;		// Temporary string buffer
	path = (char*) buff + 0x100 ;		// File path
	keystr = (char**) (path + 0x100) ;	// *KEY strings
	keybdq = (char*) keystr + 0x100 ;	// Keyboard queue
	eventq = (void*) keybdq + 0x100 ;	// Event queue
	filbuf[0] = (eventq + 0x200 / 4) ;	// File buffers n.b. pointer arithmetic!!

	farray = 1 ;				// @hfile%() number of dimensions
	fasize = MAX_PORTS + MAX_FILES + 4 ;	// @hfile%() number of elements

#ifndef _WIN32
	vflags = UTF8 ;				// Not |= (fails on Linux build)
#endif

	prand.l = (unsigned int) GetTicks () ;	/// Seed PRNG
	prand.h = (prand.l == 0) ;
	rnd () ;				// Randomise !

	memset (keystr, 0, 256) ;
	xeqvdu (0x1700, 0, 0x1F) ;		// initialise VDU drivers
	spchan = NULL ;
	exchan = NULL ;

	if (immediate == (void *) 1)
	    {
		text (szVersion) ;
		crlf () ;
		text (szNotice) ;
		crlf () ;
	    }

	return basic (progRAM, userTOP, immediate) ;
}

// Periodic timer (ON TIME, and ESCape detection while a program runs):
// not yet on the rosco (Plan P7); flags & ALERT is never set by a timer.
int StartTimer (int period)
{
	return 0 ;
}

void StopTimer (int timerid)
{
}

void SystemIO (int flag)
{
}

// Memory for BASIC: from the end of the program (newlib's heap) to the top
// of RAM the firmware reports, less room for the C stack and newlib.
#define STACK_RESERVE 0x20000	// C stack: the interpreter recurses
#define LIBC_RESERVE  0x10000	// newlib: FILE buffers, directory reads

static size_t memtop (void)
{
	size_t top = _SDB_MEM_SIZE ;
	if ((top < 0x80000) || (top > 0x1000000))
		top = 0x100000 ;	// not plausible: the board's 1 MB
	return top ;
}

int main (int argc, char* argv[])
{
	int exitcode = 0 ;
	void *immediate = (void *) 1 ;
	size_t size ;
	char *heap = sbrk (0) ;

	platform = 8 ;	// @platform%: rosco-m68k (value to agree with upstream)
	if ((_SDB_UART_BASE & 0xFFFFFF) == DUART_A)
		duart = (volatile unsigned char *) DUART_A ;
#ifdef R68K_DUART
	// Test build for r68k -d: its own firmware leaves the SDB's UART base
	// at 0, but its DUART model takes keys from stdin too (see Spec S4.3).
	duart = (volatile unsigned char *) DUART_A ;
#endif

	size = (memtop () - STACK_RESERVE - LIBC_RESERVE - (size_t) heap) & -256 ;
	while ((size >= MINIMUM_RAM) && (NULL == (userRAM = malloc (size))))
		size = (size - 0x10000) & -256 ;
	if (userRAM == NULL)
	    {
		printf ("Couldn't allocate memory\r\n") ;
		return 9 ;
	    }
	memset (userRAM, 0, PAGE_OFFSET + 0x100) ;	// no program (as mmap gives on Linux)
	MaximumRAM = size ;
	userTOP = userRAM + size ;
	progRAM = userRAM + PAGE_OFFSET ;
	szCmdLine = progRAM - 0x100 ;     // Must be immediately below default progRAM
	szTempDir = szCmdLine - 0x100 ;   // Strings must be allocated on BASIC's heap
	szUserDir = szTempDir - 0x100 ;
	szLibrary = szUserDir - 0x100 ;
	szLoadDir = szLibrary - 0x100 ;

	*szCmdLine = 0 ;
	strcpy (szTempDir, "/sd/") ;
	strcpy (szUserDir, "/sd/") ;
	strcpy (szLibrary, "/sd/lib/") ;
	strcpy (szLoadDir, "/sd/") ;

#ifdef BBC_LEMEM
	{
	void bbdata_lefix (void) ;
	bbdata_lefix () ;	// data-module tables little-endian
	}
#endif

	UserTimerID = StartTimer (250) ;

	flags = 0 ;
	exitcode = entry (immediate) ;

	if (UserTimerID)
		StopTimer (UserTimerID) ;

	fflush (stdout) ;
	_WARM_BOOT () ;		// back to the firmware
	return exitcode ;
}
