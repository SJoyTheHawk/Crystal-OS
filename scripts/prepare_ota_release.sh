#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT_ROOT="${PROJECT_ROOT}/ota_release"
BUILD_IMAGE="${PROJECT_ROOT}/build/crystal_os.bin"
RUN_BUILD=0

usage() {
    echo "Usage: $0 [--build] [output-directory]"
    echo "  --build       run scripts/build_dev.sh before packaging"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build) RUN_BUILD=1; shift ;;
        -h|--help) usage; exit 0 ;;
        -*) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
        *) OUTPUT_ROOT="$1"; shift ;;
    esac
done

cd "${PROJECT_ROOT}"
if [[ "${RUN_BUILD}" -eq 1 ]]; then
    "${SCRIPT_DIR}/build_dev.sh"
fi

if [[ ! -f "${BUILD_IMAGE}" ]]; then
    echo "ERROR: signed image not found: ${BUILD_IMAGE}" >&2
    echo "Build it first or pass --build." >&2
    exit 1
fi

VERSION="$(sed -nE 's/^[[:space:]]*project\([^ ]+[[:space:]]+VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+)\).*/\1/p' CMakeLists.txt | head -n 1)"
if [[ -z "${VERSION}" ]]; then
    echo "ERROR: could not read MAJOR.MINOR.PATCH from CMakeLists.txt" >&2
    exit 1
fi

if command -v shasum >/dev/null 2>&1; then
    SHA256="$(shasum -a 256 "${BUILD_IMAGE}" | awk '{print $1}')"
else
    SHA256="$(openssl dgst -sha256 -r "${BUILD_IMAGE}" | awk '{print $1}')"
fi
SIZE="$(stat -f%z "${BUILD_IMAGE}")"

ROCKY_ROOT="${OUTPUT_ROOT}/rockyroad.studio/apps/crystal-os"
MIRROR_ROOT="${OUTPUT_ROOT}/updates.bubstal.com/crystal-os"
rm -rf "${ROCKY_ROOT}" "${MIRROR_ROOT}"
mkdir -p "${ROCKY_ROOT}/stable" "${ROCKY_ROOT}/releases/${VERSION}"
mkdir -p "${MIRROR_ROOT}/stable" "${MIRROR_ROOT}/releases/${VERSION}"

cp "${BUILD_IMAGE}" "${ROCKY_ROOT}/releases/${VERSION}/crystal_os.bin"
cp "${BUILD_IMAGE}" "${MIRROR_ROOT}/releases/${VERSION}/crystal_os.bin"

cat > "${ROCKY_ROOT}/stable/manifest.json" <<EOF_MANIFEST
{
  "schema": 1,
  "target": "waveshare-esp32-s3-touch-lcd-4b",
  "channel": "stable",
  "version": "${VERSION}",
  "size": ${SIZE},
  "sha256": "${SHA256}",
  "urls": [
    "https://rockyroad.studio/apps/crystal-os/releases/${VERSION}/crystal_os.bin",
    "https://updates.bubstal.com/crystal-os/releases/${VERSION}/crystal_os.bin"
  ],
  "notes": "Crystal OS ${VERSION}"
}
EOF_MANIFEST
cp "${ROCKY_ROOT}/stable/manifest.json" "${MIRROR_ROOT}/stable/manifest.json"

printf 'OTA release prepared: %s\n' "${OUTPUT_ROOT}"
printf 'Version: %s\nSize: %s bytes\nSHA-256: %s\n' "${VERSION}" "${SIZE}" "${SHA256}"
printf '\nUpload %s/rockyroad.studio/* to the rockyroad.studio document root.\n' "${OUTPUT_ROOT}"
printf 'Upload %s/updates.bubstal.com/* to the updates.bubstal.com document root when it is available.\n' "${OUTPUT_ROOT}"
