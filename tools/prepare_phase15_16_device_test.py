#!/usr/bin/env python3
"""Prepare local signed fixtures for the Phase 15/16 hardware test batch."""
import argparse
import json
import os
from pathlib import Path
import shutil
import tempfile

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

import crystal

ROOT = Path(__file__).resolve().parents[1]


def signing_key(path):
    """Reuse the test identity on reruns; never replace an existing private key."""
    if path.exists():
        key = serialization.load_pem_private_key(path.read_bytes(), password=None)
        if not isinstance(key, Ed25519PrivateKey):
            raise ValueError(f'Existing key must be Ed25519: {path}')
    else:
        key = Ed25519PrivateKey.generate()
        with path.open('xb') as stream:
            stream.write(key.private_bytes(
                serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption()))
    path.chmod(0o600)
    return key


def expect_rejected(package, keys, reason):
    try:
        crystal.verify(package, keys)
    except ValueError as exc:
        if reason not in str(exc):
            raise ValueError(f'Unexpected rejection for {package.name}: {exc}') from exc
    else:
        raise ValueError(f'Negative fixture unexpectedly verified: {package.name}')


def prepare(output, key_id=240, untrusted_key_id=241):
    if key_id == untrusted_key_id:
        raise ValueError('Trusted and untrusted test key IDs must differ')
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    output.chmod(0o700)
    private = output / 'test-private.pem'
    public = output / 'test-public.pem'
    key = signing_key(private)
    public.write_bytes(key.public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
    public_hex = key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()
    (output / 'test-public-key.hex').write_text(public_hex + '\n', encoding='ascii')
    untrusted_private = output / 'untrusted-private.pem'
    signing_key(untrusted_private)

    small = output / 'dashboard-signed.capp'
    untrusted = output / 'dashboard-untrusted.capp'
    tampered = output / 'dashboard-tampered.capp'
    large = output / 'dashboard-large-signed.capp'
    guards = output / 'action-guards-signed.capp'
    source = output / 'dashboard-source'
    if source.exists():
        shutil.rmtree(source)
    shutil.copytree(ROOT / 'examples/declarative_dashboard', source,
                    ignore=shutil.ignore_patterns('.*', 'data'))
    crystal.pack(source, small, private, key_id)
    crystal.pack(source, untrusted, untrusted_private, untrusted_key_id)
    changed = bytearray(small.read_bytes())
    changed[-2] ^= 1  # Change only the signature, preserving entry CRCs and key ID.
    tampered.write_bytes(changed)

    with tempfile.TemporaryDirectory(prefix='crystal-phase15-16-') as directory:
        padded = Path(directory) / 'dashboard'
        shutil.copytree(source, padded)
        (padded / 'assets/upload-pad.bin').write_bytes(bytes(range(256)) * 5120)
        crystal.pack(padded, large, private, key_id)
        guard_source = Path(directory) / 'guards'
        guard_source.mkdir()
        manifest = crystal.load_manifest(source / 'app.yaml')
        manifest.update(id='org.crystal.action-guards', name='Action Guard Check',
                        description='Reject timer calls without permission and unsupported actions.',
                        permissions={})
        (guard_source / 'app.yaml').write_text(json.dumps(manifest), encoding='utf-8')
        document = {
            'screen': {
                'type': 'container', 'width': 480, 'height': 440,
                'flex_direction': 'column', 'gap': 16,
                'style': {'bg_color': 1120287, 'padding': 16},
                'children': [
                    {'type': 'label', 'text': 'Action guard check', 'font': 'large',
                     'width': 448, 'align': 'center'},
                    {'type': 'label', 'text': 'Both buttons must leave the timer and device running normally.',
                     'width': 448, 'align': 'center'},
                    {'type': 'label', 'text': '{{timer.remaining}} s remaining',
                     'width': 448, 'align': 'center'},
                    {'type': 'button', 'text': 'Try timer without permission',
                     'on_click': 'timer.arm(300)', 'width': 420, 'height': 56},
                    {'type': 'button', 'text': 'Try unsupported action',
                     'on_click': 'system.reboot', 'width': 420, 'height': 56},
                ],
            },
        }
        (guard_source / 'ui.json').write_text(json.dumps(document), encoding='utf-8')
        crystal.pack(guard_source, guards, private, key_id)

    trusted = {key_id: public}
    for package in (small, large, guards):
        crystal.verify(package, trusted)
    if large.stat().st_size <= 1024 * 1024:
        raise ValueError('Upload fixture must exceed 1 MiB')
    crystal.inspect(tampered)
    expect_rejected(untrusted, trusted, 'No trusted public key configured')
    expect_rejected(tampered, trusted, 'Package signature is invalid')
    record = {
        'key_id': key_id, 'untrusted_key_id': untrusted_key_id,
        'public_key_hex': public_hex, 'public_key_file': str(public),
        'dashboard_source': str(source),
        'packages': {package.name: package.stat().st_size
                     for package in (small, untrusted, tampered, large, guards)},
        'host_verification': 'PASS: signed fixtures verify; untrusted and tampered fixtures reject',
        'device_tests': 'NOT RUN',
    }
    (output / 'fixtures.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path,
                        default=ROOT / '.cache/phase15-16-device-test')
    parser.add_argument('--key-id', type=int, default=240, choices=range(1, 256), metavar='1..255')
    parser.add_argument('--untrusted-key-id', type=int, default=241,
                        choices=range(1, 256), metavar='1..255')
    args = parser.parse_args()
    try:
        os.umask(0o077)
        print(json.dumps(prepare(args.output_dir, args.key_id, args.untrusted_key_id), indent=2))
    except (OSError, ValueError) as exc:
        parser.exit(1, f'Fixture preparation failed: {exc}\n')


if __name__ == '__main__':
    main()
