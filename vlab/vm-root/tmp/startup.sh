#!/bin/bash
#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#

set -euo pipefail

export PATH=/tmp/bin:$(< $tmpdir/path)

ip link set lo up

mount proc -t proc /proc
mount sys -t sysfs /sys
mount debug -t debugfs /sys/kernel/debug
mount tmp -t tmpfs /etc
mount tmp -t tmpfs /tmp
mount tmp -t tmpfs /root
mount tmp -t tmpfs /var

mkdir /tmp/.host/
mkdir /var/log /var/run /var/empty /var/empty/sshd

mount -o remount,rw /
mount --bind / /tmp/.host/

chmod +x /tmp/.host$tmpdir/early.sh
/tmp/.host$tmpdir/early.sh

chmod 0600 /root

# pretend we have some entropy ...
PYTHONHASHSEED=0 python -c 'import fcntl; fd=open("/dev/random", "w"); fcntl.ioctl(fd.fileno(), 0x40045201, b"\x00\x01\x00\x00")'

mkdir /var/run/sshd

echo $hostname > /etc/hostname
echo $hostname > /proc/sys/kernel/hostname

# pretend there's no sudo - note we need to do this before setting $PATH
mkdir /tmp/bin
cat > /tmp/bin/which << EOF
#!/bin/sh

if [ "\$1" = "sudo" ] ; then
    exit 1
fi
exec $(which which) "\$@"
EOF
chmod +x /tmp/bin/which

# create iw symlink - avoid having symlinks in the git repo
ln -s ../../../iw/iw /tmp/bin/

# copy plugin-files
pushd /tmp/.host/$tmpdir/pluginfiles >/dev/null
find . -not -type d -not -type l -print0 | cpio --quiet -0 -R0:0 -d -p /

# copy vm-root dirs (after, so they can "win" over plugins)
echo $vmroots | tr ':' '\n' | while read dir ; do
	pushd /tmp/.host$dir >/dev/null
	find . -not -type d -not -type l -print0 | cpio --quiet -0 -R0:0 -d -p /
	for f in $(find . -type l) ; do ln -fs -T /tmp/.host$dir/$f /$f ; done
	popd >/dev/null
done

# need to fix some permissions - git doesn't preserve
chmod 0600 /tmp/sshd.key /tmp/sshd.rsa
chmod 0700 /root/.ssh
chmod 0700 /root/.ssh/config

ln -s /tmp/.host$tmpdir/hosts /etc/hosts

cat >/root/.ssh/environment <<EOF
PATH=$PATH
TMPDIR=$tmpdir
EOF

real_cfg="$(dirname $(which sshd))/../etc/ssh/sshd_config"
if test -f $real_cfg ; then
	(
		grep -v sftp /tmp/sshd.conf
		grep sftp $real_cfg
	) > /tmp/sshd.conf.tmp
	mv /tmp/sshd.conf.tmp /tmp/sshd.conf
fi

$(which sshd) -f /tmp/sshd.conf >/dev/console 2>&1 &

if ! [ -z "${customrootfs+x}" ] ; then
    test -d /tmp/.host/$customrootfs || (
        echo "specified rootfs $customrootfs doesn't exist"
        exit 2
    )
    # use cpio to copy over the rootfs folder contents
    (cd "/tmp/.host/$customrootfs" && find . -type f -print0 | cpio -L --quiet -v -0 --no-preserve-owner -d -p /)
fi

if [ "$addr" != "" ] ; then
    ip link set eth0 up || true
    ip addr add dev eth0 $addr/8 || true
fi

export HOME=/root/
export TMPDIR=$tmpdir

touch /var/log/syslog
( while true ; do socat unix-listen:/dev/log file:/var/log/syslog ; done) &

which modprobe > /proc/sys/kernel/modprobe

if [ "$run" != "" ] ; then
    chmod +x $run
    echo "running script ${run/\/tmp\/.host}"
    exec $run
fi

exec bash
