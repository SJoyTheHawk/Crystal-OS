#!/usr/bin/env python3
"""Crystal package tools. Phase 14 provides pack and inspect; transports follow in Phase 15."""
import argparse
import json
import re
import struct
import sys
import zlib
from pathlib import Path

MAX_MANIFEST = 8192
MAX_FILE_BYTES = 5632 * 1024
MAX_ENTRIES = 128
ID = re.compile(r'[a-z][a-z0-9-]*(?:\.[a-z][a-z0-9-]*)+')
SEMVER = re.compile(r'(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})')


def safe_path(path):
    return (0 < len(path.encode()) <= 160 and all(
        part not in ('', '.', '..') and len(part) <= 63 and re.fullmatch(r'[A-Za-z0-9_.-]+', part)
        for part in path.split('/')))


def validate(m):
    if not isinstance(m, dict):
        raise ValueError('Manifest must be an object')
    if type(m.get('abi')) is not int or m['abi'] != 1:
        raise ValueError('abi must be 1')
    if not isinstance(m.get('id'), str) or len(m['id']) > 63 or not ID.fullmatch(m['id']):
        raise ValueError('id must be a reverse-DNS identifier (max 63 bytes)')
    for key, limit in (('name', 31), ('runtime', 16), ('entry', 160), ('bar_style', 5)):
        v = m.get(key)
        if not isinstance(v, str) or not v or len(v.encode()) > limit or any(c in v for c in '\0\r\n\t'):
            raise ValueError(f'Invalid {key}')
    for key in ('version', 'min_os'):
        value = m.get(key)
        match = SEMVER.fullmatch(value) if isinstance(value, str) else None
        if not match or any(int(p) > 65535 for p in match.groups()):
            raise ValueError(f'{key} must be major.minor.patch')
    if m['runtime'] not in ('lua', 'native', 'declarative') or m['bar_style'] not in ('dark', 'light'):
        raise ValueError('Invalid runtime or bar_style')
    if not safe_path(m['entry']) or m['entry'] in ('app.json', 'data') or m['entry'].startswith('data/'):
        raise ValueError('Invalid entry point')
    if 'icon' in m and (not isinstance(m['icon'], str) or not safe_path(m['icon']) or not m['icon'].startswith('assets/')):
        raise ValueError('icon must be a path below assets/')
    for key, minimum, maximum in (('memory_kb', 1, 8192), ('storage_kb', 0, 5632), ('worker_threads', 0, 2)):
        if type(m.get(key)) is not int or not minimum <= m[key] <= maximum:
            raise ValueError(f'Invalid {key}')
    permissions = m.get('permissions', {})
    if not isinstance(permissions, dict):
        raise ValueError('permissions must be an object')
    for key in ('notify', 'audio', 'system_timer', 'location'):
        if key in permissions and type(permissions[key]) is not bool:
            raise ValueError(f'Permission {key} must be boolean')
    hosts = permissions.get('network_hosts', [])
    if not isinstance(hosts, list) or len(hosts) > 16:
        raise ValueError('Invalid network_hosts')
    for host in hosts:
        if not isinstance(host, str) or not 0 < len(host) <= 253 or any(
                not re.fullmatch(r'[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?', part) for part in host.split('.')):
            raise ValueError('network_hosts must contain exact lowercase hostnames')
    if hosts and not m['worker_threads']:
        raise ValueError('Networking requires a worker thread')
    return m


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'Duplicate manifest key: {key}')
        result[key] = value
    return result


def load_manifest(source):
    text = source.read_text(encoding='utf-8')
    try:
        value = json.loads(text, object_pairs_hook=no_duplicates)
    except json.JSONDecodeError:
        try:
            import yaml
        except ImportError as exc:
            raise ValueError('YAML input requires PyYAML: pip install -r tools/requirements-pack.txt') from exc
        class UniqueLoader(yaml.SafeLoader):
            pass
        def mapping(loader, node):
            return no_duplicates(loader.construct_pairs(node, deep=True))
        UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, mapping)
        value = yaml.load(text, Loader=UniqueLoader)
    return validate(value)


def pack(folder, destination, key_path=None, key_id=0):
    folder = folder.resolve()
    manifest = load_manifest(folder / 'app.yaml')
    encoded = json.dumps(manifest, sort_keys=True, separators=(',', ':'), ensure_ascii=True, allow_nan=False).encode()
    if len(encoded) > MAX_MANIFEST:
        raise ValueError('Manifest exceeds 8192 bytes')
    files = []
    for path in sorted(folder.rglob('*')):
        relative = path.relative_to(folder).as_posix()
        if path.is_symlink():
            raise ValueError(f'Symlinks are forbidden: {relative}')
        if relative == 'data' or relative.startswith('data/'):
            continue  # Never ship developer state.
        if path.is_dir():
            continue
        if not path.is_file() or not safe_path(relative) or relative.startswith('.'):
            raise ValueError(f'Invalid package path: {relative}')
        if relative in ('app.yaml', 'app.json') or path.resolve() == destination.resolve():
            continue
        if path.stat().st_size > MAX_FILE_BYTES:
            raise ValueError(f'File too large: {relative}')
        files.append((relative, path.read_bytes()))
    if len(files) > MAX_ENTRIES or sum(len(data) for _, data in files) + len(encoded) > MAX_FILE_BYTES:
        raise ValueError('Package exceeds entry or storage limit')
    names = {name for name, _ in files}
    if manifest['entry'] not in names or ('icon' in manifest and manifest['icon'] not in names):
        raise ValueError('Entry point or icon is missing from package')
    payload = bytearray(b'CAPP' + struct.pack('>HI', 1, len(encoded)) + encoded)
    for name, data in files:
        path = name.encode('ascii')
        payload += struct.pack('>H', len(path)) + path + struct.pack('>II', len(data), zlib.crc32(data)) + data
    payload += b'\0\0'
    if key_path:
        try:
            from cryptography.hazmat.primitives.serialization import load_pem_private_key
            from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
        except ImportError as exc:
            raise ValueError('Signing requires cryptography: pip install -r tools/requirements-pack.txt') from exc
        key = load_pem_private_key(key_path.read_bytes(), password=None)
        if not isinstance(key, Ed25519PrivateKey):
            raise ValueError('Signing key must be Ed25519')
        payload += b'\x01' + key.sign(bytes(payload)) + bytes([key_id])
    else:
        payload += bytes(66)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(payload)
    return manifest


def inspect(path):
    with path.open('rb') as stream:
        def read(size):
            value = stream.read(size)
            if len(value) != size:
                raise ValueError('Truncated package')
            return value
        if read(4) != b'CAPP' or struct.unpack('>H', read(2))[0] != 1:
            raise ValueError('Invalid CAPP header')
        length = struct.unpack('>I', read(4))[0]
        if not 0 < length <= MAX_MANIFEST:
            raise ValueError('Invalid manifest length')
        manifest = validate(json.loads(read(length), object_pairs_hook=no_duplicates))
        files = set()
        total = length
        while True:
            length = struct.unpack('>H', read(2))[0]
            if not length:
                break
            if length > 160 or len(files) >= MAX_ENTRIES:
                raise ValueError('Invalid entry length or count')
            name = read(length).decode('ascii')
            if not safe_path(name) or name in files or name in ('app.json', 'data') or name.startswith(('data/', '.')):
                raise ValueError('Invalid or duplicate entry path')
            files.add(name)
            size, expected = struct.unpack('>II', read(8))
            total += size
            if total > MAX_FILE_BYTES:
                raise ValueError('Package exceeds storage limit')
            crc = 0
            while size:
                data = read(min(size, 2048)); crc = zlib.crc32(data, crc); size -= len(data)
            if crc != expected:
                raise ValueError(f'CRC mismatch: {name}')
        trailer = read(66)
        if stream.read(1) or trailer[0] not in (0, 1) or (trailer[0] == 0 and any(trailer)):
            raise ValueError('Invalid package trailer')
        if manifest['entry'] not in files or ('icon' in manifest and manifest['icon'] not in files):
            raise ValueError('Missing entry point or icon')
        return dict(manifest=manifest, files=sorted(files), signature='ed25519 (not verified)' if trailer[0] else 'unsigned (Developer Mode only)')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    cmd = sub.add_parser('pack')
    cmd.add_argument('folder', type=Path)
    cmd.add_argument('-o', '--output', type=Path)
    signing = cmd.add_mutually_exclusive_group(required=True)
    signing.add_argument('--unsigned', action='store_true', help='explicitly create a Developer Mode package')
    signing.add_argument('--key', type=Path, help='Ed25519 PEM private key')
    cmd.add_argument('--key-id', type=int, default=0, choices=range(256), metavar='0..255')
    cmd = sub.add_parser('inspect'); cmd.add_argument('package', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'inspect':
            print(json.dumps(inspect(args.package), indent=2))
        else:
            m = load_manifest(args.folder / 'app.yaml')
            output = args.output or Path(f"{m['id']}-{m['version']}.capp")
            pack(args.folder, output, args.key, args.key_id)
            print(output)
    except (ValueError, OSError) as exc:
        parser.exit(1, f'crystal: {exc}\n')


if __name__ == '__main__':
    main()
