#!/usr/bin/env python3
"""Exercise Ed25519 signing and public-key trust for packages and catalogs."""
import hashlib
import json
from pathlib import Path
import tempfile
import datetime
import functools
import http.server
import ipaddress
import os
import ssl
import threading
from unittest.mock import patch

import crystal


def check_https_workflow(root, catalog, catalog_private_path, catalog_public_path, public_path):
    """Serve a signed index/package over real TLS with an ephemeral test CA."""
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography.x509.oid import NameOID
    private = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Crystal test server')])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(private.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(minutes=1))
            .not_valid_after(now + datetime.timedelta(days=1))
            .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]), critical=False)
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
            .sign(private, hashes.SHA256()))
    cert_path = root / 'tls.pem'; key_path = root / 'tls-key.pem'
    cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(private.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                             serialization.NoEncryption()))
    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *args): pass
    server = http.server.HTTPServer(('127.0.0.1', 0), functools.partial(Handler, directory=str(root)))
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); context.load_cert_chain(cert_path, key_path)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        url = f'https://127.0.0.1:{server.server_port}'
        payload = json.loads(json.dumps(catalog)); payload['apps'][0]['package_url'] = url + '/signed.capp'
        source = root / 'local-source.json'; index = root / 'local-catalog.json'
        source.write_text(json.dumps(payload)); crystal.sign_catalog(source, index, catalog_private_path, 7)
        try:
            crystal.catalog_list(url + '/local-catalog.json', {7: catalog_public_path})
            raise AssertionError('TLS accepted an untrusted server certificate')
        except ValueError as exc:
            assert 'CERTIFICATE_VERIFY_FAILED' in str(exc)
        with patch.dict(os.environ, {'SSL_CERT_FILE': str(cert_path)}):
            destination = root / 'https-downloaded.capp'
            result, key_id = crystal.catalog_download(url + '/local-catalog.json', payload['apps'][0]['id'],
                                                     destination, {7: catalog_public_path}, {23: public_path})
            assert key_id == 7 and result['signature'] == 'ed25519 (verified)'
            assert destination.read_bytes() == (root / 'signed.capp').read_bytes()
            # Publishing a new signed index changes the listing without firmware changes.
            payload['apps'][0]['description'] = 'Updated listing'
            source.write_text(json.dumps(payload)); crystal.sign_catalog(source, index, catalog_private_path, 7)
            updated, _ = crystal.catalog_list(url + '/local-catalog.json', {7: catalog_public_path})
            assert updated['apps'][0]['description'] == 'Updated listing'
    finally:
        server.shutdown(); server.server_close(); thread.join(timeout=2)
    print('PASS: real HTTPS certificate validation, signed catalog/package download and index update')


def main():
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    except ImportError as exc:
        raise SystemExit('Install tools/requirements-pack.txt before running signature tests') from exc

    manifest = {
        'abi': 1, 'id': 'org.crystal.signature-test', 'name': 'Signature Test',
        'version': '1.0.0', 'min_os': '1.0.0', 'runtime': 'native',
        'entry': 'app.native', 'bar_style': 'dark', 'memory_kb': 32,
        'storage_kb': 0, 'worker_threads': 0,
    }
    with tempfile.TemporaryDirectory(prefix='crystal-signatures-') as directory:
        root = Path(directory)
        source = root / 'source'
        source.mkdir()
        (source / 'app.yaml').write_text(json.dumps(manifest), encoding='utf-8')
        (source / 'app.native').write_bytes(b'crystal signed fixture\n')

        private = Ed25519PrivateKey.generate()
        catalog_private = Ed25519PrivateKey.generate()
        private_path = root / 'private.pem'
        public_path = root / 'public.pem'
        catalog_private_path = root / 'catalog-private.pem'
        catalog_public_path = root / 'catalog-public.pem'
        private_path.write_bytes(private.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption()))
        public_path.write_bytes(private.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))
        catalog_private_path.write_bytes(catalog_private.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
            serialization.NoEncryption()))
        catalog_public_path.write_bytes(catalog_private.public_key().public_bytes(
            serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))

        package = root / 'signed.capp'
        crystal.pack(source, package, private_path, key_id=23)
        result = crystal.verify(package, {23: public_path})
        assert result['signature'] == 'ed25519 (verified)'
        assert result['key_id'] == 23

        try:
            crystal.verify(package, {})
            raise AssertionError('Verification accepted an untrusted key ID')
        except ValueError as exc:
            assert 'No trusted public key' in str(exc)

        damaged = bytearray(package.read_bytes())
        damaged[-2] ^= 1
        tampered = root / 'tampered.capp'
        tampered.write_bytes(damaged)
        try:
            crystal.verify(tampered, {23: public_path})
            raise AssertionError('Verification accepted tampered package bytes')
        except ValueError as exc:
            assert 'signature' in str(exc).lower() or 'manifest' in str(exc).lower()

        catalog = {
            'schema': 1,
            'generated': '2026-11-11T00:00:00Z',
            'apps': [{
                'id': manifest['id'], 'name': manifest['name'], 'version': manifest['version'],
                'min_os': manifest['min_os'], 'abi': manifest['abi'],
                'package_key_id': 23,
                'package_url': 'https://apps.example.test/org.crystal.signature-test.capp',
                'sha256': hashlib.sha256(package.read_bytes()).hexdigest(),
                'size': package.stat().st_size,
                'description': 'A signed catalog fixture',
            }],
        }
        unsigned_catalog = root / 'catalog-source.json'
        signed_catalog = root / 'catalog.json'
        unsigned_catalog.write_text(json.dumps(catalog), encoding='utf-8')
        crystal.sign_catalog(unsigned_catalog, signed_catalog, catalog_private_path, key_id=7)
        checked, catalog_key_id = crystal.verify_catalog_bytes(signed_catalog.read_bytes(), {7: catalog_public_path})
        assert catalog_key_id == 7 and checked['apps'][0]['package_key_id'] == 23

        downloaded = root / 'downloaded.capp'
        with patch.object(crystal, 'fetch_https', side_effect=[signed_catalog.read_bytes(), package.read_bytes()]):
            result, used_catalog_key = crystal.catalog_download(
                'https://apps.example.test/catalog.json', manifest['id'], downloaded,
                {7: catalog_public_path}, {23: public_path})
        assert used_catalog_key == 7 and result['signature'] == 'ed25519 (verified)'
        assert downloaded.read_bytes() == package.read_bytes()

        bad_download = root / 'bad-download.capp'
        with patch.object(crystal, 'fetch_https', side_effect=[signed_catalog.read_bytes(), damaged]):
            try:
                crystal.catalog_download('https://apps.example.test/catalog.json', manifest['id'],
                                         bad_download, {7: catalog_public_path}, {23: public_path})
                raise AssertionError('Catalog download accepted package bytes with a mismatched hash')
            except ValueError as exc:
                assert 'SHA-256' in str(exc)
        assert not bad_download.exists()
        # Even a hash-matching file must pass package signature verification;
        # failure preserves an existing verified destination and removes staging.
        prior = downloaded.read_bytes()
        with patch.object(crystal, 'fetch_https', side_effect=[signed_catalog.read_bytes(), package.read_bytes()]):
            try:
                crystal.catalog_download('https://apps.example.test/catalog.json', manifest['id'],
                                         downloaded, {7: catalog_public_path}, {})
                raise AssertionError('Download accepted an untrusted package key')
            except ValueError:
                pass
        assert downloaded.read_bytes() == prior and not list(root.glob('.crystal-*'))

        try:
            crystal.verify_catalog_bytes(signed_catalog.read_bytes(), {23: public_path})
            raise AssertionError('Catalog verification accepted an untrusted key ID')
        except ValueError as exc:
            assert 'No trusted public key' in str(exc)

        changed_catalog = json.loads(signed_catalog.read_text(encoding='utf-8'))
        changed_catalog['catalog']['apps'][0]['name'] = 'Changed after signing'
        try:
            crystal.verify_catalog_bytes(crystal.canonical_json(changed_catalog), {7: catalog_public_path})
            raise AssertionError('Catalog verification accepted modified listing data')
        except ValueError as exc:
            assert 'signature is invalid' in str(exc)

        invalid_url_catalog = json.loads(json.dumps(catalog))
        invalid_url_catalog['apps'][0]['package_url'] = 'http://apps.example.test/app.capp'
        try:
            crystal.validate_catalog(invalid_url_catalog)
            raise AssertionError('Catalog validation accepted a non-HTTPS package URL')
        except ValueError as exc:
            assert 'must use HTTPS' in str(exc)
        check_https_workflow(root, catalog, catalog_private_path, catalog_public_path, public_path)

    print('PASS: Ed25519 package and catalog signing, trusted-key lookup, tamper rejection, and HTTPS policy')


if __name__ == '__main__':
    main()
