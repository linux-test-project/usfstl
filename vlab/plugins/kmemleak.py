#
# Copyright (C) 2021 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
"""
kmemleak checker plugin for VLAB
"""
import glob
import virtlab

class KmemleakPlugin(virtlab.Plugin):
    """
    kmemleak plugin for VLAB
    """
    def nodestop(self, runtime: virtlab.VlabRuntimeData) -> str:
        return '''
        test -f /sys/kernel/debug/kmemleak && (
          # writing "scan" returns EPERM if kmemleak has been disabled
          echo scan > /sys/kernel/debug/kmemleak 2> /dev/null || echo "kmemleak is disabled on $HOSTNAME"
          sleep 5 # minimum age for reporting in kmemleak
          echo scan > /sys/kernel/debug/kmemleak 2> /dev/null
          cat /sys/kernel/debug/kmemleak > /tmp/kmemleak
          test -s /tmp/kmemleak && touch $TMPDIR/kmemleak-leaked-$HOSTNAME
          cat /tmp/kmemleak > /dev/console
        )
        '''

    def postrun(self, runtime: virtlab.VlabRuntimeData) -> None:
        hosts = [f[16:] for f in glob.glob('kmemleak-leaked-*', root_dir=runtime.tmpdir)]
        if hosts:
            raise virtlab.Failure(f'Kernel memory leak detected on hosts: {", ".join(hosts)}')

    ctrlstop = nodestop
