#!/usr/bin/env python3

# Copyright (c) 2025 Siemens Mobility GmbH
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import subprocess
import sys
from pathlib import Path

# shaType 2.16.840.1.101.3.4.2.3 is SHA512

HELP_STRING: str = """\
Usage: python3 ti-k3-rom-image-gen.py []
"""

CERTIFICATE_CONFIG_TEMPLATE: str = """\
[ req ]
distinguished_name = req_distinguished_name
x509_extensions = v3_ca
prompt = no

dirstring_type = nobmp

[ req_distinguished_name ]
{CERTIFICATE_INFO}

[ v3_ca ]
basicConstraints = CA:true
1.3.6.1.4.1.294.1.9 = ASN1:SEQUENCE:ext_boot_info

[ ext_boot_info ]
extImgSize = INTEGER:{SUM_SIZE_ALL_BINARIES}
numComp = INTEGER:4
sbl = SEQUENCE:boot_image
fw = SEQUENCE:sysfw_image
bd1 = SEQUENCE:sysfw_cert
bd2 = SEQUENCE:board_config

[ boot_image ]
compType = INTEGER:1
bootCore = INTEGER:16
compOpts = INTEGER:0
destAddr = FORMAT:HEX,OCT:{USER_FIRMWARE_DESTINATION}
compSize = INTEGER:{USER_FIRMWARE_SIZE}
shaType = OID:2.16.840.1.101.3.4.2.3
shaValue = FORMAT:HEX,OCT:{USER_FIRMWARE_HASH}

[ sysfw_image ]
compType = INTEGER:2
bootCore = INTEGER:0
compOpts = INTEGER:0
destAddr = FORMAT:HEX,OCT:{SYSFW_BLOB_DESTINATION}
compSize = INTEGER:{SYSFW_BLOB_SIZE}
shaType = OID:2.16.840.1.101.3.4.2.3
shaValue = FORMAT:HEX,OCT:{SYSFW_BLOB_HASH}

[ sysfw_cert ]
compType = INTEGER:3
bootCore = INTEGER:0
compOpts = INTEGER:0
destAddr = FORMAT:HEX,OCT:00000000
compSize = INTEGER:{SYSFW_CERTIFICATE_SIZE}
shaType = OID:2.16.840.1.101.3.4.2.3
shaValue = FORMAT:HEX,OCT:{SYSFW_CERTIFICATE_HASH}

[ board_config ]
compType = INTEGER:18
bootCore = INTEGER:0
compOpts = INTEGER:0
destAddr = FORMAT:HEX,OCT:0007B000
compSize = INTEGER:{BOARDCONFIG_SIZE}
shaType = OID:2.16.840.1.101.3.4.2.3
shaValue = FORMAT:HEX,OCT:{BOARDCONFIG_HASH}
"""


class BinaryData:
    """
    Class that represents a single input file and holds both the data and some
    metadata (size + sha512 hash)
    """

    def __init__(self, path: Path):
        with open(path, "rb") as file:
            sha512 = hashlib.sha512()

            data: bytes = file.read()
            sha512.update(data)
            hash_sha512: str = sha512.hexdigest()
            # add padding, if necessary
            for _ in range(0, sha512.digest_size - int(len(hash_sha512) / 2)):
                hash_sha512 = "0" + hash_sha512

            self._size: int = len(data)
            self._data: bytes = data
            self._hash: str = hash_sha512

    def get_size(self) -> int:
        """
        Get size in bytes
        """
        return self._size

    def get_data(self) -> bytes:
        """
        Get all data bytes
        """
        return self._data

    def get_hash(self) -> str:
        """
        Get SHA512 hash
        """
        return self._hash


class BinaryFileBundle:
    """
    Class that holds all input binary files
    """

    def __init__(
        self,
        user_firmware: BinaryData,
        sysfw_blob: BinaryData,
        sysfw_cert: BinaryData,
        board_config: BinaryData,
    ):
        self._user_firmware = user_firmware
        self._sysfw_blob = sysfw_blob
        self._sysfw_cert = sysfw_cert
        self._board_config = board_config

    def get_user_firmware(self) -> BinaryData:
        """
        Get user firmware
        """
        return self._user_firmware

    def get_sysfw_blob(self) -> BinaryData:
        """
        Get SYSFW binary blob
        """
        return self._sysfw_blob

    def get_sysfw_cert(self) -> BinaryData:
        """
        Get SYSFW certificate
        """
        return self._sysfw_cert

    def get_board_config(self) -> BinaryData:
        """
        Get board config binary
        """
        return self._board_config


class OutputBinary:
    """
    Class to easily create the output binary from the input files
    """

    def __init__(
        self, input_binaries: BinaryFileBundle, certificate_dn_section: str, private_key_path: str
    ):
        self._binaries = input_binaries
        self._certificate_dn_section = certificate_dn_section
        self._private_key_path = private_key_path

    def _generate_certificate_config(
        self, user_firmware_load_address: str, sysfw_load_address: str
    ) -> str:
        combined_size: int = (
            self._binaries.get_user_firmware().get_size()
            + self._binaries.get_sysfw_blob().get_size()
            + self._binaries.get_sysfw_cert().get_size()
            + self._binaries.get_board_config().get_size()
        )

        cert_text = CERTIFICATE_CONFIG_TEMPLATE.format(
            CERTIFICATE_INFO=self._certificate_dn_section,
            SUM_SIZE_ALL_BINARIES=combined_size,
            USER_FIRMWARE_DESTINATION=user_firmware_load_address,
            USER_FIRMWARE_SIZE=self._binaries.get_user_firmware().get_size(),
            USER_FIRMWARE_HASH=self._binaries.get_user_firmware().get_hash(),
            SYSFW_BLOB_DESTINATION=sysfw_load_address,
            SYSFW_BLOB_SIZE=self._binaries.get_sysfw_blob().get_size(),
            SYSFW_BLOB_HASH=self._binaries.get_sysfw_blob().get_hash(),
            SYSFW_CERTIFICATE_SIZE=self._binaries.get_sysfw_cert().get_size(),
            SYSFW_CERTIFICATE_HASH=self._binaries.get_sysfw_cert().get_hash(),
            BOARDCONFIG_SIZE=self._binaries.get_board_config().get_size(),
            BOARDCONFIG_HASH=self._binaries.get_board_config().get_hash(),
        )

        return cert_text

    def write_output(
        self,
        user_firmware_load_address: int,
        sysfw_load_address: int,
        working_directory: Path,
        output_file_name: str,
    ):
        """
        Create output binary that can be loaded by the ROM bootloader
        """
        certificate_config: Path = working_directory / "certificate.cfg"
        certificate_output: Path = working_directory / "certificate.der"

        user_firmware_load_address_hex = format(user_firmware_load_address, "08x")
        sysfw_load_address_hex = format(sysfw_load_address, "08x")

        with open(certificate_config, "w", encoding='utf-8') as file:
            file.write(
                self._generate_certificate_config(
                    user_firmware_load_address_hex, sysfw_load_address_hex
                )
            )

        try:
            subprocess.run(
                f"openssl req -new -x509 -key {self._private_key_path} \
                           -nodes -outform DER -out {certificate_output} \
                           -config {certificate_config} \
                           -sha512",
                shell=True,
                capture_output=True,
                check=True,
            )
        except subprocess.CalledProcessError as e:
            print(
                "Generating certificate with openssl failed with the following output:\n"
                "--- begin of openssl output ---\n"
                f"{e.stderr.decode('utf-8')}"
                "--- end of openssl output ---",
                file=sys.stderr,
            )
            sys.exit(e.returncode)

        certificate_binary = BinaryData(certificate_output)

        combined_data = certificate_binary.get_data()
        combined_data += self._binaries.get_user_firmware().get_data()
        combined_data += self._binaries.get_sysfw_blob().get_data()
        combined_data += self._binaries.get_sysfw_cert().get_data()
        combined_data += self._binaries.get_board_config().get_data()

        with open(working_directory / output_file_name, "wb") as output_file:
            output_file.write(combined_data)


def parse_arguments():
    argument_parser = argparse.ArgumentParser(allow_abbrev=False)

    argument_parser.register('type', 'int_auto_base', lambda i: int(i, 0))

    argument_parser.add_argument("--user-firmware-path", type=Path, required=True)
    argument_parser.add_argument("--sysfw-blob-path", type=Path, required=True)
    argument_parser.add_argument("--sysfw-cert-path", type=Path, required=True)
    argument_parser.add_argument("--boardconfig-binary-path", type=Path, required=True)

    argument_parser.add_argument("--private-key-path", type=Path, required=True)

    argument_parser.add_argument(
        "--user-firmware-load-address", type='int_auto_base', required=True
    )
    argument_parser.add_argument("--sysfw-blob-load-address", type='int_auto_base', required=True)

    argument_parser.add_argument("--working-directory", type=Path, required=True)
    argument_parser.add_argument("--output-file-name", type=Path, required=True)

    return argument_parser.parse_args()


def main():
    arguments = parse_arguments()

    user_firmware = BinaryData(Path(arguments.user_firmware_path))
    sysfw_blob = BinaryData(Path(arguments.sysfw_blob_path))
    sysfw_cert = BinaryData(Path(arguments.sysfw_cert_path))
    board_config = BinaryData(Path(arguments.boardconfig_binary_path))
    input_binaries = BinaryFileBundle(user_firmware, sysfw_blob, sysfw_cert, board_config)

    cert_dn = "CN = Example Certificate"

    output = OutputBinary(input_binaries, cert_dn, arguments.private_key_path)

    output.write_output(
        arguments.user_firmware_load_address,
        arguments.sysfw_blob_load_address,
        arguments.working_directory,
        arguments.output_file_name,
    )


if __name__ == "__main__":
    main()
