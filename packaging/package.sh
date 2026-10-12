#!/usr/bin/env bash

set -euo pipefail

readonly NFPM_VERSION="2.47.0"
readonly ALL_FORMATS=(deb rpm archlinux)

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
readonly script_dir
root_dir=$(dirname -- "$script_dir")
readonly root_dir
readonly CONFIG="$script_dir/nfpm.yaml"

build_dir="$root_dir/build/packaging"
output_dir="$root_dir/build"
formats=()
arches=()
allow_dirty=0
clean=0
nfpm_bin="${NFPM:-}"

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

info() {
    printf '==> %s\n' "$*"
}

usage() {
    cat <<EOF
Usage: packaging/package.sh [options] [format...]

Builds the release binary for every requested architecture, packages it with
nfpm and writes the bare executable. Formats default to ${ALL_FORMATS[*]}.

Options:
  --output DIR      where the artifacts are written (default: build/)
  --build-dir DIR   build tree root (default: build/packaging); each
                    architecture builds in DIR/<arch>
  --clean           remove the build directory of every architecture and build
                    it from scratch (default: reuse it)
  --arch ARCH       architecture to build, comma separated or repeated
                    (default: host). A foreign architecture takes its toolchain
                    from packaging/toolchains/<arch>.cmake, generated from the
                    GNU triple when that file is missing
  --allow-dirty     package a working tree with uncommitted changes
  -h, --help        show this help

Environment:
  NFPM              path to an nfpm binary; when unset, nfpm is taken from
                    PATH, and downloaded to \${XDG_CACHE_HOME:-\$HOME/.cache}/riipl-nfpm
                    as a last resort.
EOF
}

fetch() {
    local url=$1 out=$2 delay=2 attempt
    for attempt in 1 2 3; do
        if curl -fsSL -o "$out" "$url"; then
            return 0
        fi
        if ((attempt < 3)); then
            printf 'warning: download of %s failed, retrying in %ss\n' "$url" "$delay" >&2
            sleep "$delay"
            delay=$((delay * 2))
        fi
    done
    die "failed to download $url"
}

resolve_nfpm() {
    if [[ -n $nfpm_bin ]]; then
        [[ -x $nfpm_bin ]] || die "NFPM=$nfpm_bin is not an executable file"
        return 0
    fi
    if nfpm_bin=$(command -v nfpm); then
        info "using nfpm from PATH: $nfpm_bin"
        return 0
    fi

    local cache="${XDG_CACHE_HOME:-$HOME/.cache}/riipl-nfpm"
    local nfpm_file="$cache/nfpm-$NFPM_VERSION"
    if [[ ! -x $nfpm_file ]]; then
        info "nfpm is not on PATH, fetching $NFPM_VERSION into $cache"
        download_nfpm "$cache" "$nfpm_file"
    fi
    nfpm_bin="$nfpm_file"
}

download_nfpm() {
    local cache=$1 nfpm_file=$2 asset arch base
    case $(uname -m) in
    x86_64) arch=x86_64 ;;
    aarch64) arch=arm64 ;;
    ppc64le) arch=ppc64le ;;
    s390x) arch=s390x ;;
    *) die "no prebuilt nfpm for $(uname -m); install nfpm and point NFPM at it" ;;
    esac

    asset="nfpm_${NFPM_VERSION}_Linux_${arch}.tar.gz"
    base="https://github.com/goreleaser/nfpm/releases/download/v${NFPM_VERSION}"
    mkdir -p "$cache"
    fetch "$base/$asset" "$cache/$asset"
    fetch "$base/checksums.txt" "$cache/checksums.txt"
    (cd "$cache" && awk -v asset="$asset" '$2 == asset' checksums.txt | sha256sum -c -) ||
        die "checksum verification failed for $asset"
    tar -xzf "$cache/$asset" -C "$cache" nfpm
    mv -f "$cache/nfpm" "$nfpm_file"
    chmod 0755 "$nfpm_file"
}

# Prints `git describe` verbatim, which is the string the build bakes into the
# executable and `riipl --version` reports.
describe_version() {
    local describe

    describe=$(git -C "$root_dir" describe --tags --dirty 2>/dev/null) ||
        die "git describe found no tag; version the repository before packaging"

    if [[ $describe == *-dirty ]]; then
        ((allow_dirty)) ||
            die "the working tree is dirty; commit the changes or pass --allow-dirty"
        printf 'warning: packaging a dirty working tree\n' >&2
    fi

    printf '%s\n' "${describe#v}"
}

# Prints the version to hand to the given packager. RPM forbids `-` in Version,
# and Arch expects the `r<count>.<sha>` revision its VCS packages use.
version_for() {
    local version

    case $1 in
    deb) printf '%s\n' "$package_version" ;;
    rpm) printf '%s\n' "${package_version//-/.}" ;;
    archlinux)
        version=$version_tag
        if ((version_commits)); then
            version="$version.r$version_commits.$version_sha"
        fi
        if [[ -n $version_dirty ]]; then
            version="$version.dirty"
        fi
        printf '%s\n' "$version"
        ;;
    esac
}

# Prints the libc the given executable links, read from its ELF interpreter.
detect_libc() {
    local interpreter
    interpreter=$(readelf -lW "$1" 2>/dev/null | sed -n 's/.*interpreter: \(.*\)\]/\1/p')

    case $interpreter in
    *musl*) printf 'musl\n' ;;
    *ld-linux* | *ld64*) printf 'glibc\n' ;;
    *) printf 'static\n' ;;
    esac
}

# Prints the GNU triple of an architecture, failing when it has no built-in one.
triple_for() {
    case $1 in
    amd64) printf 'x86_64-linux-gnu\n' ;;
    arm64) printf 'aarch64-linux-gnu\n' ;;
    arm7) printf 'arm-linux-gnueabihf\n' ;;
    arm6) printf 'arm-linux-gnueabi\n' ;;
    386) printf 'i686-linux-gnu\n' ;;
    riscv64) printf 'riscv64-linux-gnu\n' ;;
    loong64) printf 'loongarch64-linux-gnu\n' ;;
    *) return 1 ;;
    esac
}

# Installs the GNU cross toolchain of a triple when it is missing.
install_cross_toolchain() {
    local triple=$1
    command -v "$triple-g++" >/dev/null && return 0

    info "installing the $triple cross toolchain" >&2
    if command -v apt-get >/dev/null; then
        sudo apt-get install -y "g++-$triple" >&2
    elif command -v pacman >/dev/null; then
        sudo pacman -S --asdeps --noconfirm "$triple-gcc" >&2
    elif command -v dnf >/dev/null; then
        sudo dnf install -y "gcc-c++-$triple" >&2
    else
        die "install the $triple C++ cross compiler and retry"
    fi
    command -v "$triple-g++" >/dev/null || die "$triple-g++ is still missing"
}

# Prints the CMake toolchain file to configure an architecture with. The host
# architecture needs none.
toolchain_for() {
    local arch=$1 triple
    local file="$script_dir/toolchains/$arch.cmake"

    [[ $arch == "$host_arch" ]] && return 0
    if [[ -f $file ]]; then
        printf '%s\n' "$file"
        return 0
    fi

    triple=$(triple_for "$arch") ||
        die "no toolchain for $arch; add $script_dir/toolchains/$arch.cmake"
    install_cross_toolchain "$triple"

    file="$build_dir/$arch/toolchain.cmake"
    mkdir -p "$(dirname -- "$file")"
    cat >"$file" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $arch)
set(CMAKE_C_COMPILER $triple-gcc)
set(CMAKE_CXX_COMPILER $triple-g++)
set(CMAKE_FIND_ROOT_PATH /usr/$triple)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
    printf '%s\n' "$file"
}

detect_arch() {
    case $(uname -m) in
    x86_64) printf 'amd64\n' ;;
    aarch64 | arm64) printf 'arm64\n' ;;
    armv7l) printf 'arm7\n' ;;
    armv6l) printf 'arm6\n' ;;
    i386 | i486 | i586 | i686) printf '386\n' ;;
    riscv64) printf 'riscv64\n' ;;
    loongarch64) printf 'loong64\n' ;;
    *) die "unsupported machine architecture $(uname -m); pass --arch" ;;
    esac
}

build_app() {
    local dir=$1 toolchain=$2
    local -a configure=(-S "$root_dir" -B "$dir" -DCMAKE_BUILD_TYPE=Release)

    if ((clean)); then
        [[ $dir == "$root_dir"/* ]] || die "refusing to remove $dir, which is outside $root_dir"
        info "removing $dir"
        rm -rf -- "$dir"
    fi

    if [[ -n $toolchain ]]; then
        configure+=(-DCMAKE_TOOLCHAIN_FILE="$toolchain")
    fi

    info "configuring $dir"
    cmake "${configure[@]}"
    info "building $dir"
    cmake --build "$dir" -j"$(nproc)"
}

while (($#)); do
    case $1 in
    --output)
        output_dir=${2:?--output needs a directory}
        shift 2
        ;;
    --build-dir)
        build_dir=${2:?--build-dir needs a directory}
        shift 2
        ;;
    --clean)
        clean=1
        shift
        ;;
    --arch)
        IFS=, read -r -a requested <<<"${2:?--arch needs an architecture}"
        arches+=("${requested[@]}")
        shift 2
        ;;
    --allow-dirty)
        allow_dirty=1
        shift
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    -*)
        die "unknown option $1"
        ;;
    *)
        formats+=("$1")
        shift
        ;;
    esac
done

if ((${#formats[@]} == 0)); then
    formats=("${ALL_FORMATS[@]}")
fi
for format in "${formats[@]}"; do
    [[ " ${ALL_FORMATS[*]} " == *" $format "* ]] || die "unknown package format $format"
done

[[ -f $CONFIG ]] || die "missing $CONFIG"

package_version=$(describe_version)
[[ $package_version =~ ^([0-9]+(\.[0-9]+)*)(-([0-9]+)-(g[0-9a-f]+))?(-dirty)?$ ]] ||
    die "git describe returned '$package_version', which carries no numeric version"
version_tag=${BASH_REMATCH[1]}
version_commits=${BASH_REMATCH[4]:-0}
version_sha=${BASH_REMATCH[5]:-}
version_dirty=${BASH_REMATCH[6]:-}

host_arch=$(detect_arch)
if ((${#arches[@]} == 0)); then
    arches=("$host_arch")
fi

resolve_nfpm

mkdir -p "$output_dir"
output_dir=$(cd -- "$output_dir" && pwd)

export RIIPL_DESKTOP="$root_dir/resources/riipl.desktop"
export RIIPL_ICON="$root_dir/resources/icons/app.svg"
export RIIPL_LICENSE="$root_dir/LICENSE"

info "packaging riipl $package_version for ${arches[*]}"
for arch in "${arches[@]}"; do
    arch_dir="$build_dir/$arch"
    toolchain=$(toolchain_for "$arch")
    build_app "$arch_dir" "$toolchain"

    binary="$arch_dir/riipl"
    export PKG_ARCH="$arch"
    export RIIPL_BIN="$binary"
    for format in "${formats[@]}"; do
        PKG_VERSION=$(version_for "$format")
        export PKG_VERSION
        "$nfpm_bin" package -f "$CONFIG" -p "$format" -t "$output_dir"
    done

    bare_name="riipl_${package_version}_linux_${arch}_$(detect_libc "$binary")"
    info "writing the bare executable as $bare_name"
    install -m 0755 "$binary" "$output_dir/$bare_name"
done

info "artifacts in $output_dir"
ls -lh "$output_dir"
