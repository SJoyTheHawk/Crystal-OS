#!/usr/bin/env python3
"""Build, inspect, and verify Crystal app packages."""
import argparse
import json
import re
import struct
import sys
import base64
import hashlib
import urllib.error
import urllib.parse
import urllib.request
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
        if not 1 <= key_id <= 255:
            raise ValueError('Signed packages require a key ID from 1 to 255')
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
        signed_length = stream.tell()
        trailer = read(66)
        if stream.read(1) or trailer[0] not in (0, 1) or (trailer[0] == 0 and any(trailer)):
            raise ValueError('Invalid package trailer')
        if manifest['entry'] not in files or ('icon' in manifest and manifest['icon'] not in files):
            raise ValueError('Missing entry point or icon')
        signature = 'unsigned (Developer Mode only)'
        if trailer[0]:
            signature = 'ed25519 (not verified)'
        return dict(manifest=manifest, files=sorted(files), signature=signature,
                    key_id=trailer[65] if trailer[0] else None,
                    signed_length=signed_length)


def load_public_key(path):
    try:
        from cryptography.hazmat.primitives.serialization import load_pem_public_key
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    except ImportError as exc:
        raise ValueError('Verification requires cryptography: pip install -r tools/requirements-pack.txt') from exc
    key = load_pem_public_key(path.read_bytes())
    if not isinstance(key, Ed25519PublicKey):
        raise ValueError('Verification key must be Ed25519')
    return key


def verify(path, trusted_keys):
    result = inspect(path)
    if result['key_id'] is None:
        raise ValueError('Unsigned packages cannot be signature-verified')
    key_path = trusted_keys.get(result['key_id'])
    if key_path is None:
        raise ValueError(f"No trusted public key configured for key ID {result['key_id']}")
    key = load_public_key(key_path)
    data = path.read_bytes()
    try:
        key.verify(data[result['signed_length'] + 1:result['signed_length'] + 65],
                   data[:result['signed_length']])
    except Exception as exc:
        raise ValueError('Package signature is invalid') from exc
    result['signature'] = 'ed25519 (verified)'
    return result


def parse_trusted_key(value):
    try:
        key_id, path = value.split('=', 1)
        key_id = int(key_id, 10)
    except (ValueError, AttributeError) as exc:
        raise argparse.ArgumentTypeError('trusted keys must be KEY_ID=PUBLIC_KEY.pem') from exc
    if not 1 <= key_id <= 255 or not path:
        raise argparse.ArgumentTypeError('key ID must be 1..255 and public key path must not be empty')
    return key_id, Path(path)


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'),
                      ensure_ascii=True, allow_nan=False).encode('utf-8')


def validate_catalog(value):
    if not isinstance(value, dict) or set(value) != {'schema', 'generated', 'apps'}:
        raise ValueError('Catalog payload must contain schema, generated, and apps')
    if type(value['schema']) is not int or value['schema'] != 1:
        raise ValueError('Unsupported catalog schema')
    if not isinstance(value['generated'], str) or not value['generated'] or len(value['generated']) > 40:
        raise ValueError('Catalog generated must be an ISO-8601 timestamp')
    if not isinstance(value['apps'], list) or len(value['apps']) > 128:
        raise ValueError('Catalog apps must be a list of at most 128 entries')
    seen = set()
    for app in value['apps']:
        required = {'id', 'name', 'version', 'min_os', 'abi', 'package_url', 'sha256', 'size', 'package_key_id'}
        if not isinstance(app, dict) or not required <= set(app) or set(app) - (required | {'description'}):
            raise ValueError('Catalog app has missing or unknown fields')
        if not isinstance(app['id'], str) or not ID.fullmatch(app['id']) or len(app['id']) > 63:
            raise ValueError('Catalog app ID must be reverse-DNS')
        if app['id'] in seen:
            raise ValueError(f"Duplicate catalog app ID: {app['id']}")
        seen.add(app['id'])
        if not isinstance(app['name'], str) or not app['name'] or len(app['name'].encode()) > 63:
            raise ValueError(f"Invalid catalog app name: {app['id']}")
        for field in ('version', 'min_os'):
            if not isinstance(app[field], str) or not SEMVER.fullmatch(app[field]):
                raise ValueError(f"Invalid {field} for catalog app {app['id']}")
        if type(app['abi']) is not int or app['abi'] != 1:
            raise ValueError(f"Unsupported ABI for catalog app {app['id']}")
        if type(app['package_key_id']) is not int or not 1 <= app['package_key_id'] <= 255:
            raise ValueError(f"Invalid package key ID for catalog app {app['id']}")
        if type(app['size']) is not int or not 67 <= app['size'] <= MAX_FILE_BYTES + 66:
            raise ValueError(f"Invalid package size for catalog app {app['id']}")
        if not isinstance(app['sha256'], str) or not re.fullmatch(r'[0-9a-f]{64}', app['sha256']):
            raise ValueError(f"Invalid SHA-256 for catalog app {app['id']}")
        if not isinstance(app['package_url'], str):
            raise ValueError(f"Invalid package URL for catalog app {app['id']}")
        parsed = urllib.parse.urlparse(app['package_url'])
        if parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password:
            raise ValueError(f"Catalog package URL must use HTTPS: {app['id']}")
        if 'description' in app and (not isinstance(app['description'], str) or len(app['description'].encode()) > 512):
            raise ValueError(f"Invalid description for catalog app {app['id']}")
    return value


def sign_catalog(source, destination, private_key_path, key_id):
    if type(key_id) is not int or not 1 <= key_id <= 255:
        raise ValueError('Catalog signing key ID must be 1..255')
    try:
        from cryptography.hazmat.primitives.serialization import load_pem_private_key
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    except ImportError as exc:
        raise ValueError('Catalog signing requires cryptography: pip install -r tools/requirements-pack.txt') from exc
    payload = json.loads(source.read_text(encoding='utf-8'), object_pairs_hook=no_duplicates)
    validate_catalog(payload)
    key = load_pem_private_key(private_key_path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError('Catalog signing key must be Ed25519')
    envelope = {'catalog': payload, 'key_id': key_id,
                'signature': base64.b64encode(key.sign(canonical_json(payload))).decode('ascii')}
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(canonical_json(envelope) + b'\n')


def verify_catalog_bytes(data, trusted_keys):
    try:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives.serialization import load_pem_public_key
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    except ImportError as exc:
        raise ValueError('Catalog verification requires cryptography: pip install -r tools/requirements-pack.txt') from exc
    try:
        envelope = json.loads(data.decode('utf-8'), object_pairs_hook=no_duplicates)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError('Catalog is not valid UTF-8 JSON') from exc
    if not isinstance(envelope, dict) or set(envelope) != {'catalog', 'key_id', 'signature'}:
        raise ValueError('Catalog signature envelope is malformed')
    payload = validate_catalog(envelope['catalog'])
    key_id = envelope['key_id']
    if type(key_id) is not int or not 1 <= key_id <= 255:
        raise ValueError('Catalog key_id must be 1..255')
    if key_id not in trusted_keys:
        raise ValueError(f'No trusted public key configured for catalog key ID {key_id}')
    try:
        signature = base64.b64decode(envelope['signature'], validate=True)
    except (ValueError, TypeError) as exc:
        raise ValueError('Catalog signature is not valid base64') from exc
    if len(signature) != 64:
        raise ValueError('Catalog signature must be 64 bytes')
    key = load_pem_public_key(trusted_keys[key_id].read_bytes())
    if not isinstance(key, Ed25519PublicKey):
        raise ValueError('Catalog verification key must be Ed25519')
    try:
        key.verify(signature, canonical_json(payload))
    except InvalidSignature as exc:
        raise ValueError('Catalog signature is invalid') from exc
    return payload, key_id


class HttpsRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        if urllib.parse.urlparse(new_url).scheme != 'https':
            raise ValueError('Refusing HTTPS catalog redirect to a non-HTTPS URL')
        return super().redirect_request(request, fp, code, message, headers, new_url)


def fetch_https(url, maximum):
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password:
        raise ValueError('Catalog and package URLs must use HTTPS')
    opener = urllib.request.build_opener(HttpsRedirectHandler())
    request = urllib.request.Request(url, headers={'User-Agent': 'Crystal-CLI/1'})
    try:
        with opener.open(request, timeout=20) as response:
            if response.status != 200:
                raise ValueError(f'HTTPS request returned {response.status}')
            body = response.read(maximum + 1)
    except (urllib.error.URLError, TimeoutError) as exc:
        raise ValueError(f'HTTPS download failed: {exc}') from exc
    if len(body) > maximum:
        raise ValueError('HTTPS response exceeds the permitted size')
    return body


def catalog_list(url, trusted_keys):
    payload, key_id = verify_catalog_bytes(fetch_https(url, 256 * 1024), trusted_keys)
    return payload, key_id


def catalog_download(url, app_id, destination, trusted_catalog_keys, trusted_package_keys):
    payload, catalog_key_id = catalog_list(url, trusted_catalog_keys)
    app = next((row for row in payload['apps'] if row['id'] == app_id), None)
    if app is None:
        raise ValueError(f'App is not listed in the signed catalog: {app_id}')
    package = fetch_https(app['package_url'], MAX_FILE_BYTES + 66)
    if len(package) != app['size'] or hashlib.sha256(package).hexdigest() != app['sha256']:
        raise ValueError('Downloaded package size or SHA-256 does not match the signed catalog')
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(package)
    try:
        result = verify(destination, trusted_package_keys)
        if result['key_id'] != app['package_key_id']:
            raise ValueError('Downloaded package signing key ID does not match the catalog')
        if result['manifest']['id'] != app['id'] or result['manifest']['version'] != app['version']:
            raise ValueError('Downloaded package identity/version does not match the catalog')
    except Exception:
        destination.unlink(missing_ok=True)
        raise
    return result, catalog_key_id


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    cmd = sub.add_parser('pack')
    cmd.add_argument('folder', type=Path)
    cmd.add_argument('-o', '--output', type=Path)
    signing = cmd.add_mutually_exclusive_group(required=True)
    signing.add_argument('--unsigned', action='store_true', help='explicitly create a Developer Mode package')
    signing.add_argument('--key', '--sign', dest='key', type=Path,
                         help='sign with an Ed25519 PEM private key')
    cmd.add_argument('--key-id', type=int, default=1, choices=range(1, 256), metavar='1..255')
    cmd = sub.add_parser('inspect'); cmd.add_argument('package', type=Path)
    cmd.add_argument('--trusted-key', action='append', type=parse_trusted_key, default=[],
                     metavar='KEY_ID=PUBLIC_KEY.pem', help='verify signatures for this trusted key ID')
    cmd = sub.add_parser('verify', help='verify a signed package against configured public keys')
    cmd.add_argument('package', type=Path)
    cmd.add_argument('--trusted-key', action='append', type=parse_trusted_key, required=True,
                     metavar='KEY_ID=PUBLIC_KEY.pem')
    cmd = sub.add_parser('catalog-sign', help='sign a curated catalog JSON for static HTTPS hosting')
    cmd.add_argument('source', type=Path, help='unsigned catalog payload JSON')
    cmd.add_argument('-o', '--output', type=Path, required=True)
    cmd.add_argument('--key', type=Path, required=True, help='Ed25519 PEM private key (keep offline)')
    cmd.add_argument('--key-id', type=int, required=True, choices=range(1, 256), metavar='1..255')
    cmd = sub.add_parser('catalog', help='fetch and verify a signed online app catalog')
    cmd.add_argument('url', help='HTTPS URL of the signed catalog JSON')
    cmd.add_argument('--trusted-key', action='append', type=parse_trusted_key, required=True,
                     metavar='KEY_ID=PUBLIC_KEY.pem')
    cmd = sub.add_parser('catalog-download', help='download and verify one catalog package')
    cmd.add_argument('url', help='HTTPS URL of the signed catalog JSON')
    cmd.add_argument('app_id')
    cmd.add_argument('-o', '--output', type=Path, required=True)
    cmd.add_argument('--trusted-catalog-key', action='append', type=parse_trusted_key, required=True,
                     metavar='KEY_ID=PUBLIC_KEY.pem')
    cmd.add_argument('--trusted-package-key', action='append', type=parse_trusted_key, required=True,
                     metavar='KEY_ID=PUBLIC_KEY.pem')
    args = parser.parse_args()
    try:
        if args.command in ('inspect', 'verify'):
            keys = dict(args.trusted_key)
            if len(keys) != len(args.trusted_key):
                raise ValueError('Duplicate trusted key ID')
            result = verify(args.package, keys) if args.command == 'verify' else inspect(args.package)
            if args.command == 'inspect' and result['key_id'] is not None and result['key_id'] in keys:
                result = verify(args.package, keys)
            result.pop('signed_length', None)
            print(json.dumps(result, indent=2))
        elif args.command == 'catalog-sign':
            sign_catalog(args.source, args.output, args.key, args.key_id)
            print(args.output)
        elif args.command == 'catalog':
            keys = dict(args.trusted_key)
            if len(keys) != len(args.trusted_key):
                raise ValueError('Duplicate trusted key ID')
            payload, key_id = catalog_list(args.url, keys)
            print(json.dumps({'catalog_key_id': key_id, **payload}, indent=2))
        elif args.command == 'catalog-download':
            catalog_keys = dict(args.trusted_catalog_key)
            package_keys = dict(args.trusted_package_key)
            if len(catalog_keys) != len(args.trusted_catalog_key) or len(package_keys) != len(args.trusted_package_key):
                raise ValueError('Duplicate trusted key ID')
            result, catalog_key_id = catalog_download(args.url, args.app_id, args.output,
                                                      catalog_keys, package_keys)
            print(json.dumps({'catalog_key_id': catalog_key_id, 'package': str(args.output),
                              'signature': result['signature'], 'manifest': result['manifest']}, indent=2))
        else:
            m = load_manifest(args.folder / 'app.yaml')
            output = args.output or Path(f"{m['id']}-{m['version']}.capp")
            pack(args.folder, output, args.key, args.key_id)
            print(output)
    except (ValueError, OSError) as exc:
        parser.exit(1, f'crystal: {exc}\n')


if __name__ == '__main__':
    main()
