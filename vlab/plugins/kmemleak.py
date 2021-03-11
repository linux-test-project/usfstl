#
# Copyright (C) 2021 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
"""
kmemleak checker plugin for VLAB
"""
import virtlab

class KmemleakPlugin(virtlab.Plugin):
    """
    kmemleak plugin for VLAB
    """
    def nodestop(self, runtime: virtlab.VlabRuntimeData) -> str:
        return '''
        test -f /sys/kernel/debug/kmemleak && (
          echo scan > /sys/kernel/debug/kmemleak
          sleep 5 # minimum age for reporting in kmemleak
          echo scan > /sys/kernel/debug/kmemleak
          cat /sys/kernel/debug/kmemleak > /dev/console
        )
        '''

    ctrlstop = nodestop