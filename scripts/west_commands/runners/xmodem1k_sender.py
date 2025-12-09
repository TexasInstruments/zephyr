#!/usr/bin/env python3

# Copyright (c) 2025 Siemens Mobility GmbH
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import sys
from os import path

from zephyr_ext_common import ZEPHYR_BASE

from runners.core import RunnerCaps, RunnerConfig, ZephyrBinaryRunner


class FlashHeader:
    def __init__(self, offset: int, further_options: list[str]):
        self._options: list[str] = ["--flash-offset", str(offset)]
        self._options += further_options

    def get_options(self) -> list[str]:
        """
        Get all required commandline flags for sending the header
        """
        return self._options


class Xmodem1KSender(ZephyrBinaryRunner):
    def __init__(
        self,
        cfg: RunnerConfig,
        dev_id: str | None,
        file: str | None,
        flash_header: FlashHeader | None,
    ):
        super().__init__(cfg)

        self._script_path = path.join(ZEPHYR_BASE, "scripts", "support", "xmodem1k_sender.py")
        self._flash_header = flash_header

        if file is None:
            raise RuntimeError("XMODEM1k binary runner is missing path to file that should be sent")

        self._file = file

        if dev_id is None:
            self._dev_id = '/dev/ttyACM0'
            self.logger.warning("Device ID wasn't specified. Defaulting to %s", self._dev_id)
        else:
            self._dev_id = dev_id

    @classmethod
    def capabilities(cls) -> RunnerCaps:
        return RunnerCaps(commands={"flash"}, dev_id=True, file=True)

    @classmethod
    def name(cls) -> str:
        return "xmodem1k_sender"

    @classmethod
    def do_add_parser(cls, parser: argparse.ArgumentParser) -> None:
        # dev_id and file are automatically added due to the runner capabilities
        parser.register('type', 'int_auto_base', lambda i: int(i, 0))

        parser.add_argument(
            "--no-header",
            action='store_true',
            help="Don't include the flash header and only send the plain binary file via the XMODEM1k protocol",
        )

        parser.add_argument("--flash-offset", type='int_auto_base', required=False)
        parser.add_argument(
            "--no-verify",
            action='store_true',
            help="Don't do flash verifications directly after writing",
        )

        return None

    @classmethod
    def dev_id_help(cls) -> str:
        return (
            "Name/Path of the UART interface. "
            "E.g. /dev/ttyACM0 on Unix-like systems or COM0 on Windows"
        )

    @classmethod
    def do_create(cls, cfg: RunnerConfig, args: argparse.Namespace) -> 'ZephyrBinaryRunner':
        flash_header = None
        if not args.no_header:
            extra_options: list[str] = []
            if args.no_verify:
                extra_options.append("--no-verify")

            flash_header = FlashHeader(args.flash_offset, extra_options)

        return Xmodem1KSender(cfg, args.dev_id, args.file, flash_header)

    def do_run(self, command: str, **kwargs) -> None:
        cmd: list[str] = [
            sys.executable,
            self._script_path,
            "--uart-device",
            self._dev_id,
            "--file",
            self._file,
        ]

        if self._flash_header is not None:
            cmd += self._flash_header.get_options()
        else:
            self.logger.warning(
                "This runner just sends a file via the XModem protocol, except when "
                "a header is included for the xmodem1k_flasher sample. "
                "Some SoCs will load it into SRAM and execute it from there "
                "instead of writing it into flash!"
            )

        self.check_call(cmd)
