#!/bin/bash
# Extracts Ubuntu 22.04's GCC 12 (native x86_64 + aarch64 cross) into the jammy amd64 sysroot,
# so binaries are built exactly like on Ubuntu 22.04 (glibc 2.35 baseline, gcc-12 libstdc++).
# Creates wrappers in ~/lvtools/jammy-tc/bin. No root needed.
set -e
T=~/lvtools
A=$T/apt-jammy-amd64
SR=$T/sysroot-jammy-amd64
OPTS="-o Dir=$A -o Dir::State::status=$A/var/lib/dpkg/status -o APT::Architecture=amd64 -o APT::Architectures::=amd64 -o Debug::NoLocking=1 -o Dir::Etc::Parts=$A/etc/apt/apt.conf.d -o APT::Install-Recommends=0"
apt-get $OPTS install -y -qq --download-only gcc-12 g++-12 binutils \
    gcc-12-aarch64-linux-gnu g++-12-aarch64-linux-gnu binutils-aarch64-linux-gnu >/dev/null
for d in $A/var/cache/apt/archives/*.deb; do dpkg-deb -x "$d" $SR; done
find $SR -type l | while read l; do t=$(readlink "$l"); case "$t" in /*) [ -e "$SR$t" ] && ln -sfn "$SR$t" "$l" || true;; esac; done
# The cross compiler drags in its own arm64 libc (libc6-dev-arm64-cross). Its linker scripts use absolute
# paths outside our arm64 sysroot, so hide it: libc/crt then come from sysroot-jammy-arm64 (same glibc 2.35).
X=$SR/usr/aarch64-linux-gnu
[ -d $X/lib ] && rm -rf $X/lib.cross && mv $X/lib $X/lib.cross
rm -rf $X/include.cross  # (headers stay: they include libstdc++'s, and the libc ones are the same 2.35)

# Libraries the jammy compiler binaries need that the host may lack (never libc itself)
L=$T/jammy-tc/lib; rm -rf $T/jammy-tc; mkdir -p $L $T/jammy-tc/bin
for so in libisl.so.23 libmpc.so.3 libmpfr.so.6 libgmp.so.10 libzstd.so.1 libbfd-2.38-system.so libopcodes-2.38-system.so \
          libctf.so.0 libctf-nobfd.so.0 libbfd-2.38-aarch64.so libopcodes-2.38-aarch64.so; do
  f=$(find $SR/usr/lib/x86_64-linux-gnu $SR/lib/x86_64-linux-gnu -name "$so*" 2>/dev/null | head -1)
  [ -n "$f" ] && cp -L "$f" $L/$so
done
cp -L $SR/usr/lib/x86_64-linux-gnu/libbfd-*.so $SR/usr/lib/x86_64-linux-gnu/libopcodes-*.so $SR/usr/lib/x86_64-linux-gnu/libctf*.so.0 $L/ 2>/dev/null || true
for t in gcc g++; do
  for pfx in x86_64 aarch64; do
    if [ $pfx = x86_64 ]; then BDIR=$SR/usr/bin/; else BDIR=$SR/usr/aarch64-linux-gnu/bin/; fi
cat > $T/jammy-tc/bin/$pfx-linux-gnu-$t <<W
#!/bin/sh
export LD_LIBRARY_PATH="$L"
exec $SR/usr/bin/$pfx-linux-gnu-$t-12 -B$BDIR "\$@"
W
  done
done
for t in ar ranlib strip nm objdump readelf as ld; do
  for pfx in x86_64 aarch64; do
    if [ $pfx = x86_64 ]; then BDIR=$SR/usr/bin/; else BDIR=$SR/usr/aarch64-linux-gnu/bin/; fi
cat > $T/jammy-tc/bin/$pfx-linux-gnu-$t <<W
#!/bin/sh
export LD_LIBRARY_PATH="$L"
exec $SR/usr/bin/$pfx-linux-gnu-$t "\$@"
W
  done
done
chmod +x $T/jammy-tc/bin/*
cd /tmp
printf '#include <string>\n#include <random>\n#include <cmath>\n#include <cstdio>\nint main(int c,char**v){std::random_device r; float f=std::sqrt((float)c); printf("%%lu %%f\\n", std::stoul("42"), f); return 0;}\n' > jt.cpp
$T/jammy-tc/bin/x86_64-linux-gnu-g++ --sysroot=$SR -O2 jt.cpp -o jt64 -static-libstdc++ -static-libgcc && ./jt64
objdump -T jt64 | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
$T/jammy-tc/bin/aarch64-linux-gnu-g++ --sysroot=$T/sysroot-jammy-arm64 -O2 jt.cpp -o jta64 -static-libstdc++ -static-libgcc && file jta64
$T/jammy-tc/bin/aarch64-linux-gnu-objdump -T jta64 | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
