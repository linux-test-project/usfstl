#
# Copyright (C) 2021 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
"""
kmemleak checker plugin for VLAB
"""
import glob
from typing import List, Any, Optional

import virtlab

class KmemleakPluginNode(virtlab.PluginNode):
    """
    Kmemleak plugin per-node information
    """
    def __init__(self, plugin: virtlab.Plugin, cfg: Any) -> None:
        super().__init__(plugin)

        if isinstance(cfg, bool):
            cfg = {} if cfg else None

        self._cfg = cfg

    def linux_cmdline(self, runtime: virtlab.VlabRuntimeData) -> List[str]:
        # Explicitly enable/disable so that the kernel default does not matter
        if self._cfg is None:
            return [ 'kmemleak=off' ]

        ret = ['kmemleak=on']
        caches = [
            'kmemleak_object',
            'kmemleak_scan_area',
            'names_cache',
            'hostfs_inode_info',
            'ovl_inode',
        ]
        if 'caches' in self._cfg:
            caches.extend(self._cfg['caches'])
        ret.extend([f'slub_debug=-,{",".join(caches)}'])
        return ret


class KmemleakPlugin(virtlab.Plugin):
    """
    kmemleak plugin for VLAB
    """
    def parse_node(self, node: virtlab.Node, nodecfg: Any) -> Optional[KmemleakPluginNode]:
        return KmemleakPluginNode(self, nodecfg.get('kmemleak', None))

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
