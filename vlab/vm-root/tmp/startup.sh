#!/bin/bash
#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#

set -euo pipefail

export PATH=$(< $tmpdir/path)

# New root file system (this shadows the host /tmp until pivoting!)
mount --no-mtab vlab-root -t tmpfs /tmp

# mount special file systems into our new root
mkdir /tmp/proc /tmp/sys /tmp/dev
mount --no-mtab proc -t proc /tmp/proc
mount --no-mtab sysfs -t sysfs /tmp/sys
mount --no-mtab debugfs -t debugfs /tmp/sys/kernel/debug || true
mount --no-mtab devtmpfs -t devtmpfs /tmp/dev

# Ensure some standard directories exist
mkdir /tmp/etc
mkdir /tmp/var /tmp/var/log /tmp/var/empty /tmp/var/empty/sshd
mkdir /tmp/tmp /tmp/tmp/.host
mkdir /tmp/run
mkdir /tmp/root
mkdir /tmp/home

# preserve /etc/alternatives for Ubuntu/Debian
if test -d /etc/alternatives ; then
	mkdir /tmp/etc/alternatives
	mount --no-mtab --bind /etc/alternatives /tmp/etc/alternatives
fi

# Setup hostname now, we need it to load host_binds
# Linux < 5.19 will not parse the hostname= parameter and we need to set it here
if test -z "$(cat /tmp/proc/sys/kernel/hostname)" ; then
	echo "$hostname" > /tmp/proc/sys/kernel/hostname
else
	hostname="$(cat /tmp/proc/sys/kernel/hostname)"
fi

echo "$hostname" > /tmp/etc/hostname

# Valid after pivot_root
ln -s /proc/self/mounts /tmp/etc/mtab
ln -s /run /tmp/var/run

# mount the requested host_binds into our new root. Note that /tmp is shadowed,
# so bind-mount / elsewhere temporarily to ensure we can access everything.
#
# If source directory is undefined/empty, then just create an empty directory.
mount --no-mtab --bind / /tmp/tmp/.host
echo "$(cat /tmp/tmp/.host/$tmpdir/host_binds-$hostname)" | while read dest src ; do
    mkdir -p "/tmp/$dest"
    if [ -n "$src" ]; then
        mount --no-mtab --bind "/tmp/tmp/.host/$src" "/tmp/$dest"
    fi
done
umount --no-mtab /tmp/tmp/.host

# Pivot root, remount hostfs as RW and get rid of the old /dev
pivot_root /tmp/ /tmp/tmp/.host
mount --no-mtab -o remount,rw /tmp/.host
umount --no-mtab /tmp/.host/dev


# Now we have a clean root, continue

ip link set lo up

mount --bind -o rw /tmp/.host$VLAB_VAR_LOG_DIR /var/log/

chmod +x /tmp/.host$tmpdir/early.sh
/tmp/.host$tmpdir/early.sh

chmod 0600 /root
# used for pipe on tmpfiles operation, e.g source<(cmd)
ln -s /proc/self/fd /dev/fd

# pretend we have some entropy ...
PYTHONHASHSEED=0 python -c 'import fcntl; fd=open("/dev/random", "w"); fcntl.ioctl(fd.fileno(), 0x40045201, b"\x00\x01\x00\x00")'

mkdir /var/run/sshd


# Add /tmp/bin to override executables
export PATH=/tmp/bin:$PATH
mkdir /tmp/bin

# pretend there's no sudo - note we need to do this before setting $PATH
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
find . -not -type d -print0 | cpio --quiet -0 -R0:0 -d -p /

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
