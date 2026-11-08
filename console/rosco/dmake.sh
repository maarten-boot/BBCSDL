#!/bin/sh
# Run make for the rosco-m68k build inside the rosco toolchain image
# (m68k-elf-rosco-gcc 13.3.1, newlib), pinned by digest (Spec S3.1, S9.2).
# --group-add 1001: the toolchain under /home/linuxbrew belongs to that group.
#   console/rosco/dmake.sh [make arguments]      (from anywhere)
IMAGE=roscopeco/rosco_m68k@sha256:91fd8f0cf4a852691e93fa5c2b7cb1090cc1227baf2a7aa356a8b51ae18ad205
TOP=$(cd "$(dirname "$0")/../.." && pwd)
OUT=rosco
case " $* " in *" R68K=1 "*) OUT=rosco-r68k ;; esac
mkdir -p "$TOP/build/$OUT"
python3 "$TOP/console/rosco/tools/mkdata.py" "$TOP/src/bbdata_arm_32.s" "$TOP/build/$OUT/bbdata.s" || exit 1
exec docker run --rm -u "$(id -u):$(id -g)" --group-add 1001 -v "$TOP:$TOP" -w "$TOP/console/rosco" \
	--entrypoint /usr/bin/env "$IMAGE" \
	PATH=/home/linuxbrew/.linuxbrew/bin:/usr/bin:/bin make "$@"
