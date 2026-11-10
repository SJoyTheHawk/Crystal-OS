#!/usr/bin/env python3
"""Build the isolated Calculator fixture embedded by the opt-in device test."""
import argparse
import json
from pathlib import Path
import tempfile

import crystal

ROOT = Path(__file__).resolve().parents[1]
TEST_ID = 'org.crystal.phase14.calculator'


def prepare(output):
    source = ROOT / 'examples/calculator_package'
    manifest = crystal.load_manifest(source / 'app.yaml')
    manifest.update(id=TEST_ID, name='Calculator Test')
    with tempfile.TemporaryDirectory(prefix='crystal-device-fixture-') as directory:
        folder = Path(directory)
        (folder / 'app.yaml').write_text(json.dumps(manifest), encoding='utf-8')
        (folder / 'calculator.native').write_bytes((source / 'calculator.native').read_bytes())
        crystal.pack(folder, output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    prepare(args.output)


if __name__ == '__main__':
    main()
