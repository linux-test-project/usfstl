#!/usr/bin/env python3
#
# Copyright (C) 2020 Intel Corporation
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
from typing import List, Union, Dict, Any, Optional, Type, BinaryIO
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
        # pylint: disable=unused-argument, no-self-use
        """
        Return the number of connections to be made to wmediumd's API socket.
        """
        return 0

    @property
    def time_socket_connections(self) -> int:
        # pylint: disable=no-self-use
        """
        Return the number of time socket connections to wait for.
        """
        return 0

    def linux_cmdline(self, runtime: VlabRuntimeData) -> List[str]:
        # pylint: disable=unused-argument, no-self-use
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
        # pylint: disable=unused-argument, no-self-use
        """
        Return a shell script fragment to run in each node early,
        just after mounting filesystems.
        """
        return ""

    def nodestart(self, runtime: VlabRuntimeData) -> str:
        # pylint: disable=unused-argument, no-self-use
        """
        Return a shell script fragment to run at each node startup
        (except for the controller).
        """
        return ""

    def ctrlstart(self, runtime: VlabRuntimeData) -> str:
        # pylint: disable=unused-argument, no-self-use
        """
        Return a shell script fragment to run on controller (first machine)
        startup.
        """
        return ""

    def files(self, runtime: VlabRuntimeData) -> Dict[str, Union[BinaryIO, bytes]]:
        # pylint: disable=unused-argument, no-self-use
        """
        Return a dictionary mapping file names (inside the nodes) to their
        contents, or to a file(-like) object.
        """
        return {}

    def parse_node(self, node: Node, nodecfg: Any) -> Union[PluginNode, None]:
        # pylint: disable=unused-argument, no-self-use
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

class Failure(Exception):
    """
    Represents a failure in vLab
    """

def load_plugins(path: str) -> List[Type[Plugin]]:
    """
    Load plugins in the given directory.
    """
    ret: List[Type[Plugin]] = []
    sys.path.append(path)
    for filename in glob.glob(os.path.join(path, '*.py')):
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
    return '10.0.0.%d' % idx


def start_process(args: List[str], outfile: Union[None, str] = None,
                  interactive: bool = False, cwd: Union[None, str] = None) -> Any:
    """
    Start a single process and returns its Popen object.
    """
    s_out = None
    s_err = None

    if outfile is not None:
        outfd = open(outfile, 'w')
        s_out = outfd
        s_err = subprocess.STDOUT

    if interactive:
        assert outfile is None
        s_in = 0
    else:
        s_in = subprocess.DEVNULL

    return subprocess.Popen(args, start_new_session=True, cwd=cwd,
                            stdout=s_out, stdin=s_in, stderr=s_err)


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
    status = open(statusfile, 'rb').read()
    try:
        val = int(status.decode('ascii').strip())
        if val != 0:
            raise Failure(f"test script failed with status {val}")
    except (ValueError, UnicodeDecodeError):
        raise Failure(f"status '{status!r}' isn't a valid integer")


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
    dbg: bool = False
    command: Union[List[str], None] = None
    plugins: List[Plugin] = []
    config: Config

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
        cfg = yaml.safe_load(open(filename))
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
                            help=f"test timeout [seconds] (0 for inf, default 120)")
        parser.add_argument(type=str, default=cls.nodes, dest='nodes',
                            help=f"nodes configuration file, default {cls.nodes}")
        parser.add_argument("--logpath", default=cls.logpath,
                            help=("path to the test logs. If given, write to 'logs/<timestamp>/' " +
                                  "and create/update the 'logs/current' symlink; " +
                                  " otherwise use as is."))
        parser.add_argument('--dbg', action='store_const', const=True,
                            default=cls.dbg,
                            help="stop and allow gdb (disables timeout)")
        parser.add_argument(type=str, dest='command', nargs='*', metavar='cmd/arg',
                            help=f"command (with arguments) to run inside the vLab")
        data = parser.parse_args(args)

        new = VlabArguments()
        new.interactive = data.interactive
        new.capture_all = data.capture_all
        new.wallclock = data.wallclock
        new.timeout = data.timeout
        new.logpath = data.logpath
        new.dbg = data.dbg
        new.command = data.command
        new.nodes = data.nodes

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
            fail(f"Linux binary isn't built - run make")
        if not os.path.exists(os.path.join(Paths.controller)):
            fail(f"Time controller isn't built - run make")

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
        node_logdir = os.path.join(self.runtime.logdir, node.name)
        os.mkdir(node_logdir)

        # start plugins first - part of the API
        for pnode in node.plugins.values():
            if pnode is None:
                continue
            pnode.start(node, self, node_logdir)

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

        outfile = os.path.join(node_logdir,
                               f'dmesg') if logfile else None
        self.processes.append(start_process(args, outfile=outfile,
                                            interactive=interactive))

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
        self.runtime.tmpdir = tmpdir

        self.runtime.startup = os.path.join(Paths.vlab, 'vm-root/tmp/startup.sh')

        path = os.path.join(tmpdir, 'path')
        open(path, 'w').write(os.environ['PATH'])

        with open(os.path.join(tmpdir, 'hosts'), 'w') as hostsfile:
            hostsfile.write('127.0.0.1	localhost\n')
            for node in nodes:
                hostsfile.write(f'{node.addr}	{node.name}\n')

        pluginfiles = os.path.join(tmpdir, 'pluginfiles')
        os.mkdir(pluginfiles)
        for plugin in self.args.plugins:
            for fname, fcontent in plugin.files(self.runtime).items():
                assert fname.startswith('/')
                fname = fname[1:]
                os.makedirs(os.path.join(pluginfiles, os.path.dirname(fname)))
                if isinstance(fcontent, bytes):
                    open(os.path.join(pluginfiles, fname), 'wb').write(fcontent)
                else:
                    fcontent.seek(0)
                    open(os.path.join(pluginfiles, fname), 'wb').write(fcontent.read())

        earlystart = os.path.join(tmpdir, 'early.sh')
        open(earlystart, 'w').write(f'''#!/bin/sh

{NEWLINE.join(plugin.earlystart(self.runtime) for plugin in self.args.plugins)}
''')

        nodestart = os.path.join(tmpdir, 'node.sh')
        open(nodestart, 'w').write(f'''#!/bin/sh

{NEWLINE.join(plugin.nodestart(self.runtime) for plugin in self.args.plugins)}

while true ; do sleep 600 ; done
''')

        ctrlstart = os.path.join(tmpdir, 'ctrl.sh')
        open(ctrlstart, 'w').write(fr'''#!/bin/sh

{NEWLINE.join(plugin.ctrlstart(self.runtime) for plugin in self.args.plugins)}

cd /tmp/.host/{os.getcwd()}

{' '.join(args.command)}
code=$?

status=$(sed 's/.*status=\([^ ]*\)\( .*\|$\)/\1/;t;d' /proc/cmdline)
echo $code > $status

{NEWLINE.join([f"echo power off {node.addr} ; ssh -Fnone -oStrictHostKeyChecking=no {node.addr} poweroff -f &" for node in nodes[1:]])}
{"echo waiting 5 seconds for shutdown; sleep 5" if len(nodes) > 1 else ""}

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

        ctrl_args = [Paths.controller, f'--net={self.runtime.net}']
        if args.dbg:
            ctrl_args += [f'--debug=3', f'--flush']

        if cfg.net_delay is not None:
            ctrl_args.append('--net-delay=%f' % cfg.net_delay)

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
                if wmediumd_vhost_conns or True:
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
                    gdb_log += [f'\npid: {process.pid} ({os.path.basename(process.args[0])})']
                    gdb: List[str] = ['gdb']
                    if os.path.basename(process.args[0]) == 'linux':
                        linux_gdb = os.path.join(os.path.dirname(__file__), 'linux.gdb')
                        gdb += [f'-ex "source {linux_gdb}"']
                    gdb.extend(['--pid', str(process.pid)])
                    gdb_log += [f'\t{" ".join(gdb)}']
                    gdb_log += [f'\tcmd: {" ".join(process.args)}']

                gdb_log_str = "\n".join(gdb_log)
                print(gdb_log_str)
                with open(os.path.join(self.runtime.logdir, 'gdb_log.txt'), 'w') as file:
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
                        raise Failure(f"Timeout of {args.timeout} seconds expired!")
                    name = str(process.args[0])
                    if args.interactive:
                        # just in case ... for Ctrl-C
                        os.system('reset -I >/dev/null 2>&1 || true')
                        print(f"Waiting for process {name}")
                        print("Press Ctrl-C to cancel")
                        process.wait()
                    else:
                        raise Failure(f"Processes didn't exit cleanly")
        finally:
            for process in itertools.chain(self.killprocesses, self.processes):
                if process.poll() is None:
                    pgrp = os.getpgid(process.pid)
                    os.killpg(pgrp, signal.SIGKILL)
            os.system('reset -I >/dev/null 2>&1 || true')

        if not args.interactive:
            assert statusfile is not None
            _check_status(statusfile)

    def run(self) -> None:
        """
        Run the virtual lab.
        """
        with tempfile.TemporaryDirectory() as tmpdir:
            self._run(tmpdir)

if __name__ == '__main__':
    try:
        # load all plugins by default
        Vlab(VlabArguments.parse(plugins=load_plugins(Paths.plugins))).run()
    except Failure as failure:
        print(f"{failure}")
        sys.exit(2)
