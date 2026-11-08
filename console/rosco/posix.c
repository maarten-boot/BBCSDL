/******************************************************************\
*       BBC BASIC for the rosco-m68k: POSIX functions that the     *
*       rosco newlib does not provide (used by bbccos.c).          *
*       Phase 1: directories are not supported (Plan P7).          *
\******************************************************************/

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <rosco_m68k/machine.h>

static char cwd[260] = "/sd/" ;

// Wait at least 'us' microseconds (the tick is 10 ms; short waits return).
int usleep (unsigned int us)
{
	unsigned int ticks = us / 10000 ;
	unsigned int start = _TIMER_100HZ ;
	while ((unsigned int)(_TIMER_100HZ - start) < ticks)
		;
	return 0 ;
}

char *getcwd (char *buf, size_t size)
{
	if (strlen (cwd) >= size)
	    {
		errno = ERANGE ;
		return NULL ;
	    }
	strcpy (buf, cwd) ;
	return buf ;
}

int chdir (const char *path)
{
	errno = ENOSYS ;
	return -1 ;
}

int mkdir (const char *path, mode_t mode)
{
	errno = ENOSYS ;
	return -1 ;
}

int rmdir (const char *path)
{
	errno = ENOSYS ;
	return -1 ;
}

int chmod (const char *path, mode_t mode)
{
	return 0 ;	// FAT has no permissions to change
}

// bbccos.c translates VDU codes to ANSI sequences, and asks the terminal for
// the cursor position (ESC[6n), only when stdin and stdout are terminals.
// Phase 1 uses the plain console (VDU codes sent as they are), as the Linux
// edition does with piped input; build with -DROSCO_ANSI for the ANSI one.
// (newlib's own buffering decision uses _isatty, which this does not change.)
int isatty (int fd)
{
#ifdef ROSCO_ANSI
	return (fd >= 0) && (fd <= 2) ;
#else
	return 0 ;
#endif
}
