#!/usr/bin/env python3

# Copyright (c) 2025 Siemens Mobility GmbH
#
# SPDX-License-Identifier: Apache-2.0

# WARNING / TODO:
# This is a quite minimal implementation for transfers similar to the XMODEM1k protocol.
# The code here isn't compliant / functional in terms of retransmission attempts and timeouts but enough for using it with the AM2434
#
# Additionally it's possible to add a small header for usage with the xmodem1k_flasher sample to flash data to external flash memory
#
# THIS CODE IS NOT READY TO BE SUBMITTED UPSTREAM YET AND IS ONLY PUBLIC AS WORK-IN-PROGRESS / PROOF-OF-CONCEPT!

import argparse
import struct
import sys
import textwrap
import time
from pathlib import Path

import serial

# XMODEM1k bytes
SOH = 0x01.to_bytes()
STX = 0x02.to_bytes()

EOT = 0x04.to_bytes()
ACK = 0x06.to_bytes()
NAK = 0x15.to_bytes()
CAN = 0x18.to_bytes()
PING = 0x43.to_bytes()


# Header bits
NO_VERIFICATION = 1 << 0


class TransferDataCreator:
    def __init__(self, data: bytes, polynomial: int = 0x1021):
        self._polynomial = polynomial
        self._data = data
        self._header = None

    def add_header(self, flash_offset: int, flash_size: int, flags: int):
        """
        Add data to a header that will be sent first and can be parsed by the
        xmodem1k_flasher sample
        """
        self._header = struct.pack(">LLL", flash_offset, flash_size, flags)

    def calc_crc16(self, data: bytes, initial_crc: int = 0x0) -> int:
        """
        Calculate the CRC16 checksum for multiple bytes
        """
        crc = initial_crc
        for byte in data:
            crc = self._calc_crc16_single_byte(byte, crc)
        return crc

    def _calc_crc16_single_byte(self, byte: int, crc: int) -> int:
        """
        Calculate the CRC16 checksum for a single byte
        """
        # the actual CRC is in the upper 16 bit of the combined variable
        combined = crc ^ (byte << 8)

        for _ in range(0, 16):
            if combined & 0x800000 != 0:
                combined = (combined << 1) ^ (self._polynomial << 8)
            else:
                combined <<= 1
            combined &= 0xFFFFFF
        return (combined >> 8) & 0xFFFF

    def _pad_data(self, data: bytes) -> bytes:
        """
        Pad data to either 128 or 1024 bytes with zero bytes
        """
        ret = data
        current_len = len(data)

        assert current_len <= 1024

        if current_len <= 128:
            for _ in range(128 - current_len):
                ret += b'\x00'
        else:
            for _ in range(1024 - current_len):
                ret += b'\x00'
        return ret

    def _construct_single_chunk(self, data: bytes, block_num: int) -> bytes:
        """
        Construct a full single chunk for transfer
        """
        chunk = b''
        if len(data) > 128:
            chunk += STX
        else:
            chunk += SOH
        chunk += block_num.to_bytes()
        chunk += (0xFF - block_num).to_bytes()

        data = self._pad_data(data)

        chunk += data

        crc = self.calc_crc16(data)

        chunk += ((crc >> 8) & 0xFF).to_bytes()
        chunk += (crc & 0xFF).to_bytes()

        return chunk

    def get_transfer_chunks(self) -> list[bytes]:
        """
        Get a list of all chunks that need to be transferred
        """
        data_parts: list[bytes] = []
        current_data = b''
        current_number_of_bytes = 0
        for byte in self._data:
            current_data += byte.to_bytes()
            current_number_of_bytes += 1
            if current_number_of_bytes == 1024:
                data_parts.append(current_data)
                current_data = b''
                current_number_of_bytes = 0

        if current_number_of_bytes != 0:
            data_parts.append(current_data)

        chunks: list[bytes] = []
        block_num = 0x1

        if self._header is not None:
            chunks.append(self._construct_single_chunk(self._header, block_num))
            block_num += 1

        for part in data_parts:
            chunks.append(self._construct_single_chunk(part, block_num))
            block_num += 1
            if block_num > 0xFF:
                block_num = 0x0

        return chunks


class FirmwareUploader:
    def __init__(self, chunks: list[bytes], uart_path: str):
        self._chunks: list[bytes] = chunks
        self._uart_path: str = uart_path

    def upload_data(self):
        """
        Upload the firmware to the device by sending it via UART
        """
        uart = serial.Serial(
            self._uart_path, baudrate=115200, bytesize=8, parity='N', stopbits=1, timeout=5
        )

        # due to some weird behaviour a delay is needed after the serial port is
        # created. Otherwise a read will return nothing.
        #
        # Found out via this bug report:
        # https://github.com/pyserial/pyserial/issues/735
        time.sleep(3)

        # flush available data first; this is done since some SoCs send their
        # device number or buffered output from the last application that ran
        # first which would otherwise be interpreted as a violation of the
        # XMODEM protocol
        read_response = uart.read_all()
        if read_response is None:
            read_response = uart.read()
        if len(read_response) == 0 or not read_response.endswith(PING):
            raise OSError(
                "Didn't receive expected 'C' ping. "
                "Please ensure the SoC is in UART mode and waiting for firmware"
            )

        index = 1
        for chunk in self._chunks:
            print(
                f"Sending chunk {index} / {len(self._chunks)} "
                f"(~ {round(index * 100 / len(self._chunks), 2)}%)"
            )

            uart.write(chunk)
            response = uart.read()
            empty = uart.read_all()

            if response != ACK or (empty is not None and len(empty) != 0):
                print(f"response = {response}")
                raise OSError("Didn't receive expected ACK. Stopping transmission")

            index += 1

        uart.write(EOT)
        if uart.read() != ACK:
            raise OSError("No ACK received for EOT message")

        print("Finished sending all chunks successfully")


def parse_arguments():
    # TODO: Help strings!
    argument_parser = argparse.ArgumentParser(allow_abbrev=False)
    argument_parser.register('type', 'int_auto_base', lambda i: int(i, 0))

    argument_parser.add_argument("--file", type=Path, required=True)
    argument_parser.add_argument("--uart-device", type=str, required=True)

    argument_parser.add_argument("--flash-offset", type='int_auto_base', required=False)

    argument_parser.add_argument("--no-verify", action='store_true')

    return argument_parser.parse_args()


def main(args: argparse.Namespace):
    print(
        textwrap.dedent("""
                          WARNING: This is just a quite minimal implementation of the XMODEM1k protocol
                                   that isn't compliant to the original standard!

                                   Please take a look at the comment in the scripts/support/xmodem1k_sender.py file!
                          """)
    )

    data = None
    with open(args.file, 'rb') as f:
        data = f.read()

    flash_size = len(data)

    if len(data) == 0:
        raise OSError("Some error occurred while reading the data from the file")

    flags = 0x0
    if args.no_verify:
        flags |= NO_VERIFICATION

    transfer_data_creator = TransferDataCreator(data)

    if args.flash_offset is not None:
        transfer_data_creator.add_header(args.flash_offset, flash_size, flags)

    chunks = transfer_data_creator.get_transfer_chunks()

    uploader = FirmwareUploader(chunks, args.uart_device)
    uploader.upload_data()


if __name__ == "__main__":
    sys.exit(main(parse_arguments()))
