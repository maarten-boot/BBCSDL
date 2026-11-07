#!/bin/sh
# Build the byte-swap test edition (Spec S2.8): Linux i386 console edition
# with -DBBC_SWAPTEST, in a Debian container.  Output: build/swap/bbcbasic.
#   console/rosco/swaptest.sh            (from the repository root)
set -e
TOP=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$TOP/build/swap"
python3 "$TOP/console/rosco/tools/mkdata.py" "$TOP/src/bbdata_arm_32.s" "$TOP/build/swap/bbdata.s"
docker run --rm -v "$TOP:/src:ro" -v "$TOP/build/swap:/out" debian:trixie-slim sh -ec '
apt-get -qq update >/dev/null
apt-get -qq install -y gcc gcc-multilib >/dev/null 2>&1
cd /out
I="-m32 -DBBC_SWAPTEST -I/src/include -w"
for f in bbmain bbexec bbeval; do gcc $I -O2 -ffast-math -fno-finite-math-only -c /src/src/$f.c -o $f.o; done
for f in bbccos bbccon; do gcc $I -Os -c /src/src/$f.c -o $f.o; done
gcc $I -Os -c /src/console/rosco/bbasmb_none.c -o bbasmb.o
gcc $I -Os -c /src/console/rosco/lefix.c -o lefix.o
gcc -m32 -c bbdata.s -o bbdata.o
gcc -m32 bbmain.o bbexec.o bbeval.o bbccos.o bbccon.o bbasmb.o lefix.o bbdata.o -ldl -lm -lrt -pthread -o bbcbasic
chown -R '"$(id -u):$(id -g)"' /out'
ls -l "$TOP/build/swap/bbcbasic"
