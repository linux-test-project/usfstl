#!/usr/bin/env python3
#
# Copyright (C) 2020-2021 Intel Corporation
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Note: please keep this file "mypy --strict" and "pylint" clean.
#
"""
Virtual lab script.
"""
from __future__ import annotations

import os
import sys
import time
import glob
import datetime
import signal
import argparse
import subprocess
import importlib
import tempfile
from typing import List, Union, Dict, Any, Optional, Type, BinaryIO, TYPE_CHECKING
import itertools
import yaml
import attr


class PluginNode:
    """
    Plugin information for a node
    """
    def __init__(self, plugin: Plugin) -> None:
        self.plugin = plugin
        self._started = False

    @property
    def wmediumd_vhost_connections(self) -> int:
        """
        Return the number of connections to be made to wmediumd's vhost-user socket.
        """
        return 0

    @property
    def wmediumd_api_connections(self) -> int:
        # pylint: disable=unused-argument
        """
        Return the number of connections to be made to wmediumd's API socket.
        """
        return 0

    @property
    def time_socket_connections(self) -> int:
        """
        Return the number of time socket connections to wait for.
        """
        return 0

    def linux_cmdline(self, runtime: VlabRuntimeData) -> List[str]:
        # pylint: disable=unused-argument
        """
        Extra arguments to add to the Linux command-line
        """
        assert self._started
        return []

    def start(self, node: Node, vlabinst: Vlab, node_logdir: str) -> None:
        # pylint: disable=unused-argument
        """
        Start anything needed for the vlab run, you can access
        the Vlab instance and e.g. append to its process lists
        using start_process().
        """
        self._started = True

    def postrun(self, node: Node, vlabinst: Vlab, node_logdir: str) -> None:
        # pylint: disable=unused-argument
        """
        Do any necessary work after the vlab run concluded and all nodes
        have been shut down again.
        """

class Plugin:
    """
    Base vlab plugin
    """
    def __init__(self, vlabdir: str, args: VlabArguments) -> None:
        """
        Instantiate the plugin.
        """
        self.vlabdir = vlabdir
        self.args = args

    def earlystart(self, runtime: VlabRuntimeData) -> str:
        # pylint: disable=unused-argument
        """
        Return a shell script fragment to run in each node early,
        just after mounting filesystems.
        """
        return ""

    def nodestart(self, runtime: VlabRuntimeData) -> str:
        # pylint: disable=unused-argument
        """
        Return a shell script fragment to run at each node startup
        (except for the controller).
        """
        return ""

    def ctrlstart(self, runtime: VlabRuntimeData) -> str:
        # pylint: disable=unused-argument
        """
        Return a shell script fragment to run on controller (first machine)
        startup.
        """
        return ""

    def nodestop(self, runtime: VlabRuntimeData) -> str:
        # pylint: disable=unused-argument
        """
        Return a shell script fragment to run on each node at node shutdown
        (except for the controller).
        """
        return ""

    def ctrlstop(self, runtime: VlabRuntimeData) -> str:
        # pylint: disable=unused-argument
        """
        Return a shell script fragment to run on the controller at shutdown.
        """
        return ""

    def files(self, runtime: VlabRuntimeData) -> Dict[str, Union[BinaryIO, bytes]]:
        # pylint: disable=unused-argument
        """
        Return a dictionary mapping file names (inside the nodes) to their
        contents, or to a file(-like) object.
        """
        return {}

    def parse_node(self, node: Node, nodecfg: Any) -> Union[PluginNode, None]:
        # pylint: disable=unused-argument
        """
        Check for any plugin-specific YAML keys and return an
        instance of PluginNode if appropriate, or None.
        """
        return None

    def validate(self, nodes: List[PluginNode]) -> None:
        """
        Validate that the run can proceed, e.g. checking if the needed
        data or binaries are present. Raise Failure() if not.
        """

class Paths:
    # pylint: disable=too-few-public-methods
    """
    Paths that vlab uses.
    """
    vlab = os.path.dirname(__file__)
    plugins = os.path.join(vlab, 'plugins')
    linux: str = os.path.join(vlab, 'linux')
    controller: str = os.path.join(vlab, '../control/controller')
    wmediumd: str = os.path.join(vlab, 'wmediumd/wmediumd/wmediumd')

NEWLINE = '\n'


@attr.s
class Node:
    # pylint: disable=too-many-instance-attributes
    """
    Per-node configuration data
    """
    addr: str = attr.ib(None)
    mem: int = attr.ib(128)
    _name: Union[None, str] = attr.ib(None)
    run: Union[None, str] = attr.ib(None)
    plugins: Dict[Type[Plugin], Optional[PluginNode]] = attr.ib(None)
    rootfs: Union[None, str] = attr.ib(None)
    baseid: int = attr.ib(0)
    logdir: str = attr.ib(None)

    def set_name(self, value: str) -> None:
        """
        set the name of the Node
        """
        self._name = value

    def get_name(self) -> str:
        """
        return the name of the Node
        """
        return self._name or f'vnode_{self.addr}'

    def getid(self, idtype: int) -> int:
        """
        return the node ID
        """
        return idtype << 60 | (self.baseid << 40)

    name = property(get_name, fset=set_name)


@attr.s
class Config:
    # pylint: disable=too-few-public-methods, too-many-instance-attributes
    """
    Global configuration data
    """
    wmediumd_conf: Union[None, str] = attr.ib(None)
    wmediumd_per: Union[None, str] = attr.ib(None)
    net_delay: Union[None, float] = attr.ib(None)
    nodes: List[Node] = attr.ib([])
    start_time: int = 0
    no_shm: bool = False

class Failure(Exception):
    """
    Represents a failure in vLab
    """
    retcode = 2

class TimeoutFailure(Failure):
    """
    Represents a Timeout failure in vlab
    """
    retcode = 3

def load_plugins(path: str) -> List[Type[Plugin]]:
    """
    Load plugins in the given directory.
    """
    ret: List[Type[Plugin]] = []
    sys.path.append(path)
    for filename in sorted(glob.glob(os.path.join(path, '*.py'))):
        modname = os.path.basename(filename)[:-3]
        clsname = modname.capitalize() + 'Plugin'
        mod = importlib.import_module(modname)
        ret.append(getattr(mod, clsname))
    return ret

def fail(msg: str) -> None:
    """
    Fail the vLab setup.
    """
    raise Failure(msg)


def addr(idx: int) -> str:
    """
    Return an IP address for a given node index
    """
    return f'10.0.0.{idx}'


if TYPE_CHECKING:
    # pylint: disable=invalid-name,unsubscriptable-object
    _VlabNamedProcessBase = subprocess.Popen[bytes]
else:
    _VlabNamedProcessBase = subprocess.Popen

class VlabNamedProcess(_VlabNamedProcessBase):
    # pylint: disable=too-few-public-methods
    """
    Vlab process class to have an extra vlab name for debug
    """
    def __init__(self, *args: Any, vlab_name: Optional[str] = None, **kw: Any):
        # work around some stupid mypy version dependent issues
        _args = [self] + list(args)
        _VlabNamedProcessBase.__init__(*_args, **kw)
        self.vlab_name = vlab_name


def start_process(args: List[str], outfile: Union[None, str] = None,
                  interactive: bool = False, cwd: Union[None, str] = None,
                  vlab_name: Optional[str] = None) -> Any:
    """
    Start a single process and returns its Popen object.
    """
    s_out = None
    s_err = None

    if outfile is not None:
        # pylint: disable=consider-using-with
        outfd = open(outfile, 'w', encoding='utf-8')
        s_out = outfd
        s_err = subprocess.STDOUT

    if interactive:
        assert outfile is None
        s_in = 0
    else:
        s_in = subprocess.DEVNULL

    return VlabNamedProcess(args, start_new_session=True, cwd=cwd,
                            stdout=s_out, stdin=s_in, stderr=s_err,
                            vlab_name=vlab_name)


def wait_for_socket(which: str, sockname: Optional[str], timeout: int = 2) -> None:
    """
    Unless it is None, wait for the socket to appear, with a timeout
    (which raises a Failure).
    """
    if sockname is None:
        return
    start = time.time()
    while not os.path.exists(sockname):
        time.sleep(0.1)
        if time.time() - start > timeout:
            raise Failure(f"Socket {sockname} for {which} didn't appear!")


def _check_status(statusfile: str) -> None:
    if not os.path.exists(statusfile):
        raise Failure("status file wasn't created - test crashed?")
    with open(statusfile, 'rb') as stat_f:
        status = stat_f.read()
    try:
        val = int(status.decode('ascii').strip())
        if val != 0:
            raise Failure(f"test script failed with status {val}")
    except (ValueError, UnicodeDecodeError) as except_status:
        raise Failure(f"status '{status!r}' isn't a valid integer") from except_status


class VlabArguments:
    # pylint: disable=too-many-instance-attributes, too-few-public-methods
    """
    Class to hold vlab arguments, see the parse() classmethod to get them
    from the command line, but you can fill them programatically as well.
    """
    interactive: bool = False
    capture_all: bool = False
    wallclock: bool = False
    timeout: Union[None, int] = 120
    nodes: str = 'nodes.yaml'
    logpath: Union[str, None] = None
    tmpdir: Union[str, None] = None
    dbg: bool = False
    command: Union[List[str], None] = None
    plugins: List[Plugin] = []
    config: Config
    sigquit_on_timeout: bool = False
    no_shm: bool = False

    def __init__(self) -> None:
        self.machineid = 1

    def create_node(self, cfgdir: str, nodecfg: Dict[str, Any]) -> Node:
        """
        Create a node from a configuration dictionary
        """
        ret = Node()
        if not nodecfg:
            return ret
        if 'addr' in nodecfg:
            ret.addr = nodecfg['addr']
        if 'mem' in nodecfg:
            ret.mem = nodecfg['mem']
        if 'name' in nodecfg:
            ret.name = nodecfg['name']
        if 'rootfs' in nodecfg:
            ret.rootfs = os.path.join(cfgdir, nodecfg['rootfs'])
        ret.baseid = self.machineid
        self.machineid += 1
        ret.plugins = {}
        for plugin in self.plugins:
            ret.plugins[type(plugin)] = plugin.parse_node(ret, nodecfg)
        return ret

    def parse_config(self) -> None:
        """
        Parse the nodes configuration file (YAML) and create
        our config attribute.
        """
        filename = self.nodes
        config = Config()
        cfgdir = os.path.dirname(filename)
        with open(filename, encoding='utf-8') as yaml_f:
            cfg = yaml.safe_load(yaml_f)
        nodes = [self.create_node(cfgdir, nodecfg) for nodecfg in cfg['nodes']]
        addrs = []
        for node in nodes:
            addrs.append(node.addr)
        idx = 1
        for node in nodes:
            if not node.addr:
                while addr(idx) in addrs:
                    idx += 1
                node.addr = addr(idx)
                addrs.append(node.addr)

        if 'wmediumd' in cfg:
            wmd = cfg['wmediumd']

            wm_cfg = wmd.get('config', None)
            if wm_cfg is not None:
                assert isinstance(wm_cfg, str)
                config.wmediumd_conf = os.path.join(cfgdir, wm_cfg)

            wm_per = wmd.get('per', None)
            if wm_per is not None:
                assert isinstance(wm_per, str)
                config.wmediumd_per = os.path.join(cfgdir, wm_per)

        if 'net' in cfg:
            net = cfg['net']

            net_delay = net.get('delay', None)
            if net_delay is not None:
                assert isinstance(net_delay, (int, float))
                config.net_delay = float(net_delay)

        if 'controller' in cfg:
            config.start_time = cfg['controller'].get('start-time', 0)
            config.no_shm = 'no-shm' in cfg['controller']

        config.nodes = nodes
        self.config = config

    @classmethod
    def parse(cls, args: Union[List[str], None] = None,
              plugins: Union[List[Type[Plugin]], None] = None) -> VlabArguments:
        """
        Parse the given arguments, or sys.argv if None.
        """
        plugins = plugins or []
        parser = argparse.ArgumentParser(sys.argv[0], description='vlab runner')
        parser.add_argument('--interactive', action='store_const', const=True,
                            default=cls.interactive,
                            help="start in interactive mode, with shell on the first VM")
        parser.add_argument('--capture-all', action='store_const', const=True,
                            default=cls.capture_all,
                            help="capture output from all VMs including the first")
        parser.add_argument('--wallclock', action='store_const', const=True,
                            default=cls.wallclock,
                            help="run in wallclock mode, without time simulation")
        parser.add_argument('--timeout', type=int, default=cls.timeout,
                            help="test timeout [seconds] (0 for inf, default 120)")
        parser.add_argument(type=str, default=cls.nodes, dest='nodes',
                            help=f"nodes configuration file, default {cls.nodes}")
        parser.add_argument("--logpath", default=cls.logpath,
                            help=("path to the test logs. If given, write to 'logs/<timestamp>/' " +
                                  "and create/update the 'logs/current' symlink; " +
                                  " otherwise use as is."))
        parser.add_argument("--tmpdir", default=cls.tmpdir,
                            help=("path for temporary files. If given, use this directory for " +
                                  "temporary data. Otherwise create a new directory."))
        parser.add_argument('--dbg', action='store_const', const=True,
                            default=cls.dbg,
                            help="stop and allow gdb (disables timeout)")
        parser.add_argument('--sigquit-on-timeout', action='store_const', const=True,
                            default=cls.sigquit_on_timeout,
                            help="Send SIGQUIT on timeout to get core dumps")
        parser.add_argument(type=str, dest='command', nargs='*', metavar='cmd/arg',
                            help="command (with arguments) to run inside the vLab")
        parser.add_argument('--no-shm', action='store_const', const=True,
                            help="Disable shared memory in controller")
        data = parser.parse_args(args)

        new = VlabArguments()
        new.interactive = data.interactive
        new.capture_all = data.capture_all
        new.wallclock = data.wallclock
        new.timeout = data.timeout
        new.logpath = data.logpath
        new.tmpdir = data.tmpdir
        new.dbg = data.dbg
        new.command = data.command
        new.nodes = data.nodes
        new.sigquit_on_timeout = data.sigquit_on_timeout
        new.no_shm = data.no_shm

        if new.interactive:
            if new.command:
                raise Failure("--interactive can only be given without a command")
            if new.capture_all:
                raise Failure("--capture-all is incompatible with --interactive")
            welcome = '*********** WELCOME ***********'
            exitmsg = "Type 'exit' or press Ctrl-D to exit and shut down!"
            echomsg = f'{welcome}\n{exitmsg}'
            new.command = [f'echo "{echomsg}" ; bash -i']
            new.wallclock = True

        if new.wallclock and new.dbg:
            raise Failure("--dbg cannot be used with --wallclock or --interactive")

        if new.timeout == 0 or new.dbg or new.interactive:
            new.timeout = None # infinite

        new.plugins = [cls(Paths.vlab, new) for cls in plugins]
        new.parse_config()

        return new


@attr.s
class VlabRuntimeData:
    # pylint: disable=too-few-public-methods, too-many-instance-attributes
    """
    Runtime data (paths etc.)
    """
    tmpdir: str = attr.ib("")
    logdir: str = attr.ib("")
    startup: str = attr.ib("")
    clock: Union[None, str] = attr.ib(None)
    net: Union[None, str] = attr.ib(None)
    wmediumd_vu_sock: Union[None, str] = attr.ib(None)
    wmediumd_us_sock: Union[None, str] = attr.ib(None)


class Vlab:
    """
    virtual lab runner
    """
    processes: List[Any]
    killprocesses: List[Any]
    runtime: VlabRuntimeData
    extraroots: List[str]

    def __init__(self, args: VlabArguments, extraroots: Optional[List[str]] = None) -> None:
        """
        Init a virtual lab instance.
        """
        self.args = args
        self.processes: List[Any] = []
        self.killprocesses: List[Any] = []
        self.extraroots = extraroots or []

        # validate that we can use this config
        if not os.path.exists(os.path.join(Paths.linux, 'linux')):
            fail("Linux binary isn't built - run make")
        if not os.path.exists(os.path.join(Paths.controller)):
            fail("Time controller isn't built - run make")

        for plugin in args.plugins:
            instances: List[PluginNode] = []
            for node in args.config.nodes:
                pnode = node.plugins[type(plugin)]
                if pnode is not None:
                    instances.append(pnode)
            plugin.validate(instances)

    def start_node(self, node: Node, logfile: bool = True,
                   interactive: bool = False,
                   statusfile: Union[None, str] = None) -> None:
        """
        Start a UML node in the virtual lab.
        """
        os.mkdir(node.logdir)

        # start plugins first - part of the API
        for pnode in node.plugins.values():
            if pnode is None:
                continue
            pnode.start(node, self, node.logdir)

        vmroots = [f'{Paths.vlab}/vm-root']
        vmroots.extend(self.extraroots)
        args = [f'{Paths.linux}/linux', f'mem={node.mem}M',
                f'init={self.runtime.startup}',
                'root=none', 'hostfs=/', 'rootfstype=hostfs', 'rootflags=/',
                f'run=/tmp/.host{node.run}',
                f'virtio_uml.device={self.runtime.net}:1',
                f'addr={node.addr}',
                f'tmpdir={self.runtime.tmpdir}',
                f'hostname={node.name}',
                f'vmroots={":".join(vmroots)}',
                f'vlab={Paths.vlab}']
        if node.rootfs:
            args.append(f'customrootfs={node.rootfs}')

        if self.runtime.clock is not None:
            args.append(f'time-travel=ext:0x{node.getid(1):x}:{self.runtime.clock}')
        if statusfile:
            args.append(f'status=/tmp/.host{statusfile}')
        for pnode in node.plugins.values():
            if pnode is None:
                continue
            args.extend(pnode.linux_cmdline(self.runtime))

        outfile = os.path.join(node.logdir, 'dmesg') if logfile else None
        self.processes.append(start_process(args, outfile=outfile,
                                            interactive=interactive,
                                            vlab_name=node.name))

    def get_log_dir(self) -> str:
        """
        Return the log directory by current date/time
        """
        if self.args.logpath is not None:
            assert isinstance(self.args.logpath, str)
            return self.args.logpath

        log_base_path = os.path.join(os.getcwd(), "logs")

        now = datetime.datetime.now()
        date_path = now.strftime("%Y-%m-%d_%H_%M_%S_%f")
        log_date_path = os.path.join(log_base_path, date_path)
        os.makedirs(log_date_path)

        current_log_path = os.path.join(log_base_path, "current")
        try:
            os.remove(current_log_path)
        except OSError:
            pass
        os.symlink(log_date_path, current_log_path)
        return log_date_path

    def _run(self, tmpdir: str) -> None:
        # pylint: disable=too-many-locals, too-many-branches, too-many-statements
        """
        Run the virtual lab with the given tmpdir, internal.
        """
        args = self.args
        # check that parsing worked (and make type checking happy)
        assert args is not None
        assert isinstance(args.command, list)

        self.runtime = VlabRuntimeData()

        self.processes = []
        self.killprocesses = []

        cfg = self.args.config
        nodes = cfg.nodes

        self.runtime.logdir = self.get_log_dir()
        for node in nodes:
            node.logdir = os.path.join(self.runtime.logdir, node.name)

        self.runtime.tmpdir = tmpdir

        self.runtime.startup = os.path.join(Paths.vlab, 'vm-root/tmp/startup.sh')

        path = os.path.join(tmpdir, 'path')
        with open(path, 'w', encoding='utf-8') as path_f:
            path_f.write(os.environ['PATH'])

        with open(os.path.join(tmpdir, 'hosts'), 'w', encoding='utf-8') as hostsfile:
            hostsfile.write('127.0.0.1	localhost\n')
            for node in nodes:
                hostsfile.write(f'{node.addr}	{node.name}\n')

        pluginfiles = os.path.join(tmpdir, 'pluginfiles')
        os.mkdir(pluginfiles)
        for plugin in self.args.plugins:
            for fname, fcontent in plugin.files(self.runtime).items():
                assert fname.startswith('/')
                fname = fname[1:]
                os.makedirs(os.path.join(pluginfiles, os.path.dirname(fname)),
                            exist_ok=True)
                if isinstance(fcontent, bytes):
                    with open(os.path.join(pluginfiles, fname), 'wb') as fcont_f:
                        fcont_f.write(fcontent)
                else:
                    fcontent.seek(0)
                    with open(os.path.join(pluginfiles, fname), 'wb') as fcont_f:
                        fcont_f.write(fcontent.read())

        earlystart = os.path.join(tmpdir, 'early.sh')
        with open(earlystart, 'w', encoding='utf-8') as earlystart_f:
            earlystart_f.write(f'''#!/bin/sh

{NEWLINE.join(plugin.earlystart(self.runtime) for plugin in self.args.plugins)}
''')

        nodestop = os.path.join(tmpdir, 'stop.sh')
        with open(nodestop, 'w', encoding='utf-8') as nodestop_f:
            nodestop_f.write(f'''#!/bin/sh

{NEWLINE.join(plugin.nodestop(self.runtime) for plugin in self.args.plugins)}
''')

        nodestart = os.path.join(tmpdir, 'node.sh')
        with open(nodestart, 'w', encoding='utf-8') as nodestart_f:
            nodestart_f.write(f'''#!/bin/sh

chmod +x /tmp/.host/{nodestop}

{NEWLINE.join(plugin.nodestart(self.runtime) for plugin in self.args.plugins)}

while true ; do sleep 600 ; done
''')

        ctrlstart = os.path.join(tmpdir, 'ctrl.sh')
        with open(ctrlstart, 'w', encoding='utf-8') as ctrlstart_f:
            ctrlstart_f.write(fr'''#!/bin/sh

{NEWLINE.join(plugin.ctrlstart(self.runtime) for plugin in self.args.plugins)}

cd /tmp/.host/{os.getcwd()}

{' '.join(args.command)}
code=$?

status=$(sed 's/.*status=\([^ ]*\)\( .*\|$\)/\1/;t;d' /proc/cmdline)
echo $code > $status

{NEWLINE.join([f"ssh -Fnone -oStrictHostKeyChecking=no {node.addr} /tmp/.host/{nodestop}" for node in nodes[1:]])}

{NEWLINE.join([f"echo power off {node.addr} ; ssh -Fnone -oStrictHostKeyChecking=no {node.addr} poweroff -f &" for node in nodes[1:]])}
{"echo waiting 5 seconds for shutdown; sleep 5" if len(nodes) > 1 else ""}

{NEWLINE.join(plugin.ctrlstop(self.runtime) for plugin in self.args.plugins)}

poweroff -f
''')

        self.runtime.net = os.path.join(tmpdir, 'net')
        statusfile: Union[str, None] = None
        if not args.interactive:
            statusfile = os.path.join(tmpdir, 'status')

        wmediumd_vhost_conns = 0
        wmediumd_api_conns = 0
        controller_conns = 0
        for node in nodes:
            for pnode in node.plugins.values():
                if pnode is None:
                    continue
                wmediumd_vhost_conns += pnode.wmediumd_vhost_connections
                wmediumd_api_conns += pnode.wmediumd_api_connections
                controller_conns += pnode.time_socket_connections

        ctrl_args = [Paths.controller, f'--net={self.runtime.net}',
                     f'--time-at-start={cfg.start_time}']
        if args.dbg:
            ctrl_args += ['--debug=3']
        if args.no_shm or cfg.no_shm:
            ctrl_args += ['--no-shm']

        if cfg.net_delay is not None:
            ctrl_args.append(f'--net-delay={cfg.net_delay}')

        if args.wallclock:
            ctrl_args.append('--wallclock-network')
        else:
            self.runtime.clock = os.path.join(tmpdir, 'clock')
            clients = len(nodes)
            if wmediumd_vhost_conns + wmediumd_api_conns > 1:
                clients += 1
            clients += controller_conns
            ctrl_args.extend([f'--time={self.runtime.clock}',
                              f'--clients={clients}'])

        control_process = start_process(ctrl_args,
                                        outfile=os.path.join(self.runtime.logdir,
                                                             'controller.log'))
        self.killprocesses.append(control_process)

        timedout = False

        try:
            wait_for_socket("clock controller", self.runtime.clock)
            wait_for_socket("ethernet", self.runtime.net)

            if args.dbg:
                control_process.send_signal(signal.SIGSTOP)

            if wmediumd_vhost_conns + wmediumd_api_conns > 1:
                wmediumd_args = ['-p', os.path.join(self.runtime.logdir, 'wmediumd.pcapng')]

                # NOTE: wmediumd currently requires this argument to
                #       not try to connect to netlink ... it should
                #       probably have a way to just disable netlink,
                #       or automatically disable it if '-a' is given
                # if wmediumd_vhost_conns or True:
                self.runtime.wmediumd_vu_sock = os.path.join(tmpdir, 'wmediumd-vu')
                wmediumd_args.extend(['-u', self.runtime.wmediumd_vu_sock])

                if wmediumd_api_conns:
                    self.runtime.wmediumd_us_sock = os.path.join(tmpdir, 'wmediumd-us')
                    wmediumd_args.extend(['-a', self.runtime.wmediumd_us_sock])

                if self.runtime.clock:
                    wmediumd_args.extend(['-t', self.runtime.clock])

                if not cfg.wmediumd_conf:
                    raise Failure("If wmediumd is used, it must be configured!")

                wmediumd_args.extend(['-c', cfg.wmediumd_conf])

                if cfg.wmediumd_per:
                    wmediumd_args.extend(['-x', cfg.wmediumd_per])

                wmediumd_args.extend(['-l', '7'])

                wmediumd_proc = start_process([Paths.wmediumd] + wmediumd_args,
                                              outfile=os.path.join(self.runtime.logdir,
                                                                   'wmediumd.log'))
                self.killprocesses.append(wmediumd_proc)

                if wmediumd_vhost_conns:
                    wait_for_socket("wmediumd vhost-user", self.runtime.wmediumd_vu_sock)
                if wmediumd_api_conns:
                    wait_for_socket("wmediumd unix domain", self.runtime.wmediumd_us_sock)

            nodes[0].run = ctrlstart
            self.start_node(nodes[0], args.capture_all, args.interactive, statusfile)
            for node in nodes[1:]:
                node.run = nodestart
                self.start_node(node)

            if args.dbg:
                gdb_log: List[str] = []
                for process in itertools.chain(self.killprocesses, self.processes):
                    extra = ''
                    if isinstance(process, VlabNamedProcess) and process.vlab_name:
                        extra = f' {process.vlab_name}'
                    basename = os.path.basename(process.args[0])
                    gdb_log += [f'\npid: {process.pid} ({basename}{extra})']
                    gdb: List[str] = ['gdb']
                    if os.path.basename(process.args[0]) == 'linux':
                        linux_gdb = os.path.join(os.path.dirname(__file__), 'linux.gdb')
                        gdb += [f'-ex "source {linux_gdb}"']
                    gdb.extend(['--pid', str(process.pid)])
                    gdb_log += [f'\t{" ".join(gdb)}']
                    gdb_log += [f'\tcmd: {" ".join(process.args)}']

                gdb_log_str = "\n".join(gdb_log)
                print(gdb_log_str)
                with open(os.path.join(self.runtime.logdir, 'gdb_log.txt'),
                          'w', encoding='utf-8') as file:
                    file.write(gdb_log_str)
                input('==== press Enter to continue ====\n')
                control_process.send_signal(signal.SIGCONT)

            first = True
            for process in self.processes:
                try:
                    timeout: Union[None, int] = 1
                    if first:
                        timeout = args.timeout
                    process.wait(timeout=timeout)
                    first = False
                except subprocess.TimeoutExpired:
                    if first:
                        timedout = True
                        raise TimeoutFailure(
                            f"Timeout of {args.timeout} seconds expired!"
                        ) from None
                    name = str(process.args[0])
                    if args.interactive:
                        # just in case ... for Ctrl-C
                        os.system('reset -I >/dev/null 2>&1 || true')
                        print(f"Waiting for process {name}")
                        print("Press Ctrl-C to cancel")
                        process.wait()
                    else:
                        raise Failure("Processes didn't exit cleanly") from None
        finally:
            for process in itertools.chain(self.killprocesses, self.processes):
                # Check If the process is still alive to get any signals
                if process.poll() is None:
                    pgrp = os.getpgid(process.pid)
                    sig_send = signal.SIGKILL
                    if timedout and args.sigquit_on_timeout:
                        sig_send = signal.SIGQUIT
                    os.killpg(pgrp, sig_send)
            os.system('reset -I >/dev/null 2>&1 || true')

            for node in nodes:
                for pnode in node.plugins.values():
                    if pnode is None:
                        continue
                    pnode.postrun(node, self, node.logdir)

        if not args.interactive:
            assert statusfile is not None
            _check_status(statusfile)

    def run(self) -> None:
        """
        Run the virtual lab.
        """
        if self.args.tmpdir is not None:
            self._run(self.args.tmpdir)
        else:
            with tempfile.TemporaryDirectory() as tmpdir:
                self._run(tmpdir)

if __name__ == '__main__':
    try:
        # load all plugins by default
        Vlab(VlabArguments.parse(plugins=load_plugins(Paths.plugins))).run()
    except Failure as failure:
        print(f"{failure}")
        sys.exit(failure.retcode)
