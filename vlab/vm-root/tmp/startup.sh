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
mount tmp -t tmpfs /tmp

# preserve /etc/alternatives for Ubuntu/Debian
if test -d /etc/alternatives ; then
	mkdir /tmp/alternatives
	mount --no-mtab --bind /etc/alternatives /tmp/alternatives

	mount tmp -t tmpfs /etc

	mkdir /etc/alternatives
	mount --no-mtab --move /tmp/alternatives /etc/alternatives
	rmdir /tmp/alternatives
else
	mount tmp -t tmpfs /etc
fi

mount tmp -t tmpfs /root
mount tmp -t tmpfs /var
mount tmp -t tmpfs /run

mkdir /tmp/.host/
mkdir /var/log /var/empty /var/empty/sshd
ln -s /run /var/run

mount -o remount,rw /
mount --bind / /tmp/.host/

chmod +x /tmp/.host$tmpdir/early.sh
/tmp/.host$tmpdir/early.sh

chmod 0600 /root
# used for pipe on tmpfiles operation, e.g source<(cmd)
ln -s /proc/self/fd /dev/fd

# pretend we have some entropy ...
PYTHONHASHSEED=0 python -c 'import fcntl; fd=open("/dev/random", "w"); fcntl.ioctl(fd.fileno(), 0x40045201, b"\x00\x01\x00\x00")'

mkdir /var/run/sshd

if test -z $(cat /proc/sys/kernel/hostname) ; then
	echo $hostname > /proc/sys/kernel/hostname
else
	hostname=$(cat /proc/sys/kernel/hostname)
fi
echo $hostname > /etc/hostname

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
TMPDIR=/tmp/.host$tmpdir
HOSTNAME=$hostname
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
export TMPDIR=/tmp/.host$tmpdir
export HOSTNAME=$hostname

cat > /etc/rsyslog.conf << EOF
\$umask 0000

module (load="imkmsg")
module (load="imuxsock")

*.*     /var/log/syslog
EOF
rsyslogd

which modprobe > /proc/sys/kernel/modprobe

if [ "$run" != "" ] ; then
    chmod +x $run
    echo "running script ${run/\/tmp\/.host}"
    exec $run
fi

exec bash
