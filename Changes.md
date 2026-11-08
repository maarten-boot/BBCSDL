# Changes

Changes between releases of the rosco-m68k work on this fork (branch `mboot-68k-dev`).

## Unreleased
- Added planning docs in `architecture/` (Conversation log, Plan, Spec, Tests).
- rosco-m68k console edition (68010, bare metal, gcc 13): `console/rosco/`
  (bbcrosco.c OS layer, posix.c, data module generator, Makefile, dmake.sh).
- Big-endian support in the shared interpreter, neutral on little-endian
  platforms: `include/bbcplat.h` (BBC_LEMEM, BBC_DOUBLE, BBC_NOREGS,
  BBC_NOLLFMT), little-endian heap macros, `v.NLO`/`v.NHI`, stavar through
  the macros, NEXT/resloc/unstack stack slots read as pushed, getsub fix,
  64-bit integer formatting without `%lld`.
- Test tools: `console/rosco/tools/bastest.py` (shared corpus runner),
  `console/rosco/swaptest.sh` (PC byte-swap build).
