#!/usr/bin/env bash
set -euo pipefail

# Incremental CARLA UE5 development rebuild.
#
# The normal package target rebuilds the Python wheel and editor, cooks assets,
# and copies ~15k files. Most sensor/source changes only require relinking the
# game executable. The CARLA plugin uses per-file compilation, so later builds
# compile only edited source files. This script updates existing development
# package trees without touching cooked content.
#
# Use the full CMake package target when Config/, Content/, .uproject, plugin
# manifests, reflected UPROPERTY/UFUNCTION declarations, or cook settings
# change.

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_dir="$(cd -- "${script_dir}/../.." && pwd)"
build_dir="${repo_dir}/Build"
engine_dir="${CARLA_UNREAL_ENGINE_PATH:-/mnt/data/maksym/carla/UnrealEngine5_carla}"
jobs="${CARLA_BUILD_JOBS:-8}"

if [[ ! -x "${engine_dir}/Engine/Build/BatchFiles/Linux/Build.sh" ]]; then
  echo "CARLA Unreal Engine not found at ${engine_dir}" >&2
  exit 2
fi

export CARLA_UNREAL_ENGINE_PATH="${engine_dir}"
(
  cd "${build_dir}/Unreal"
  "${engine_dir}/Engine/Build/BatchFiles/Linux/Build.sh" \
    CarlaUnreal Linux Development \
    "-project=${repo_dir}/Unreal/CarlaUnreal/CarlaUnreal.uproject" \
    -game "-MaxParallelActions=${jobs}"
)

source_binary="${repo_dir}/Unreal/CarlaUnreal/Binaries/Linux/CarlaUnreal"
package_roots=(
  "${build_dir}/Package/StagedBuilds/Carla-0.10.0-Linux-Development/Linux"
  "${build_dir}/Package/Carla-0.10.0-Linux-Development/Linux"
)

if [[ ! -x "${source_binary}" ]]; then
  echo "Built CARLA executable is missing: ${source_binary}" >&2
  exit 3
fi

for package_root in "${package_roots[@]}"; do
  packaged_binary="${package_root}/CarlaUnreal/Binaries/Linux/CarlaUnreal"
  if [[ -f "${packaged_binary}" ]]; then
    cp --preserve=mode,timestamps "${source_binary}" "${packaged_binary}"
  fi
done

sha256sum "${source_binary}"
