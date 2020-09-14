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
mount tmp -t tmpfs /home
mount tmp -t tmpfs /root
mount tmp -t tmpfs /var

mkdir /tmp/.host/
mkdir /var/log /var/run /var/empty /var/empty/sshd

mount -o remount,rw /
mount --bind / /tmp/.host/

chmod +x /tmp/.host$tmpdir/early.sh
/tmp/.host$tmpdir/early.sh

chmod 0600 /root

if which haveged >/dev/null 2>&1 ; then
    haveged
elif which rngd >/dev/null 2>&1 ; then
    # for some strange reason, this doesn't work so well
    # when we don't start it into foreground with -f ...
    rngd -f -r /dev/hw_random &
else
    echo "No entropy gathering daemon available - expect issues!"
fi

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

ln -s /tmp/.host$tmpdir/hosts /etc/hosts

cat >/root/.ssh/environment <<EOF
PATH=$PATH
EOF

$(which sshd) -f /tmp/sshd.conf >/dev/console >/dev/null 2>&1 &

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

touch /var/log/syslog
( while true ; do socat unix-listen:/dev/log file:/var/log/syslog ; done) &

which modprobe > /proc/sys/kernel/modprobe

if [ "$run" != "" ] ; then
    chmod +x $run
    echo "running script ${run/\/tmp\/.host}"
    exec $run
fi

exec bash
