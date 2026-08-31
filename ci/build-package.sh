#!/usr/bin/env bash
set -Eeuo pipefail

readonly SOURCE_COMMIT="14ffa6fdaa53f7b51512be2b3d24f3939695403c"
readonly SOURCE_TAG="v2026.516.143833"
readonly INSTALL_PREFIX="/userdata/system/add-ons/sunshine"
readonly PACKAGE_NAME="sunshine-batocera-arm64"

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
readonly REPO_ROOT
readonly SOURCE_DIR="${REPO_ROOT}/source"
readonly BUILD_DIR="${REPO_ROOT}/build"
readonly STAGE_DIR="${REPO_ROOT}/stage"
readonly ARTIFACTS_DIR="${REPO_ROOT}/artifacts"
readonly PACKAGE_DIR="${STAGE_DIR}/${PACKAGE_NAME}"
readonly PISP_PREFIX="${PISP_PREFIX:-${REPO_ROOT}/pisp-prefix}"

if [[ "$(uname -m)" != "aarch64" ]]; then
  echo "ERROR: native aarch64 runner required; found $(uname -m)" >&2
  exit 1
fi

actual_commit="$(git -C "${SOURCE_DIR}" rev-parse HEAD)"
if [[ "${actual_commit}" != "${SOURCE_COMMIT}" ]]; then
  echo "ERROR: source commit ${actual_commit}, expected ${SOURCE_COMMIT}" >&2
  exit 1
fi

pisp_pc="$(find "${PISP_PREFIX}" -type f -name 'libpisp.pc' -print -quit)"
if [[ -z "${pisp_pc}" ]]; then
  echo "ERROR: pinned libpisp installation not found under ${PISP_PREFIX}" >&2
  exit 1
fi
pisp_pkgdir="$(dirname -- "${pisp_pc}")"
export PKG_CONFIG_PATH="${pisp_pkgdir}${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
pisp_cflags="$(pkg-config --cflags libpisp)"
pisp_libs="$(pkg-config --libs --static libpisp)"
printf '%s\n' "PISP_PREFIX=${PISP_PREFIX}" "PISP_CFLAGS=${pisp_cflags}" "PISP_LIBS=${pisp_libs}"

for generated_dir in "${BUILD_DIR}" "${STAGE_DIR}" "${ARTIFACTS_DIR}"; do
  if [[ -e "${generated_dir}" ]]; then
    echo "ERROR: refusing to overwrite existing path: ${generated_dir}" >&2
    exit 1
  fi
done
mkdir -p -- "${BUILD_DIR}" "${PACKAGE_DIR}/bin" "${PACKAGE_DIR}/lib" "${ARTIFACTS_DIR}"

cmake_args=(
  -S "${SOURCE_DIR}"
  -B "${BUILD_DIR}"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}"
  -DCMAKE_CXX_FLAGS="${pisp_cflags}"
  -DCMAKE_EXE_LINKER_FLAGS="${pisp_libs}"
  -DSUNSHINE_ASSETS_DIR=share/sunshine
  -DSUNSHINE_EXECUTABLE_PATH="${INSTALL_PREFIX}/bin/sunshine"
  -DBUILD_DOCS=OFF
  -DBUILD_TESTS=OFF
  -DBUILD_WERROR=ON
  -DSUNSHINE_ENABLE_TRAY=OFF
  -DSUNSHINE_ENABLE_CUDA=OFF
  -DSUNSHINE_ENABLE_VULKAN=OFF
  -DSUNSHINE_ENABLE_X11=OFF
  -DSUNSHINE_ENABLE_KWIN=OFF
  -DSUNSHINE_ENABLE_PORTAL=OFF
  -DSUNSHINE_ENABLE_WAYLAND=ON
  -DSUNSHINE_ENABLE_DRM=ON
  -DSUNSHINE_ENABLE_VAAPI=ON
)

printf '%s\n' "SOURCE_TAG=${SOURCE_TAG}" "SOURCE_COMMIT=${actual_commit}"
printf 'CMAKE_ARG=%q\n' "${cmake_args[@]}"
BRANCH=main \
BUILD_VERSION="${SOURCE_TAG}" \
CLONE_URL=https://github.com/LizardByte/Sunshine.git \
COMMIT="${SOURCE_COMMIT}" \
TAG="${SOURCE_TAG}" \
  cmake "${cmake_args[@]}"
cmake --build "${BUILD_DIR}" --parallel "$(nproc)"

DESTDIR="${STAGE_DIR}/install-root" cmake --install "${BUILD_DIR}"
install_root="${STAGE_DIR}/install-root${INSTALL_PREFIX}"
if [[ ! -x "${install_root}/bin/sunshine" ]]; then
  echo "ERROR: staged Sunshine executable not found" >&2
  exit 1
fi

cp -a -- "${install_root}/bin/sunshine" "${PACKAGE_DIR}/bin/sunshine"
cp -a -- "${install_root}/share" "${PACKAGE_DIR}/share"

# PiSP is linked statically. Ship the exact backend defaults used by libpisp
# so runtime behaviour is independent of the Batocera root filesystem.
install -Dm0644 \
  "${REPO_ROOT}/libpisp/src/libpisp/backend/backend_default_config.json" \
  "${PACKAGE_DIR}/share/libpisp/backend/backend_default_config.json"

# Batocera was verified to miss Ubuntu's ICU 74 ABI. Bundle only the ICU 74
# closure actually referenced by Sunshine; never manufacture cross-ABI links.
declare -A seen=()
queue=()
while IFS= read -r soname; do
  [[ "${soname}" =~ ^libicu(uc|data|i18n)\.so\.74$ ]] && queue+=("${soname}")
done < <(readelf -d "${PACKAGE_DIR}/bin/sunshine" | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')

while ((${#queue[@]})); do
  soname="${queue[0]}"
  queue=("${queue[@]:1}")
  [[ -n "${seen[${soname}]:-}" ]] && continue
  seen["${soname}"]=1

  library_path="$(ldconfig -p | awk -v wanted="${soname}" '$1 == wanted { print $NF; exit }')"
  if [[ -z "${library_path}" || ! -f "${library_path}" ]]; then
    echo "ERROR: required bundle library not found: ${soname}" >&2
    exit 1
  fi
  cp -L --preserve=mode,timestamps -- "${library_path}" "${PACKAGE_DIR}/lib/${soname}"

  while IFS= read -r dependency; do
    if [[ "${dependency}" =~ ^libicu(uc|data|i18n)\.so\.74$ ]]; then
      queue+=("${dependency}")
    fi
  done < <(readelf -d "${library_path}" | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')
done

cat > "${PACKAGE_DIR}/bin/sunshine-start" <<'WRAPPER'
#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SUNSHINE_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)
export XDG_RUNTIME_DIR=/var/run
export WAYLAND_DISPLAY=wayland-0
export LD_LIBRARY_PATH="${SUNSHINE_ROOT}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export LIBPISP_BE_CONFIG_FILE="${SUNSHINE_ROOT}/share/libpisp/backend/backend_default_config.json"
export SUNSHINE_ASSETS="${SUNSHINE_ROOT}/share/sunshine"
export SUNSHINE_WEBROOT="${SUNSHINE_ROOT}/share/sunshine/web"
export SUNSHINE_APP_ICONS="${SUNSHINE_ROOT}/share/sunshine"
exec "${SUNSHINE_ROOT}/bin/sunshine" "$@"
WRAPPER
chmod 0755 "${PACKAGE_DIR}/bin/sunshine-start"

install -Dm0755 \
  "${REPO_ROOT}/batocera/sunshine" \
  "${PACKAGE_DIR}/share/batocera/sunshine"
install -Dm0755 \
  "${REPO_ROOT}/batocera/install-service.sh" \
  "${PACKAGE_DIR}/bin/install-batocera-service"

cat > "${PACKAGE_DIR}/README.txt" <<EOF
Sunshine ${SOURCE_TAG} for Batocera 43apu.1 / Raspberry Pi 5 aarch64
Source commit: ${SOURCE_COMMIT}
Install path: ${INSTALL_PREFIX}
Manual start: ${INSTALL_PREFIX}/bin/sunshine-start
Install Batocera service: ${INSTALL_PREFIX}/bin/install-batocera-service
Experimental converter: Raspberry Pi PiSP (RGB888 -> YUV420P), with libswscale fallback
libpisp commit: f8a5eb2af4c5dea76442785ef42b2fb1aa9e62f9

The packaged Batocera user service waits for Wayland and for all dynamic
Sunshine dependencies to resolve before starting. It defaults to
/userdata/system/.config/sunshine/sunshine.conf and can be overridden through
/userdata/system/configs/sunshine-service.conf.

Only the ICU 74 closure required by Sunshine is bundled. libpisp is linked
statically and its backend_default_config.json is bundled in share/libpisp.
Other dynamic libraries must be provided by the Batocera target.
EOF

(
  cd "${PACKAGE_DIR}"
  find . -type f ! -name SHA256SUMS -print0 \
    | LC_ALL=C sort -z \
    | xargs -0 sha256sum > "${STAGE_DIR}/SHA256SUMS.tmp"
)
mv -- "${STAGE_DIR}/SHA256SUMS.tmp" "${PACKAGE_DIR}/SHA256SUMS"

"${REPO_ROOT}/ci/audit-package.sh" \
  "${PACKAGE_DIR}" "${BUILD_DIR}" "${ARTIFACTS_DIR}/ci-report.txt"

tarball="${ARTIFACTS_DIR}/${PACKAGE_NAME}-${SOURCE_TAG}.tar.gz"
tar --sort=name \
  --mtime='UTC 2026-05-16 14:38:33' \
  --owner=0 --group=0 --numeric-owner \
  -C "${STAGE_DIR}" -czf "${tarball}" "${PACKAGE_NAME}"
(
  cd "${ARTIFACTS_DIR}"
  sha256sum "$(basename -- "${tarball}")" > "$(basename -- "${tarball}").sha256"
)

echo "Created ${tarball}"
