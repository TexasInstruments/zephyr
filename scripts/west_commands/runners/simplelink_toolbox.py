# Copyright (c) 2025 Conclusive Engineering Sp. z o.o.
#
# SPDX-License-Identifier: Apache-2.0

'''Runner for simplelink-wifi-toolbox.'''

import argparse
import logging
import re
import subprocess
import os
from os import name as os_name
from os import path
from pathlib import Path

from zephyr_ext_common import ZEPHYR_BASE

if os_name != "nt":
    import sys
    import termios

try:  # noqa SIM105
    from elftools.elf.elffile import ELFFile
except ImportError:
    pass

from runners.core import FileType, RunnerCaps, ZephyrBinaryRunner

_logger = logging.getLogger('runners')

DEFAULT_OPENOCD_TCL_PORT = 6333
DEFAULT_OPENOCD_TELNET_PORT = 4444
DEFAULT_OPENOCD_GDB_PORT = 3333
DEFAULT_OPENOCD_RTT_PORT = 5555
DEFAULT_OPENOCD_RESET_HALT_CMD = 'reset init'
DEFAULT_OPENOCD_TARGET_HANDLE = "_TARGETNAME"


def to_num(number):
    dev_match = re.search(r"^\d*\+dev", number)
    dev_version = dev_match is not None

    num_match = re.search(r"^\d*", number)
    num = int(num_match.group(0))

    if dev_version:
        num += 1

    return num


class SimpleLinkBinaryRunner(ZephyrBinaryRunner):
    '''Runner front-end for simplelink-wifi-toolbox.'''

    def __init__(
        self,
        cfg,
        simplelink_tool,
        pre_init=None,
        tcl_port=DEFAULT_OPENOCD_TCL_PORT,
        telnet_port=DEFAULT_OPENOCD_TELNET_PORT,
        gdb_port=DEFAULT_OPENOCD_GDB_PORT,
        tui=None,
        config=None,
        serial=None,
        image_type=None,
        gdb_client_port=DEFAULT_OPENOCD_GDB_PORT,
        gdb_init=None,
        load=False,
        target_handle=DEFAULT_OPENOCD_TARGET_HANDLE,
        rtt_port=DEFAULT_OPENOCD_RTT_PORT,
        rtt_server=None,
        no_halt=False,
        no_init=False,
        no_targets=False,
        no_flash=False,
        flash_address=None,
    ):
        super().__init__(cfg)

        if not path.exists(cfg.board_dir):
            # try to find the board support in-tree
            cfg_board_path = path.normpath(cfg.board_dir)
            boards_parent = cfg_board_path.split('boards')[0]
            boards_and_below = path.relpath(cfg_board_path, boards_parent)
            support = path.join(ZEPHYR_BASE, boards_and_below, 'support')
        else:
            support = path.join(cfg.board_dir, 'support')

        if not config:
            default = path.join(support, 'openocd.cfg')
            if path.exists(default):
                config = [default]
        self.openocd_config = config

        search_args = []
        if path.exists(support):
            search_args.append('-s')
            search_args.append(support)

        if self.openocd_config is not None:
            for i in self.openocd_config:
                if path.exists(i) and not path.samefile(path.dirname(i), support):
                    search_args.append('-s')
                    search_args.append(path.dirname(i))

        if cfg.openocd_search is not None:
            for p in cfg.openocd_search:
                search_args.extend(['-s', p])
        self.openocd_cmd = [cfg.openocd or 'openocd'] + search_args
        self.elf_name = Path(cfg.elf_file).as_posix() if cfg.elf_file else None
        self.pre_init = pre_init or []
        self.simplelink_tool = simplelink_tool
        self.vendor_file = (
            Path(cfg.build_dir) / 'zephyr' / 'flash' / 'vendor_image.sign.bin'
        ).resolve()
        self.debug_action_request_file = (
            Path(cfg.build_dir) / 'zephyr' / 'flash' / 'debug_action_request.sign.bin'
        ).resolve()
        self.tool_setting_file = (Path(cfg.board_dir) / 'config' / 'tool_settings.json').resolve()
        self.programming_report_file = (
            Path(self.cfg.build_dir) / 'simplelink_toolbox_report.txt'
        ).resolve()
        self.tcl_port = tcl_port
        self.telnet_port = telnet_port
        self.gdb_port = gdb_port
        self.gdb_client_port = gdb_client_port
        self.gdb_cmd = [cfg.gdb] if cfg.gdb else None
        self.tui_arg = ['-tui'] if tui else []
        self.halt_arg = [] if no_halt else ['-c halt']
        self.init_arg = [] if no_init else ['-c init']
        self.targets_arg = [] if no_targets else ['-c targets']
        self.serial = ['-c set _ZEPHYR_BOARD_SERIAL ' + serial] if serial else []
        self.image_type = image_type
        self.flash_address = flash_address
        self.gdb_init = gdb_init
        self.load_arg = []
        self.target_handle = target_handle
        self.rtt_port = rtt_port
        self.rtt_server = rtt_server
        self.no_flash = no_flash

    @classmethod
    def name(cls):
        return 'simplelink_toolbox'

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument(
            '--simplelink-tool',
            default='simplelink-wifi-toolbox',
            help='path to simplelink tool, default is simplelink-wifi-toolbox',
        )

        # options for debugging
        parser.add_argument(
            '--serial',
            default="",
            help='''if given, selects FTDI instance by its serial number,
                            defaults to empty''',
        )
        parser.add_argument(
            '--no-flash',
            default=False,
            action='store_true',
            help='''if given, disables flashing of the device''',
        )
        parser.add_argument(
            '--tui', default=False, action='store_true', help='if given, GDB uses -tui'
        )
        parser.add_argument(
            '--tcl-port',
            default=DEFAULT_OPENOCD_TCL_PORT,
            help='openocd TCL port, defaults to 6333',
        )
        parser.add_argument(
            '--telnet-port',
            default=DEFAULT_OPENOCD_TELNET_PORT,
            help='openocd telnet port, defaults to 4444',
        )
        parser.add_argument(
            '--gdb-port',
            default=DEFAULT_OPENOCD_GDB_PORT,
            help='openocd gdb port, defaults to 3333',
        )
        parser.add_argument(
            '--gdb-client-port',
            default=DEFAULT_OPENOCD_GDB_PORT,
            help='''openocd gdb client port if multiple ports come
                            up, defaults to 3333''',
        )
        parser.add_argument('--gdb-init', action='append', help='if given, add GDB init commands')
        parser.add_argument(
            '--no-halt', action='store_true', help='if given, no halt issued in gdb server cmd'
        )
        parser.add_argument(
            '--no-init', action='store_true', help='if given, no init issued in gdb server cmd'
        )
        parser.add_argument(
            '--no-targets', action='store_true', help='if given, no target issued in gdb server cmd'
        )
        parser.add_argument(
            '--target-handle',
            default=DEFAULT_OPENOCD_TARGET_HANDLE,
            help=f'''Internal handle used in openocd targets cfg
                            files, defaults to "{DEFAULT_OPENOCD_TARGET_HANDLE}".
                            ''',
        )
        parser.add_argument(
            '--rtt-port',
            default=DEFAULT_OPENOCD_RTT_PORT,
            help='openocd rtt port, defaults to 5555',
        )
        parser.add_argument(
            '--rtt-server',
            default=False,
            action='store_true',
            help='''start the RTT server while debugging.
                            To view the RTT log, connect to the rtt port using
                            a command like telnet.''',
        )

    @classmethod
    def do_create(cls, cfg, args):
        return SimpleLinkBinaryRunner(
            cfg,
            simplelink_tool=args.simplelink_tool,
            pre_init=None,
            no_targets=args.no_targets,
            tcl_port=args.tcl_port,
            tui=args.tui,
            serial=args.serial,
            telnet_port=args.telnet_port,
            gdb_port=args.gdb_port,
            gdb_client_port=args.gdb_client_port,
            gdb_init=args.gdb_init,
            load=args.load,
            target_handle=args.target_handle,
            rtt_port=args.rtt_port,
            rtt_server=args.rtt_server,
            no_halt=args.no_halt,
            no_init=args.no_init,
            no_flash=args.no_flash,
        )

    def do_run(self, command, **kwargs):
        self.require(self.openocd_cmd[0])
        if globals().get('ELFFile') is None:
            raise RuntimeError('elftools missing; please "pip3 install elftools"')

        self.cfg_cmd = []
        if self.openocd_config is not None:
            for i in self.openocd_config:
                self.cfg_cmd.append('-f')
                self.cfg_cmd.append(i)

        if command == 'flash':
            self.do_flash(**kwargs)
        elif command in ('attach', 'debug', 'rtt'):
            self.do_attach_debug_rtt(command, **kwargs)
        elif command == 'load':
            self.do_flash(**kwargs)
        else:
            self.do_debugserver(**kwargs)

    def do_flash(self):
        if self.vendor_file is not None and os.path.isfile(self.vendor_file):
            fname = self.vendor_file
        else:
            raise ValueError(f'Cannot flash; no vendor ({self.vendor_file}) file found. ')

        cmd = [
            str(self.simplelink_tool),
            'programmer',
            '-i',
            'XDS110',
            '-param1',
            'auto',
            'programming',
            '--tool_settings',
            str(self.tool_setting_file),
            '--report_file_name_path',
            str(self.programming_report_file),
        ]

        self.logger.info(f'Flashing file: {fname}')

        try:
            self.check_output(cmd, cwd=Path(self.cfg.build_dir).resolve())
            self.logger.info('Success')
        except subprocess.CalledProcessError as grepexc:
            self.logger.error(f"Failure {grepexc.returncode}")

    def print_gdbserver_message(self):
        if not self.thread_info_enabled:
            thread_msg = '; no thread info available'
        elif self.supports_thread_info():
            thread_msg = '; thread info enabled'
        else:
            thread_msg = '; update OpenOCD software for thread info'
        self.logger.info(f'OpenOCD GDB server running on port {self.gdb_port}{thread_msg}')

    def print_rttserver_message(self):
        self.logger.info(f'OpenOCD RTT server running on port {self.rtt_port}')

    def read_version(self):
        self.require(self.openocd_cmd[0])

        # OpenOCD prints in stderr, need redirect to get output
        out = self.check_output(
            [self.openocd_cmd[0], '--version'], stderr=subprocess.STDOUT
        ).decode()

        # Account for version info format of ADI fork of OpenOCD as well
        version_match = re.search(r"Open On-Chip Debugger.* v?(\d+.\d+.\d+)", out)
        version = version_match.group(1).split('.')

        return [to_num(i) for i in version]

    def supports_thread_info(self):
        # Zephyr rtos was introduced after 0.11.0
        (major, minor, rev) = self.read_version()
        return (major, minor, rev) > (0, 11, 0)

    def swtich_to_debug_mode(self):
        cmd_simple_link_tool = [
            str(self.simplelink_tool),
            'programmer',
            '-i',
            'XDS110',
            '-param1',
            'auto',
            'debug',
            '--debug_action_req_path',
            str(self.debug_action_request_file),
        ]

        self.logger.info(f'Enabling Debug mode')
        self.check_call(cmd_simple_link_tool)

        try:
            self.check_output(cmd_simple_link_tool, cwd=Path(self.cfg.build_dir).resolve())
            self.logger.info('Success')
        except subprocess.CalledProcessError as grepexc:
            self.logger.error(f"Failure {grepexc.returncode}")

    def _openocd_cmd(self, cmd_list):
        """Convert a list of commands to openocd -c arguments."""
        return [x for cmd in cmd_list for x in ("-c", cmd)]

    def do_attach_debug_rtt(self, command, **kwargs):
        if self.gdb_cmd is None:
            raise ValueError('Cannot debug; no gdb specified')
        if self.elf_name is None:
            raise ValueError('Cannot debug; no .elf specified')

        if self.no_flash:
            self.logger.info('Flashing disabled, skipping flash step')
        else:
            self.do_flash()

        self.swtich_to_debug_mode()

        pre_init_cmd = []
        for i in self.pre_init:
            pre_init_cmd.append("-c")
            pre_init_cmd.append(i)

        if self.thread_info_enabled and self.supports_thread_info():
            pre_init_cmd.append("-c")
            rtos_command = f'${self.target_handle} configure -rtos Zephyr'
            pre_init_cmd.append(rtos_command)

        server_cmd = (
            self.openocd_cmd
            + self.serial
            + self.cfg_cmd
            + [
                '-c',
                f'tcl_port {self.tcl_port}',
                '-c',
                f'telnet_port {self.telnet_port}',
                '-c',
                f'gdb_port {self.gdb_port}',
            ]
            + pre_init_cmd
            + self.init_arg
            + self.targets_arg
            + self.halt_arg
        )

        if self.rtt_server and command != 'rtt':
            rtt_address = self.get_rtt_address()
            if rtt_address is None:
                raise ValueError("RTT Control block not found")

            server_cmd = (
                server_cmd
                + ['-c', f'rtt setup 0x{rtt_address:x} 0x10 "SEGGER RTT"']
                + ['-c', 'rtt start']
                + ['-c', f'rtt server start {self.rtt_port} 0']
            )
        if command == 'rtt':
            # Run GDB in batch mode. This will disable pagination automatically
            gdb_args = ['--batch']
        else:
            gdb_args = []
        gdb_cmd = (
            self.gdb_cmd
            + gdb_args
            + self.tui_arg
            + ['-ex', f'target extended-remote :{self.gdb_client_port}', self.elf_name]
        )
        if command == 'debug':
            gdb_cmd.extend(self.load_arg)
            gdb_cmd.append("-ex")
            gdb_cmd.append("monitor reset halt")

        if self.gdb_init is not None:
            for i in self.gdb_init:
                gdb_cmd.append("-ex")
                gdb_cmd.append(i)

        if command == 'rtt':
            rtt_address = self.get_rtt_address()
            if rtt_address is None:
                raise ValueError("RTT Control block not found")

            # start the internal openocd rtt service via gdb monitor commands
            gdb_cmd.extend(['-ex', f'monitor rtt setup 0x{rtt_address:x} 0x10 "SEGGER RTT"'])
            gdb_cmd.extend(['-ex', 'monitor reset halt'])
            gdb_cmd.extend(['-ex', 'monitor rtt start'])
            gdb_cmd.extend(['-ex', f'monitor rtt server start {self.rtt_port} 0'])
            # detach from the target and quit the gdb client session
            gdb_cmd.extend(['-ex', 'detach', '-ex', 'quit'])

        self.require(gdb_cmd[0])
        self.print_gdbserver_message()

        if command in ('attach', 'debug'):
            server_proc = self.popen_ignore_int(server_cmd, stderr=subprocess.DEVNULL)
            try:
                self.run_client(gdb_cmd)
            finally:
                server_proc.terminate()
                server_proc.wait()
        elif command == 'rtt':
            self.print_rttserver_message()
            server_proc = self.popen_ignore_int(server_cmd)

            if os_name != 'nt':
                # Save the terminal settings
                fd = sys.stdin.fileno()
                new_term = termios.tcgetattr(fd)
                old_term = termios.tcgetattr(fd)

                # New terminal setting unbuffered
                new_term[3] = new_term[3] & ~termios.ICANON & ~termios.ECHO
                termios.tcsetattr(fd, termios.TCSAFLUSH, new_term)
            else:
                fd = None
                old_term = None

            try:
                # run the binary with gdb, set up the rtt server (runs to completion)
                subprocess.run(gdb_cmd)
                # run the rtt client in the foreground
                self.run_telnet_client('localhost', self.rtt_port)
            finally:
                if old_term is not None and fd is not None:
                    termios.tcsetattr(fd, termios.TCSAFLUSH, old_term)

                server_proc.terminate()
                server_proc.wait()

    def do_debugserver(self, **kwargs):
        pre_init_cmd = []
        for i in self.pre_init:
            pre_init_cmd.append("-c")
            pre_init_cmd.append(i)

        if self.thread_info_enabled and self.supports_thread_info():
            pre_init_cmd.append("-c")
            rtos_command = f'${self.target_handle} configure -rtos Zephyr'
            pre_init_cmd.append(rtos_command)

        self.swtich_to_debug_mode()

        cmd = (
            self.openocd_cmd
            + self.cfg_cmd
            + [
                '-c',
                f'tcl_port {self.tcl_port}',
                '-c',
                f'telnet_port {self.telnet_port}',
                '-c',
                f'gdb_port {self.gdb_port}',
            ]
            + pre_init_cmd
            + self.init_arg
            + self.targets_arg
            + ['-c', self.reset_halt_cmd]
        )

        if self.rtt_server:
            rtt_address = self.get_rtt_address()
            if rtt_address is None:
                raise ValueError("RTT Control block not found")

            cmd = (
                cmd
                + ['-c', f'rtt setup 0x{rtt_address:x} 0x10 "SEGGER RTT"']
                + ['-c', 'rtt start']
                + ['-c', f'rtt server start {self.rtt_port} 0']
            )

        self.print_gdbserver_message()
        self.check_call(cmd)
