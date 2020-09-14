#
# Copyright (C) 2020 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
"""
Hwsim plugin module for VLAB
"""
from typing import List, Any, Union
import virtlab

class HwsimPluginNode(virtlab.PluginNode):
    """
    Hwsim plugin per-node information
    """
    @property
    def wmediumd_vhost_connections(self) -> int:
        return 1

    def linux_cmdline(self, runtime: virtlab.VlabRuntimeData) -> List[str]:
        return [f'virtio_uml.device={runtime.wmediumd_vu_sock}:29']

class HwsimPlugin(virtlab.Plugin):
    """
    Hwsim plugin for VLAB
    """
    def parse_node(self, node: virtlab.Node, nodecfg: Any) -> Union[HwsimPluginNode, None]:
        if 'hwsim' in nodecfg:
            return HwsimPluginNode(self)
        return None
