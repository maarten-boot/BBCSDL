# Spec — BBC BASIC console edition (BBCTTY) on rosco-m68k, compared with bbc-basic-68k

Status: v0.4 (2026-11-08). D1, D2 decided; D3 open.

## S1 Goal
S1.1 Port this project's portable C interpreter (console edition: `bbmain.c`,
     `bbexec.c`, `bbeval.c`, `bbccos.c`, `bbccon.c` + data module) to the rosco-m68k.
S1.2 Compare it with the assembly port `../bbc-basic-68k` (derived from BBCZ80).
S1.3 Out of scope: SDL editions, graphics, sound, Minix (for now).

## S2 Target
S2.1 CPU: MC68010 (rosco_m68k Classic), no FPU → software floating point.
S2.2 Runtime: bare metal on the rosco firmware (v2.42+), loaded as `ROSCODE1.BIN`
     from the SD card, console on the serial port.
S2.3 Memory: the board's 1 MB plus the 4 MB expansion (5 MB) is the reference
     configuration, as for bbc-basic-68k; must still start with 1 MB.
S2.4 The 68010 faults on odd-address word/long accesses: the port must never
     perform them (r68k emulates this fault). D2 = a: fixed in the source.
     The LE macros of S2.5.1 are byte-wise, so they are alignment-safe; the
     remaining raw casts are converted where they can be unaligned.
S2.5 Byte order (D1 = b): everything BASIC can see is little-endian, as in
     BBCZ80 and bbc-basic-68k: `!`/`?`/`^`/`$` indirection, variables and
     descriptors on the heap, tokenised programs, files.
S2.5.1 Done by redefining BBC.h's heap access macros (ILOAD/ISTORE, TLOAD/TSTORE,
     ULOAD/USTORE, SLOAD/SSTORE, VLOAD/VSTORE, CLOAD/CSTORE, NLOAD/NSTORE) under
     `#ifdef __m68k__` to byte-wise little-endian access (upstream hint).
S2.5.2 NLOAD/NSTORE (VAR, 10 bytes): type word `t` at offset 8 (LE 16-bit); if
     `t == -1` (string) `p` and `l` are two LE 32-bit words, otherwise bytes 0-7 are
     one LE 64-bit value (long long or double).
S2.5.3 Heap accesses that bypass the macros (raw casts) are found by tests and
     converted to the macros.
S2.5.4 Also little-endian in memory: `stavar[]` (static integer variables), the
     data-module tables (`sysvar`, `vdustr`, `ptfmt`, `b4fmt`, …) and every other
     structure BASIC can read with indirection. C code reads them through the macros.
S2.5.5 One switch in BBC.h selects this: `BBC_LEMEM` (set for `__m68k__` and for the
     test build `BBC_SWAPTEST`), plus `BBC_DOUBLE` (VAR holds a double) and
     `BBC_NOREGS` (esi/esp are ordinary globals).
S2.6 Floating point: `double` (64-bit) as on ARM, not `long double`
     (m68k 96-bit extended would be emulated in software and be slow).

S2.8 Byte-swap test build: Linux i386 (`-m32 -DBBC_SWAPTEST`), same 32-bit layout
     as the 68k; the macros store multibyte values byte-reversed. Its test output
     must equal the plain Linux reference (S6.3), which shows that no code mixes
     macro and native access.

## S3 Toolchain
S3.1 Primary: `m68k-elf-rosco-gcc` 13.3.1 (newlib) from the docker image
     `roscopeco/rosco_m68k@sha256:91fd8f0cf4a852691e93fa5c2b7cb1090cc1227baf2a7aa356a8b51ae18ad205`
     (pinned by digest; tag `latest`, created 2024-11-03), `-mcpu=68010`.
S3.2 Secondary (later phase): ACK from `../ack`. Expect C99/GNU-extension patches.
S3.3 Build products: `ROSCODE1.BIN` (+ ELF, map) under `build/rosco/`.

## S4 Emulators
S4.1 r68k (`../r68k`): functional tests, fast, inaccurate timing.
S4.2 MAME rosco-emulator (`../rosco-emulator`, image `rosco-emulator:local`):
     timing measurements (same CPU timing as the board).

## S5 Port features
S5.1 Interpreter: all of the non-graphics language the console edition supports.
S5.2 OS layer: serial console in/out, `TIME` from the firmware 100 Hz tick
     (`SDB_UPTICKS`), ESCape detection, files on the SD card (FAT) via the
     firmware/newlib, `*BYE` returns to the firmware.
S5.3 No threads, no `mmap`, no `dlopen`: replaced by single-threaded polling.
S5.4 Inline assembler: phase 1 none (`[` gives an error); a 68k assembler is a
     later decision (D3).
S5.5 Data module: `bbdata_m68k.c`, a 32-bit C module derived from
     `bbdata_wasm32.c`, used by the rosco build and the byte-swap test build; its
     tables are converted to little-endian at start-up when `BBC_LEMEM` is set.
S5.6 OS layer: `console/rosco/bbcrosco.c`, derived from `bbccon.c`.

## S6 Comparison (vs bbc-basic-68k)
S6.1 Size: code/data size of the binary, free memory for BASIC at start-up.
S6.2 Speed: the same benchmark programs (Rugg/Feldman BM1-BM8, sieve, as in
     bbc-basic-68k's benchmark work), timed on MAME; the board for key runs.
S6.3 Compatibility: the same BASIC test programs on both, outputs diffed;
     differences classified (bug / dialect difference BBC BASIC for Windows vs Z80).
S6.4 gcc vs ACK builds compared as well, when the ACK build exists.
S6.5 Report: Markdown in `architecture/` (or `tmp/`), tables per criterion.

## S7 Source layout
S7.1 Work lives on branch `mboot-68k-dev`. Port-specific files under `console/rosco/`
     (makefile, startup, OS glue) and `src/` (CPU-specific `bbdata_m68k.*`).
S7.2 Changes to the shared interpreter files are minimal and guarded by
     `#ifdef __m68k__`, so they can be offered upstream.

## S8 CI
S8.1 GitHub Actions on this fork: build in the pinned rosco image, smoke-test on r68k.

## S9 Project rules
S9.1 CI action steps are pinned to a full commit SHA, with the version tag in a
     comment; the latest workable version is used, or the reason it can't be is documented.
S9.2 Container images are pinned by digest for the same reason.

## Decisions
- **D1 Byte order** = (b) little-endian as seen by BASIC (S2.5), 2026-11-07, user + upstream hint.
- **D2 Unaligned accesses** = (a) fixed in the source (S2.4), 2026-11-07.
- **D3 Inline assembler** (S5.4) — OPEN.
