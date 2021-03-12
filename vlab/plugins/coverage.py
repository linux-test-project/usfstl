#
# Copyright (C) 2021 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
"""
coverage collection plugin for VLAB
"""
import os
import shutil
import subprocess
from typing import Any
import virtlab

class CoveragePluginNode(virtlab.PluginNode):
    """
    Coverage plugin per-node information
    """
    def postrun(self, node: virtlab.Node, vlabinst: virtlab.Vlab, node_logdir: str) -> None:
        tmpdir = f'{vlabinst.runtime.tmpdir}/cov/{node.name}/gcov'
        if not os.path.exists(tmpdir):
            return
        if set(os.listdir(tmpdir)) == set(['reset']):
            return
        machine_file = os.path.join(tmpdir, 'lcov')
        subprocess.run(['lcov', '-q', '--rc', 'lcov_branch_coverage=1',
                        '-c', '-d', tmpdir, '-o', machine_file], check=True)
        combined_file = os.path.join(vlabinst.runtime.logdir, 'data.cov')
        if os.path.exists(combined_file):
            subprocess.run(['lcov', '-q', '--rc', 'lcov_branch_coverage=1',
                            '-a', machine_file, '-a', combined_file,
                            '-o', combined_file], check=True)
        else:
            shutil.copy(machine_file, combined_file)

class CoveragePlugin(virtlab.Plugin):
    """
    coverage collection plugin for VLAB
    """
    def nodestop(self, runtime: virtlab.VlabRuntimeData) -> str:
        return '''test -d /sys/kernel/debug/gcov && (\
          covdir=/tmp/.host$TMPDIR/cov/$HOSTNAME/
          mkdir -p $covdir
          cp -d -r /sys/kernel/debug/gcov $covdir
        )
        '''

    ctrlstop = nodestop

    def parse_node(self, node: virtlab.Node, nodecfg: Any) -> CoveragePluginNode:
        return CoveragePluginNode(self)
