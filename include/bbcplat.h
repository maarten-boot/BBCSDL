/******************************************************************\
*       BBC BASIC: platform switches for big-endian CPUs           *
*       (rosco-m68k port; see architecture/Spec.md S2.5)           *
\******************************************************************/

#ifndef BBCPLAT_H
#define BBCPLAT_H

// Platform switches for the rosco-m68k port (big-endian, strict alignment):
// BBC_LEMEM:  BASIC's memory stays little-endian although the CPU is not; all
//             multi-byte accesses to it go through the xLOAD/xSTORE macros.
// BBC_DOUBLE: a variant holds a 64-bit double (as on ARM), not a long double.
// BBC_NOREGS: esi and esp are ordinary globals, not register variables.
// BBC_SWAPTEST (PC test build, -m32): the macros store bytes reversed, which
//             exposes the same mixed native/macro accesses as on the 68k.
#if defined __m68k__ || defined BBC_SWAPTEST
#define BBC_LEMEM 1
#define BBC_DOUBLE 1
#define BBC_NOREGS 1
#endif

#ifdef BBC_LEMEM
// Byte-wise access, so alignment-safe; byte order: little-endian on a
// big-endian CPU, or reversed from native in the PC test build.
#ifdef BBC_SWAPTEST
#define LEB(i,n) ((n) - 1 - (i))	// big-endian on the little-endian PC
#else
#define LEB(i,n) (i)			// little-endian on the big-endian 68k
#endif
static inline unsigned int le_ld32 (const void *p)
{
	const unsigned char *b = (const unsigned char *) p ;
	return (unsigned int) b[LEB(0,4)] | ((unsigned int) b[LEB(1,4)] << 8) |
	       ((unsigned int) b[LEB(2,4)] << 16) | ((unsigned int) b[LEB(3,4)] << 24) ;
}
static inline void le_st32 (void *p, unsigned int v)
{
	unsigned char *b = (unsigned char *) p ;
	b[LEB(0,4)] = v ; b[LEB(1,4)] = v >> 8 ; b[LEB(2,4)] = v >> 16 ; b[LEB(3,4)] = v >> 24 ;
}
static inline unsigned short le_ld16 (const void *p)
{
	const unsigned char *b = (const unsigned char *) p ;
	return (unsigned short) (b[LEB(0,2)] | (b[LEB(1,2)] << 8)) ;
}
static inline void le_st16 (void *p, unsigned short v)
{
	unsigned char *b = (unsigned char *) p ;
	b[LEB(0,2)] = v ; b[LEB(1,2)] = v >> 8 ;
}
static inline unsigned long long le_ld64 (const void *p)
{
#ifdef BBC_SWAPTEST
	return ((unsigned long long) le_ld32 (p) << 32) | le_ld32 ((const char *) p + 4) ;
#else
	return le_ld32 (p) | ((unsigned long long) le_ld32 ((const char *) p + 4) << 32) ;
#endif
}
static inline void le_st64 (void *p, unsigned long long v)
{
#ifdef BBC_SWAPTEST
	le_st32 (p, v >> 32) ; le_st32 ((char *) p + 4, v) ;
#else
	le_st32 (p, v) ; le_st32 ((char *) p + 4, v >> 32) ;
#endif
}
// For files without BBC.h's macros (bbccon.c, bbccos.c, bbcrosco.c):
#define LE32LOAD(p)    ((int) le_ld32 (p))
#define LE32STORE(p,i) le_st32 ((void *)(p), (unsigned int)(i))
#else
#define LE32LOAD(p)    (*(int *)(p))
#define LE32STORE(p,i) (*(int *)(p) = (i))
#endif

// BBC_NOLLFMT: the C library's printf has no long long (%lld, %llX):
// the rosco newlib.  bbeval.c then formats 64-bit integers itself.
#ifdef __m68k__
#define BBC_NOLLFMT 1
#endif

// With a double variant, the long double maths functions are not wanted
// (on the 68k they would be 96-bit and emulated in software):
#ifdef BBC_DOUBLE
#define truncl trunc
#define floorl floor
#endif

#endif
