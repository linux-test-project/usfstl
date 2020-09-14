#!/bin/bash -e
#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#

exampledir=$(realpath $(dirname $0))
. $exampledir/common.lib

cat > $tmpdir/setup.sh << EOF
#!/bin/bash

set -euo pipefail

test -d /sys/module/mac80211_hwsim || modprobe mac80211-hwsim radios=1
while ! test -d /sys/class/net/wlan0 ; do sleep 0.1 ; done
ip link set wlan0 addr 02:00:00:00:00:\$1
iw wlan0 set type ibss
ip link set wlan0 up

iw wlan0 ibss join test-ibss 2412 fixed-freq aa:bb:cc:dd:ee:ff
# make sure IBSS starts up
sleep 1
ip addr add 192.168.1.\$1/24 dev wlan0
EOF

chmod +x $tmpdir/setup.sh

cat > $tmpdir/ping.sh << EOF
#!/bin/bash

set -euo pipefail

/tmp/.host/$tmpdir/setup.sh 01
ssh -Fnone -oStrictHostKeyChecking=no m2 /tmp/.host/$tmpdir/setup.sh 02
ping -i 10 -c 10 192.168.1.2
EOF

chmod +x $tmpdir/ping.sh

run_vlab $exampledir/two-hwsim-nodes.yaml /tmp/.host/$tmpdir/ping.sh
