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
