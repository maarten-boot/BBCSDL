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

## Progress (night of 11-07/08)
- P3 done: Linux reference, 235 tests run, 2 timeouts (`kw/AUTO`, `bas/x03textprog`:
  both stay in AUTO mode, which only Escape ends) → `build/bas/host/`.
- P3a done: `include/bbcplat.h` (BBC_LEMEM/BBC_DOUBLE/BBC_NOREGS, truncl/floorl → double),
  LE byte-wise macros in BBC.h; data module generated from `bbdata_arm_32.s`
  (`tools/mkdata.py`, 151 words made LE at start-up by `lefix.c`).
  No multi-byte C-native variable lands on an odd address (checked with nm).
- P3b started: swap-test build works (`console/rosco/swaptest.sh`); corpus run vs host
  started in the background (results not yet looked at).
  Note: the swap build cannot be identical to the host — byte views (`?^A%`) are
  reversed by design, and data built byte-wise then read with a macro reads wrong
  there but right on the 68k. It finds native/macro mixing; r68k is the final judge.
- P4: `console/rosco/` Makefile + `dmake.sh` (pinned image, `--group-add 1001` for the
  linuxbrew toolchain), `bbcrosco.c` (from bbccon.c: polled serial input, 100 Hz tick,
  plain SYS calls, VDU-queue ints read byte-wise LE, getims via memcpy), `posix.c` stubs
  (usleep, getcwd; chdir/mkdir/rmdir ENOSYS). Links: ROSCODE1.BIN 266 KB
  (text 257 KB, data 4.6 KB, bss 100 KB). No compiler warnings.
- First r68k boot: the program starts and sends `ESC[6n` (newlib reports a tty, so
  bbccos uses ANSI mode and waits for a cursor report). Next: answer it in the runner
  or choose plain mode (isatty → 0) for phase 1.
- User: "going to bed" — stopped here.

## Next steps
1. Read the swap-test results (`build/bas/swap/`, diff vs `build/bas/host/`).
2. r68k: decide ANSI vs plain console mode; get to the `>` prompt (P5).
3. ESCape/ON TIME need a timer interrupt (chain the firmware tick) — P7.
4. Open questions for upstream: value of `@platform%` for the rosco (8 for now).

## Session resumed ("you can continue")
- Swap-test corpus run (rerun, the first was killed with the session):
  101 of 235 differ from the Linux reference — includes arrays (DIM) and PROC
  parameters, both later found and fixed on the 68k; outputs can explode
  (62 MB) → the runner needs an output cap (todo).
- Rosco build on r68k (`-d`), plain console mode (`isatty` → 0 in posix.c, as the
  Linux edition with piped input; `-DROSCO_ANSI` for ANSI): reaches the `>` prompt.
- Bugs found and fixed (all neutral on little-endian platforms):
  1. program area not zeroed (malloc) → "Bad program" → memset in bbcrosco.c;
  2. `VAR` union: code used `s.p`/`s.l` as the low/high halves of the 64-bit
     number → new `v.NLO`/`v.NHI` (BBC.h, endian-dependent); loadn/storen copy
     heap ↔ VAR with ULOAD/USTORE/NLOAD/NSTORE instead of memcpy;
  3. rosco newlib printf has no `%lld`/`%llX` → `fmtll()` in bbeval.c (BBC_NOLLFMT);
  4. NEXT read int stack slots as short/char → read the int and narrow it;
  5. `@%`, `A%`-`Z%` (stavar) read/written natively in C → ILOAD/ISTORE
     (LE32LOAD in console files; LE helpers moved to bbcplat.h);
  6. `getsub` read a C local through CLOAD → native read;
  7. `savloc` pushes a VAR as 3 native ints, `resloc`/`unstack` popped it with
     NLOAD → `stkvar()` pops field by field.
- Works on r68k: arithmetic, floats, strings, `?`/`!` little-endian (`?^A%`=&78),
  FOR/NEXT (int, float, STEP), arrays (1-D, 2-D, string, SUM), PROC/FN, LOCAL,
  recursion, `@platform%`=8, HIMEM-LOMEM = 4.6 MB with 5 MB RAM.
- r68k input finding (Spec S4.3): with `-d`, r68k's own firmware takes keys from
  stdin for the firmware calls, and the DUART model also takes one from stdin
  on a timer tick when its buffer is empty; nobody reads the DUART, so exactly one
  key per session is lost (position depends on timing). The real firmware 2.42
  polls the DUART itself (RECVCHAR, stage1/duart.asm), so on the board there is
  one path. r68k's firmware leaves the SDB UART base (0x418) at 0.
  Tried: real 2.42 ROM in r68k (boots, but finds no boot media); reading the DUART
  directly (`R68K=1` test build) — prints nothing yet, not understood.
  → Asked the user how to proceed (r68k is the user's repository).
