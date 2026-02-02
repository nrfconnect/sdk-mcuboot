#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
"""Bootloader metadata and configuration library."""

import os
import pickle
import re
import sys
import uuid


class BootUuid:
    """Class to represent a UUID in various formats."""

    def __init__(self, namespace, value):
        """Initialize the UUID from various input formats.

        :param namespace: UUID namespace to use when generating UUID from string.
        :param value: UUID value in one of the supported formats:
                      - RAW format: 12345678-1234-5678-1234-567812345678
                      - RAW HEX format: 12345678123456781234567812345678
                      - String format: any printable string
        """
        self._namespace = None
        self._input = None
        self._uuid = None

        # Check if UUID is in the RAW format (12345678-1234-5678-1234-567812345678)
        uuid_re = r'[0-9A-f]{8}-[0-9A-f]{4}-[0-9A-f]{4}-[0-9A-f]{4}-[0-9A-f]{12}'
        if re.match(uuid_re, value):
            self._uuid = uuid.UUID(hex=value)

        # Check if UUID is in the RAW HEX format (12345678123456781234567812345678)
        elif re.match(r'[0-9A-f]{32}', value):
            self._uuid = uuid.UUID(hex=value)

        # Check if UUID is in the string format
        elif value.isprintable():
            if namespace is not None:
                if isinstance(namespace, BootUuid):
                    if namespace.input is not None:
                        self._namespace = namespace.input
                    else:
                        self._namespace = str(namespace)
                elif namespace == uuid.NAMESPACE_DNS:
                    self._namespace = "NAMESPACE_DNS"
                else:
                    self._namespace = str(namespace)
                self._input = value
                self._uuid = uuid.uuid5(namespace, value)
            else:
                raise ValueError(f"Unknown namespace for UUID: {value}")
        else:
            raise ValueError(f"Unknown UUID format: {value}")

    @property
    def input(self):
        """Return the input parameter used to create the UUID."""
        return self._input

    @property
    def bytes(self):
        """Return the UUID as a bytes object."""
        return self._uuid.bytes

    @property
    def c_array(self):
        """Return the UUID as a C array initializer string."""
        return ', '.join(f'0x{b:02x}' for b in self._uuid.bytes)

    def __repr__(self):
        """Return input parameter used to create the UUID."""
        if self._namespace is not None:
            return f'(namespace: {self._namespace}, value: {self._input})'
        return str(self._uuid)


def get_addr_range(node):
    """Get the start (absolute) address and size of a devicetree node.

    :param node: The devicetree node to get the address range for.
    :return: A tuple containing the (start address, size) of the node.
    """
    start_addr = 0
    size = 0

    if node is None:
        return 0, 0
    if node.unit_addr is not None:
        start_addr = node.unit_addr
    if node.regs is not None and len(node.regs) > 0:
        size = node.regs[0].size
    return start_addr, size


class Image:
    """Class to represent an image configuration from the devicetree."""

    def __init__(self, config_node, active_partition):
        """Initialize an Image instance.

        :param config_node: The devicetree node representing the image configuration.
        :param active_partition: The active partition associated with the image.
        """
        self.name = config_node.name
        self.config_node = config_node
        self.active_partition = active_partition
        self.partitions = []
        self.index = None
        self.vid = None
        self.cid = None

        uuid_vids = []
        uuid_cids = []
        partitions = []

        if 'image-index' in self.config_node.props:
            self.index = self.config_node.props['image-index'].val
        if 'uuid-vid' in self.config_node.props:
            uuid_vids = self.config_node.props['uuid-vid'].val
        if 'uuid-cid' in self.config_node.props:
            uuid_cids = self.config_node.props['uuid-cid'].val
        if 'partitions' in self.config_node.props:
            partitions = self.config_node.props['partitions'].val
        for i in range(len(partitions)):
            vid = BootUuid(uuid.NAMESPACE_DNS, uuid_vids[i]) if i < len(uuid_vids) else None
            cid = BootUuid(vid, uuid_cids[i]) if i < len(uuid_cids) else None
            off, size = get_addr_range(partitions[i])
            dev = self._get_c_path(partitions[i].flash_controller)
            labels = partitions[i].labels
            if partitions[i].label is not None:
                labels = [partitions[i].label] + labels
            if i == self.active_partition:
                self.vid = vid
                self.cid = cid

            self.partitions.append({
                'uuid-vid': vid,
                'uuid-cid': cid,
                'partition': {
                    'off': off,
                    'size': size,
                    'dev': dev,
                    'labels': labels
                }
            })

    @staticmethod
    def _get_c_path(node):
        """Return the node's path that can be passed to C macros.

        :param node: The devicetree node to return the path for.
        :return: The node's path formatted for C macros.
        """
        return node.path.replace('@', '_').replace('-', '_').split('/')[1:]


class Bootloader:
    """Class to represent a bootloader configuration from the devicetree."""

    def __init__(self, config_node):
        """Initialize a Bootloader instance.

        :param config_node: The devicetree node representing the bootloader configuration.
        """
        self.images = {}
        if 'images' not in config_node.children:
            return
        for name, image in config_node.children['images'].children.items():
            self.images[name] = Image(image, None)
        self.images = dict(sorted(self.images.items(), key=lambda item: item[1].index))


class BootConfiguration:
    """Class to represent the boot configuration, including bootloader and image information."""

    def __init__(self, edt_pickle_path):
        """Initialize a BootConfiguration instance by parsing the devicetree.

        :param edt_pickle_path: The path to the pickled EDT object representing the devicetree.
        """
        self.bootloader = None
        self.image = None
        edt = self.load(edt_pickle_path)

        # Use zephyr,code-partition chosen node to identify:
        # - which configuration applies to the current image
        # - provide indexes and UUIDs for the current image
        code_partition_node = edt.chosen_nodes['zephyr,code-partition']
        code_partition_addr, code_partition_size = get_addr_range(code_partition_node)

        for bl_config_node in edt.compat2nodes['nordic,mcuboot']:
            # First - check if the code partition points to a bootloader partition
            bl_partition_idx = self._find_matching_partition(
                bl_config_node, code_partition_addr, code_partition_size
            )
            if bl_partition_idx is not None:
                # Found bootloader partition.
                self.bootloader = Bootloader(bl_config_node)
                # Continue searching to check if the bootloader is not mentioned
                # as a higher-level bootloader image.
                continue

            # Next - check if the code partition points to an image partition
            if 'images' not in bl_config_node.children:
                continue
            for image_node in bl_config_node.children['images'].children.values():
                image_idx = self._find_matching_partition(
                    image_node, code_partition_addr, code_partition_size
                )
                if image_idx is not None:
                    if self.image is not None:
                        raise ValueError("Multiple matching images found for the code partition. "
                            "Please ensure only one image matches the code partition "
                            f"(0x{code_partition_addr:X}, 0x{code_partition_size:X}).")
                    self.image = Image(image_node, image_idx)
                    if self.bootloader is None:
                        self.bootloader = Bootloader(bl_config_node)
    @staticmethod
    def _find_matching_partition(node, addr, size):
        """Find the index of the partition that matches the provided address and size.

        :param node: The devicetree node to search for partitions.
        :param addr: The starting address of the partition to match.
        :param size: The size of the partition to match.
        :return: The index of the matching partition if found, otherwise None.
        """
        if 'partitions' not in node.props:
            return None
        partitions = node.props['partitions'].val
        if not isinstance(partitions, list):
            partitions = [partitions]
        for partition_idx, partition_node in enumerate(partitions):
            node_addr, node_size = get_addr_range(partition_node)
            if (addr >= node_addr) and ((addr + size) <= (node_addr + node_size)):
                # Found matching partition partition.
                return partition_idx
        return None

    def load(self, edt_pickle_path):
        """Load the boot configuration from a pickled EDT object.

        :param edt_pickle_path: The path to the pickled EDT object representing the devicetree.
        :return: The loaded EDT object.
        """
        try:
            zephyr_base = os.environ['ZEPHYR_BASE']
        except KeyError:
            print('Need to either have ZEPHYR_BASE in environment or pass as argument')
            sys.exit(1)

        path = os.path.join(zephyr_base, "scripts", "dts", "python-devicetree", "src")
        if path not in sys.path:
            sys.path.insert(0, path)

        import devicetree.edtlib

        with open(edt_pickle_path, 'rb') as f:
            edt = pickle.load(f)
            assert isinstance(edt, devicetree.edtlib.EDT)
        return edt
