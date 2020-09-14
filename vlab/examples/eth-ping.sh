#!/bin/bash -e
#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#

exampledir=$(realpath $(dirname $0))
. $exampledir/common.lib

cat > $tmpdir/ping.sh << EOF
#!/bin/bash

start=\$(date +%s)
ping -c 1000 10.0.0.2
end=\$(date +%s)

echo "************** pings took \$((\$end - \$start)) seconds (in VM)"
EOF

chmod +x $tmpdir/ping.sh

run_vlab $exampledir/two-eth-nodes.yaml /tmp/.host/$tmpdir/ping.sh
