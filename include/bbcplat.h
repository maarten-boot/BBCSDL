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

// With a double variant, the long double maths functions are not wanted
// (on the 68k they would be 96-bit and emulated in software):
#ifdef BBC_DOUBLE
#define truncl trunc
#define floorl floor
#endif

#endif
