#!/usr/bin/env python3
#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
"""
This script uses edtlib to parse a pickled devicetree object and generate C source files
that provide image UUIDs (VIDs and CIDs) for the current image as well as maps of UUIDs for all
images defined in the bootloader configuration.
"""

import argparse
import os
import sys
from jinja2 import FileSystemLoader, Environment
from imgtool.bootlib import BootUuid, BootConfiguration

def parse_args():
	parser = argparse.ArgumentParser(allow_abbrev=False)
	parser.add_argument("--edt-pickle", required=True,
						help="path to read the pickled devicetree object from")
	parser.add_argument("--output-dir", required=True,
						help="path to write the output files to")
	parser.add_argument("--uuid-vid-map", required=False, default=False, action='store_true',
						help="output a map of UUID VIDs")
	parser.add_argument("--uuid-cid-map", required=False, default=False, action='store_true',
						help="output a map of UUID CIDs")
	parser.add_argument("--uuid-vid", required=False, default=False, action='store_true',
						help="output the current image UUID VID")
	parser.add_argument("--uuid-cid", required=False, default=False, action='store_true',
						help="output the current image UUID CID")
	return parser.parse_args()


def main():
	args = parse_args()
	cfg = BootConfiguration(args.edt_pickle)
	env = Environment(
		loader=FileSystemLoader([os.path.join(os.path.dirname(__file__),"templates")]),
		trim_blocks=False,
		lstrip_blocks=False
	)
	data = {
		'image': cfg.image,
		'bootloader': cfg.bootloader,
	}

	source_files = []
	if args.uuid_vid_map:
		if cfg.bootloader is None:
			sys.exit("Could not find bootloader configuration in the devicetree")
		print(f'Writing UUID VID map to {os.path.join(args.output_dir, "uuid-vid-map.c")}')
		with open(os.path.join(args.output_dir, "uuid-vid-map.c"), "w", encoding="utf-8") as file:
			template = env.get_template("uuid-map.c.jinja2")
			data['uuid_type'] = 'vid'
			file.write(template.render(data))
			source_files.append("uuid-vid-map.c")

	if args.uuid_cid_map:
		if cfg.bootloader is None:
			sys.exit("Could not find bootloader configuration in the devicetree")
		print(f'Writing UUID CID map to {os.path.join(args.output_dir, "uuid-cid-map.c")}')
		with open(os.path.join(args.output_dir, "uuid-cid-map.c"), "w", encoding="utf-8") as file:
			template = env.get_template("uuid-map.c.jinja2")
			data['uuid_type'] = 'cid'
			file.write(template.render(data))
			source_files.append("uuid-cid-map.c")

	if args.uuid_vid:
		if cfg.image is None:
			sys.exit("Could not find image configuration in the devicetree")
		print(f'Writing UUID VID for the current image to {os.path.join(args.output_dir, "uuid-vid.c")}')
		with open(os.path.join(args.output_dir, "uuid-vid.c"), "w", encoding="utf-8") as file:
			template = env.get_template("uuid.c.jinja2")
			data['uuid_type'] = 'vid'
			file.write(template.render(data))
			source_files.append("uuid-vid.c")

	if args.uuid_cid:
		if cfg.image is None:
			sys.exit("Could not find image configuration in the devicetree")
		print(f'Writing UUID CID for the current image to {os.path.join(args.output_dir, "uuid-cid.c")}')
		with open(os.path.join(args.output_dir, "uuid-cid.c"), "w", encoding="utf-8") as file:
			template = env.get_template("uuid.c.jinja2")
			data['uuid_type'] = 'cid'
			file.write(template.render(data))
			source_files.append("uuid-cid.c")

	print(f'Writing CMake library to {os.path.join(args.output_dir, "uuid.cmake")}')
	with open(os.path.join(args.output_dir, "uuid.cmake"), "w", encoding="utf-8") as cmake_file:
		template = env.get_template("uuid.cmake.jinja2")
		data = {'source_files': source_files}
		cmake_file.write(template.render(data))


if __name__ == "__main__":
	main()
