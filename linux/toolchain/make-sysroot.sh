#!/bin/bash
# make-sysroot.sh <amd64|arm64> [suite]
# Builds a GTK3 development sysroot from Ubuntu packages without root, into ~/lvtools/sysroot-<suite>-<arch>.
# Used so NeuralXP links against an old glibc/GTK and runs on Ubuntu 22.04+, Debian 12+, Mint 21+...
set -e
ARCH=$1; SUITE=${2:-jammy}
case $ARCH in
  amd64) MIRROR=http://archive.ubuntu.com/ubuntu; SEC=http://security.ubuntu.com/ubuntu ;;
  arm64) MIRROR=http://ports.ubuntu.com/ubuntu-ports; SEC=$MIRROR ;;
  *) echo "usage: $0 amd64|arm64 [suite]"; exit 1 ;;
esac
T=~/lvtools
A=$T/apt-$SUITE-$ARCH
SR=$T/sysroot-$SUITE-$ARCH
mkdir -p $A/etc/apt/apt.conf.d $A/etc/apt/preferences.d $A/var/lib/apt/lists/partial \
         $A/var/cache/apt/archives/partial $A/var/lib/dpkg
touch $A/var/lib/dpkg/status
KEY=/usr/share/keyrings/ubuntu-archive-keyring.gpg
cat > $A/etc/apt/sources.list <<EOF
deb [arch=$ARCH signed-by=$KEY] $MIRROR $SUITE main universe
deb [arch=$ARCH signed-by=$KEY] $MIRROR $SUITE-updates main universe
deb [arch=$ARCH signed-by=$KEY] $SEC $SUITE-security main universe
EOF
OPTS="-o Dir=$A -o Dir::State::status=$A/var/lib/dpkg/status -o APT::Architecture=$ARCH -o APT::Architectures::=$ARCH -o Debug::NoLocking=1 -o Dir::Etc::Parts=$A/etc/apt/apt.conf.d -o APT::Install-Recommends=0"
apt-get $OPTS update -qq
apt-get $OPTS install -y -qq --download-only libgtk-3-dev libc6-dev >/dev/null
rm -rf $SR; mkdir -p $SR
for d in $A/var/cache/apt/archives/*.deb; do dpkg-deb -x "$d" $SR; done
# absolute symlinks -> point inside the sysroot
find $SR -type l | while read l; do t=$(readlink "$l"); case "$t" in /*) ln -sfn "$SR$t" "$l";; esac; done
echo "$SR: $(du -sh $SR | cut -f1), $(ls $A/var/cache/apt/archives/*.deb | wc -l) packages, glibc $(ls $SR/lib/*/libc.so.6 $SR/usr/lib/*/libc.so.6 2>/dev/null | head -1 | xargs readlink -f | xargs basename)"
grep -h "^Version" $SR/usr/lib/*/pkgconfig/gtk+-3.0.pc
