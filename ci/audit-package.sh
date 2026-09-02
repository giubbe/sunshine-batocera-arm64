#!/usr/bin/env bash
set -Eeuo pipefail

if (($# != 3)); then
  echo "usage: $0 PACKAGE_DIR BUILD_DIR REPORT" >&2
  exit 2
fi

readonly PACKAGE_DIR="$1"
readonly BUILD_DIR="$2"
readonly REPORT="$3"
readonly SUNSHINE="${PACKAGE_DIR}/bin/sunshine"
readonly WRAPPER="${PACKAGE_DIR}/bin/sunshine-start"
readonly SERVICE_INSTALLER="${PACKAGE_DIR}/bin/install-batocera-service"
readonly SERVICE_TEMPLATE="${PACKAGE_DIR}/share/batocera/sunshine"
readonly SOURCE_TAG="v2026.516.143833"
readonly SOURCE_COMMIT="14ffa6fdaa53f7b51512be2b3d24f3939695403c"
readonly FORBIDDEN_REGEX='(libgtk|libgdk|libayatana|libappindicator|libnotify|libX11|libxcb|libvulkan|libpipewire)'

exec > >(tee "${REPORT}") 2>&1

echo "== Build identity =="
echo "SOURCE_TAG=${SOURCE_TAG}"
echo "SOURCE_COMMIT=${SOURCE_COMMIT}"
echo "RUNNER_ARCH=$(uname -m)"
echo "INSTALL_PREFIX=/userdata/system/add-ons/sunshine"

echo "== Packaged launcher/service syntax =="
test -x "${WRAPPER}"
test -x "${SERVICE_INSTALLER}"
test -x "${SERVICE_TEMPLATE}"
sh -n "${WRAPPER}"
sh -n "${SERVICE_INSTALLER}"
bash -n "${SERVICE_TEMPLATE}"
grep -Fqx 'WAYLAND_SOCKET="/var/run/wayland-0"' "${SERVICE_TEMPLATE}"
grep -Fq 'runtime_dependencies_ready' "${SERVICE_TEMPLATE}"
grep -Fq 'batocera-services enable sunshine' "${SERVICE_INSTALLER}"

echo "== Effective CMake profile =="
grep -E '^(BUILD_DOCS|BUILD_TESTS|BUILD_WERROR|CMAKE_BUILD_TYPE|CMAKE_INSTALL_PREFIX|CMAKE_INTERPROCEDURAL_OPTIMIZATION|SUNSHINE_ASSETS_DIR|SUNSHINE_EXECUTABLE_PATH|SUNSHINE_ENABLE_(TRAY|CUDA|VULKAN|X11|KWIN|PORTAL|WAYLAND|DRM|VAAPI)):' \
  "${BUILD_DIR}/CMakeCache.txt" | LC_ALL=C sort
grep -Fqx 'CMAKE_BUILD_TYPE:STRING=Release' "${BUILD_DIR}/CMakeCache.txt"
grep -Fqx 'CMAKE_INTERPROCEDURAL_OPTIMIZATION:UNINITIALIZED=ON' "${BUILD_DIR}/CMakeCache.txt"
grep -F 'CMAKE_CXX_FLAGS_RELEASE:STRING=' "${BUILD_DIR}/CMakeCache.txt" | grep -F -- '-O3 -mcpu=cortex-a76 -mtune=cortex-a76'

echo "== Architecture =="
file "${SUNSHINE}"
mapfile -d '' elf_files < <(find "${PACKAGE_DIR}" -type f -print0 | xargs -0 file | awk -F: '$2 ~ /ELF/ { print $1 }' | tr '\n' '\0')
if ((${#elf_files[@]} == 0)); then
  echo "ERROR: no ELF files found" >&2
  exit 1
fi
for elf in "${elf_files[@]}"; do
  file "${elf}"
  readelf -h "${elf}" | grep -Eq 'Machine:[[:space:]]+AArch64' || {
    echo "ERROR: non-AArch64 ELF: ${elf}" >&2
    exit 1
  }
done

echo "== Embedded Sunshine identity =="
if ! strings "${SUNSHINE}" | grep -Fqx "${SOURCE_TAG#v}"; then
  echo "ERROR: expected Sunshine version is not embedded in the ELF" >&2
  exit 1
fi
if ! strings "${SUNSHINE}" | grep -Fqx "${SOURCE_COMMIT}"; then
  echo "ERROR: expected Sunshine commit is not embedded in the ELF" >&2
  exit 1
fi
echo "SUNSHINE_EMBEDDED_VERSION=${SOURCE_TAG#v}"
echo "SUNSHINE_EMBEDDED_COMMIT=${SOURCE_COMMIT}"

echo "== readelf -d bin/sunshine =="
readelf -d "${SUNSHINE}"

echo "== ELF DT_NEEDED inventory =="
for elf in "${elf_files[@]}"; do
  echo "--- ${elf#"${PACKAGE_DIR}"/}"
  readelf -d "${elf}" | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p' | LC_ALL=C sort
done

if readelf -d "${SUNSHINE}" | grep -Eiq "${FORBIDDEN_REGEX}"; then
  echo "ERROR: forbidden desktop/X11/Vulkan/PipeWire dependency found" >&2
  exit 1
fi

# This build deliberately keeps the pinned Sunshine upstream PulseAudio capture
# implementation. Upstream uses pa_simple_read(), so both libpulse and
# libpulse-simple are expected dependencies. The experimental pa_stream callback
# used during diagnostics is not applied to this build.
if ! readelf -d "${SUNSHINE}" | grep -Fq '[libpulse.so.0]'; then
  echo "ERROR: expected libpulse.so.0 dependency not found" >&2
  exit 1
fi
if ! readelf -d "${SUNSHINE}" | grep -Fq '[libpulse-simple.so.0]'; then
  echo "ERROR: expected upstream libpulse-simple.so.0 dependency not found" >&2
  exit 1
fi
if strings "${SUNSHINE}" | grep -Fq 'AUDIO_PROBE'; then
  echo "ERROR: diagnostic audio probe unexpectedly embedded" >&2
  exit 1
fi
if strings "${SUNSHINE}" | grep -Fq 'PIPELINE_TELEMETRY'; then
  echo "ERROR: diagnostic video telemetry unexpectedly embedded" >&2
  exit 1
fi
if ! strings "${SUNSHINE}" | grep -Fq 'PISP_CONVERTER enabled'; then
  echo "ERROR: PiSP converter marker not embedded" >&2
  exit 1
fi
if readelf -d "${SUNSHINE}" | grep -Fqi '[libpisp'; then
  echo "ERROR: libpisp must be linked statically, not recorded in DT_NEEDED" >&2
  exit 1
fi
echo "PISP_LINKAGE=STATIC"

echo "== Bundled runtime libraries =="
find "${PACKAGE_DIR}/lib" -maxdepth 1 -type f -printf '%f\n' | LC_ALL=C sort
if find "${PACKAGE_DIR}/lib" -maxdepth 1 -type f -printf '%f\n' \
    | grep -Evq '^libicu(uc|data|i18n)\.so\.74$'; then
  echo "ERROR: bundle contains a library outside the ICU 74 allowlist" >&2
  exit 1
fi

if grep -RIna --binary-files=text -E 'x86_64|x86-64|amd64' "${PACKAGE_DIR}"; then
  echo "ERROR: accidental x86_64 reference found" >&2
  exit 1
fi

echo "== Internal SHA256SUMS verification =="
(
  cd "${PACKAGE_DIR}"
  sha256sum --check SHA256SUMS
)

echo "== Ubuntu-only dependency resolution (not a Batocera equivalence test) =="
LD_LIBRARY_PATH="${PACKAGE_DIR}/lib" ldd "${SUNSHINE}"

echo "AUDIT_RESULT=PASS"
echo "BATOCERA_RUNTIME_VALIDATION=NOT_PERFORMED_IN_CI"
