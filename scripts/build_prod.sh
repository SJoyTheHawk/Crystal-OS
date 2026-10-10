#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
KEY_PATH="${CRYSTAL_PROD_KEY:-${HOME}/.crystal_keys/prod_signing_key.pem}"

if [[ ! -f "${KEY_PATH}" ]]; then
    echo "ERROR: production signing key not found: ${KEY_PATH}" >&2
    echo "Generate a local test key with: espsecure generate-signing-key --version 2 --scheme rsa3072 ${KEY_PATH}" >&2
    echo "For production, provide it through secure secret management or CRYSTAL_PROD_KEY." >&2
    exit 1
fi

cd "${PROJECT_ROOT}"
echo "Building production profile with RSA signing key: ${KEY_PATH}"
if ! grep -q '^CONFIG_SECURE_SIGNED_APPS_RSA_SCHEME=y$' sdkconfig; then
    echo "ERROR: sdkconfig is not configured for RSA app signing." >&2
    echo "Enable RSA under menuconfig: Security features -> App Signing Scheme -> RSA." >&2
    exit 1
fi
ln -sfn "${KEY_PATH}" "${PROJECT_ROOT}/secure_boot_signing_key.pem"
idf.py build
echo "Production signed image: build/crystal_os.bin"
echo "Verify with: espsecure signature-info-v2 build/crystal_os.bin"
