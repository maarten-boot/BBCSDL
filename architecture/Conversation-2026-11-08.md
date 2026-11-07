# Conversation log — 2026-11-08

(continued from Conversation-2026-11-07.md; the session crossed midnight)

## P3: Linux reference build
- Built the x86_64 console edition in `debian:trixie-slim` (gcc, nasm); binary in
  `build/host/bbcbasic` (build/ is git-ignored).
- Test runner `console/rosco/tools/bastest.py` runs bbc-basic-68k's corpus
  (test/bas, kw, guide: 237 tests, 2 `#typed` skipped).
- Findings while getting the runner to work (host console edition, not our target):
  - piped stdout is block-buffered → output appears only in 4 KB chunks;
  - stdin on a tty → it queries the cursor (`ESC[6n`) after each character and
    probes the width with `ESC[1;999H`;
  - with stdin a pipe and stdout a pseudo-terminal it behaves; typing costs
    about 7 ms a character (its input thread polls), so long tests take minutes.
  → runner: stdin pipe, stdout pty, each line typed when the prompt is back, `-j` jobs.

## Rosco OS layer (P4) — investigation
- rosco newlib (m68k-elf-rosco-gcc): stdio on the serial console, files under `/sd/`,
  `_TIMER_100HZ`, `mcCheckchar/mcReadchar/mcSendchar`, `_SDB_MEM_SIZE`, `_WARM_BOOT`.
- Plan: `console/rosco/bbcrosco.c` replaces `bbccon.c` (as the Pico port has its own
  file): no threads/termios/mmap/dlopen; polling input, firmware tick for TIME.
- D1 (b) reaches further than the heap macros:
  - `bbdata` tables (`sysvar`, `vdustr`, `ptfmt`, `b4fmt`) hold names packed as
    little-endian `int`s, and pointers → must be little-endian in memory;
  - `stavar[]` (`@%`, `A%`–`Z%`) is read natively by C (17 places);
  - ~80 native `(int*)`, `(heapptr*)`, `(short*)`, `(long long*)` casts in the 5 core files
    (bbexec.c 46); e.g. `bbccon.c:787` reads the VDU queue as `int`s, `getims`
    writes `int`s at odd offsets of `accs` (also an alignment fault on the 68010).
- Strategy (proposed in Spec S2.8): a **byte-swap test build on the PC**
  (`-m32 -DBBC_SWAPTEST`): the heap macros store bytes reversed, exactly as the LE
  macros do on the big-endian 68k. Any place that mixes macro and native access then
  fails the same way on the PC as on the rosco — but runs natively, in minutes.
  Must match the plain Linux reference output before moving to the 68k.
