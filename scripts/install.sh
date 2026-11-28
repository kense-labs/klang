#!/bin/sh
# Cross-platform installer for the K programming language (klang).
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/kense-labs/klang/main/scripts/install.sh | sh
#   sh install.sh [--version <tag>] [--profile <default|core|cloud>] [--dir <path>] [--no-modify-path]
set -eu

REPO="kense-labs/klang"
API_BASE="https://api.github.com/repos/$REPO"
DEFAULT_INSTALL_DIR="${HOME%/}/.klang/current"

VERSION="${KLANG_VERSION:-}"
PROFILE="${KLANG_PROFILE:-default}"
INSTALL_DIR="${KLANG_INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"
NO_MODIFY_PATH="${KLANG_NO_MODIFY_PATH:-0}"
PRERELEASE="${KLANG_PRERELEASE:-0}"
BASE_URL="${KLANG_BASE_URL:-}"
ARCH_OVERRIDE="${KLANG_ARCH:-}"
TMP_DIR=""

usage() {
    cat <<'EOF'
K programming language (klang) installer

Usage:
  install.sh [options]

Options:
  -v, --version <tag>       Install a specific release tag (e.g. v0.1.0-preview.1).
                            Defaults to the latest stable release, falling back to
                            the newest pre-release if no stable release exists yet.
  -p, --profile <name>      Select archive profile:
                              default  macOS arm64/x64 & Linux x64 (bundled packages),
                                       or Linux arm64 (cloud archive)
                              core     macOS arm64 core compiler/runtime (reproducible)
                              cloud    Linux x64/arm64 with Cordis inference gateway
  -d, --dir <path>          Installation directory (default: ~/.klang/current)
      --prerelease          Resolve the newest non-draft release (including pre-releases)
      --no-modify-path      Do not modify shell profile files
      --base-url <url>      Override download base URL (mirrors or offline archives)
  -h, --help                Show this help and exit

Environment overrides:
  KLANG_VERSION, KLANG_PROFILE, KLANG_INSTALL_DIR, KLANG_NO_MODIFY_PATH=1,
  KLANG_PRERELEASE=1, KLANG_BASE_URL, KLANG_ARCH, GITHUB_TOKEN
EOF
}

say() {
    printf '%s\n' "$*"
}

warn() {
    printf 'warning: %s\n' "$*" >&2
}

err() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

cleanup() {
    if [ -n "$TMP_DIR" ] && [ -d "$TMP_DIR" ]; then
        rm -rf "$TMP_DIR"
    fi
}
trap cleanup EXIT INT TERM

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || err "required command '$1' was not found on PATH"
}

parse_args() {
    while [ "$#" -gt 0 ]; do
        case "$1" in
            -v|--version)
                [ "$#" -ge 2 ] || err "$1 requires a value"
                VERSION="$2"
                shift 2
                ;;
            --version=*)
                VERSION="${1#--version=}"
                shift
                ;;
            -p|--profile)
                [ "$#" -ge 2 ] || err "$1 requires a value"
                PROFILE="$2"
                shift 2
                ;;
            --profile=*)
                PROFILE="${1#--profile=}"
                shift
                ;;
            -d|--dir)
                [ "$#" -ge 2 ] || err "$1 requires a value"
                INSTALL_DIR="$2"
                shift 2
                ;;
            --dir=*)
                INSTALL_DIR="${1#--dir=}"
                shift
                ;;
            --prerelease)
                PRERELEASE=1
                shift
                ;;
            --no-modify-path)
                NO_MODIFY_PATH=1
                shift
                ;;
            --base-url)
                [ "$#" -ge 2 ] || err "$1 requires a value"
                BASE_URL="$2"
                shift 2
                ;;
            --base-url=*)
                BASE_URL="${1#--base-url=}"
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                err "unknown option '$1' (run with --help for usage)"
                ;;
        esac
    done
}

fetch_url() {
    url="$1"
    dest="$2"
    is_api="${3:-0}"

    if command -v curl >/dev/null 2>&1; then
        case "$url" in
            https://*)
                set -- --proto '=https' --tlsv1.2 -fLsS
                ;;
            *)
                set -- -fLsS
                ;;
        esac
        if [ "$is_api" = 1 ]; then
            set -- "$@" -H "Accept: application/vnd.github+json" -H "User-Agent: klang-installer"
            if [ -n "${GITHUB_TOKEN:-}" ]; then
                set -- "$@" -H "Authorization: Bearer $GITHUB_TOKEN"
            fi
        fi
        curl "$@" -o "$dest" "$url"
    elif command -v wget >/dev/null 2>&1; then
        if [ "$is_api" = 1 ]; then
            if [ -n "${GITHUB_TOKEN:-}" ]; then
                wget -q --header="Accept: application/vnd.github+json" \
                     --header="User-Agent: klang-installer" \
                     --header="Authorization: Bearer $GITHUB_TOKEN" \
                     -O "$dest" "$url"
            else
                wget -q --header="Accept: application/vnd.github+json" \
                     --header="User-Agent: klang-installer" \
                     -O "$dest" "$url"
            fi
        else
            wget -q -O "$dest" "$url"
        fi
    else
        err "curl or wget is required to download releases"
    fi
}

extract_first_release_tag() {
    if command -v python3 >/dev/null 2>&1; then
        python3 -c '
import json, sys
data = json.load(sys.stdin)
items = data if isinstance(data, list) else [data]
for rel in items:
    if isinstance(rel, dict) and not rel.get("draft", False) and rel.get("tag_name"):
        print(rel["tag_name"])
        break
'
    else
        tr '{},[]' '\n\n\n\n' | awk -F'"' '
            /"tag_name"[ \t]*:/ { tag = $4 }
            /"draft"[ \t]*:[ \t]*false/ { if (tag != "") { print tag; exit } }
            /"draft"[ \t]*:[ \t]*true/ { tag = "" }
        '
    fi
}

resolve_version() {
    if [ -n "$VERSION" ]; then
        return 0
    fi
    need_cmd awk
    api_json="$TMP_DIR/release.json"
    if [ "$PRERELEASE" != 1 ]; then
        if fetch_url "$API_BASE/releases/latest" "$api_json" 1 2>/dev/null; then
            VERSION="$(extract_first_release_tag < "$api_json")"
        fi
    fi
    if [ -z "$VERSION" ]; then
        if ! fetch_url "$API_BASE/releases?per_page=10" "$api_json" 1; then
            err "failed to query GitHub releases at $API_BASE/releases"
        fi
        VERSION="$(extract_first_release_tag < "$api_json")"
        if [ -n "$VERSION" ] && [ "$PRERELEASE" != 1 ]; then
            say "No stable release found; selecting latest preview release $VERSION"
        fi
    fi
    [ -n "$VERSION" ] || err "could not determine a release tag (pass --version <tag> explicitly)"
}

detect_platform() {
    os_raw="$(uname -s)"
    case "$os_raw" in
        Darwin) OS="macos" ;;
        Linux)  OS="linux" ;;
        MINGW*|MSYS*|CYGWIN*)
            err "Windows shell detected ($os_raw); please use PowerShell: irm https://raw.githubusercontent.com/$REPO/main/scripts/install.ps1 | iex"
            ;;
        *)
            err "unsupported operating system: $os_raw"
            ;;
    esac

    if [ -n "$ARCH_OVERRIDE" ]; then
        case "$ARCH_OVERRIDE" in
            arm64|x64) ARCH="$ARCH_OVERRIDE" ;;
            *) err "invalid KLANG_ARCH '$ARCH_OVERRIDE' (expected arm64 or x64)" ;;
        esac
    else
        arch_raw="$(uname -m)"
        case "$arch_raw" in
            x86_64|amd64)
                ARCH="x64"
                if [ "$OS" = "macos" ] && command -v sysctl >/dev/null 2>&1; then
                    rosetta="$(sysctl -n sysctl.proc_translated 2>/dev/null || printf '0')"
                    if [ "$rosetta" = "1" ]; then
                        ARCH="arm64"
                    fi
                fi
                ;;
            arm64|aarch64)
                ARCH="arm64"
                ;;
            *)
                err "unsupported CPU architecture: $arch_raw"
                ;;
        esac
    fi

    PLATFORM="${OS}-${ARCH}"
}

select_asset() {
    case "$PLATFORM:$PROFILE" in
        macos-arm64:default) ASSET="klang-${VERSION}-macos-arm64.tar.gz" ;;
        macos-arm64:core)    ASSET="klang-${VERSION}-macos-arm64-core.tar.gz" ;;
        macos-x64:default)   ASSET="klang-${VERSION}-macos-x64.tar.gz" ;;
        linux-x64:default)   ASSET="klang-${VERSION}-linux-x64.tar.gz" ;;
        linux-x64:cloud)     ASSET="klang-${VERSION}-linux-x64-cloud.tar.gz" ;;
        linux-arm64:default)
            say "Note: linux-arm64 publishes the cloud archive; installing linux-arm64-cloud."
            ASSET="klang-${VERSION}-linux-arm64-cloud.tar.gz"
            ;;
        linux-arm64:cloud)   ASSET="klang-${VERSION}-linux-arm64-cloud.tar.gz" ;;
        macos-arm64:cloud|macos-x64:core|macos-x64:cloud|linux-x64:core|linux-arm64:core)
            err "profile '$PROFILE' is not published for $PLATFORM"
            ;;
        *)
            err "invalid profile '$PROFILE' (expected default, core, or cloud)"
            ;;
    esac
}

compute_sha256() {
    file="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$file" | awk '{print tolower($1)}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$file" | awk '{print tolower($1)}'
    elif command -v openssl >/dev/null 2>&1; then
        openssl dgst -sha256 "$file" | awk '{print tolower($NF)}'
    else
        err "sha256sum, shasum, or openssl is required to verify download integrity"
    fi
}

verify_checksum() {
    archive="$1"
    sums_file="$2"
    asset_name="$3"

    expected="$(awk -v target="$asset_name" '
        /^[[:space:]]*#/ || NF < 2 { next }
        {
            hash = tolower($1)
            name = $2
            sub(/^\*/, "", name)
            sub(/^\.\//, "", name)
            if (name == target) {
                print hash
                exit
            }
        }
    ' "$sums_file")"
    [ -n "$expected" ] || err "no checksum entry for '$asset_name' found in SHA256SUMS"

    actual="$(compute_sha256 "$archive")"
    if [ "$actual" != "$expected" ]; then
        err "SHA-256 verification failed for $asset_name (expected $expected, got $actual)"
    fi
    say "Verified SHA-256 checksum ($expected)"
}

install_payload() {
    src_dir="$1"
    dest_dir="$2"
    parent_dir="$(dirname "$dest_dir")"
    mkdir -p "$parent_dir"

    backup_dir=""
    if [ -e "$dest_dir" ]; then
        backup_dir="${dest_dir}.bak.$$"
        rm -rf "$backup_dir"
        mv "$dest_dir" "$backup_dir"
    fi

    if ! mv "$src_dir" "$dest_dir"; then
        if [ -n "$backup_dir" ] && [ -e "$backup_dir" ]; then
            mv "$backup_dir" "$dest_dir" 2>/dev/null || true
        fi
        err "failed to move extracted files into $dest_dir"
    fi

    if [ -n "$backup_dir" ] && [ -e "$backup_dir" ]; then
        rm -rf "$backup_dir"
    fi
}

append_path_line() {
    rc_file="$1"
    line="$2"
    marker="# K programming language (klang)"

    if [ -f "$rc_file" ] && grep -Fq "$marker" "$rc_file"; then
        return 0
    fi
    mkdir -p "$(dirname "$rc_file")"
    {
        if [ -f "$rc_file" ] && [ -s "$rc_file" ]; then
            printf '\n'
        fi
        printf '%s\n%s\n' "$marker" "$line"
    } >> "$rc_file"
    say "Added $INSTALL_DIR to PATH in $rc_file"
}

configure_path() {
    if [ "$NO_MODIFY_PATH" = 1 ]; then
        return 0
    fi
    case ":${PATH:-}:" in
        *":$INSTALL_DIR:"*)
            return 0
            ;;
    esac

    shell_name="$(basename "${SHELL:-sh}")"
    case "$shell_name" in
        zsh)
            zdot="${ZDOTDIR:-$HOME}"
            append_path_line "$zdot/.zshrc" "export PATH=\"$INSTALL_DIR:\$PATH\""
            ;;
        bash)
            added=0
            if [ -f "$HOME/.bashrc" ]; then
                append_path_line "$HOME/.bashrc" "export PATH=\"$INSTALL_DIR:\$PATH\""
                added=1
            fi
            if [ -f "$HOME/.bash_profile" ]; then
                append_path_line "$HOME/.bash_profile" "export PATH=\"$INSTALL_DIR:\$PATH\""
                added=1
            fi
            if [ "$added" = 0 ]; then
                if [ "$OS" = "macos" ]; then
                    append_path_line "$HOME/.bash_profile" "export PATH=\"$INSTALL_DIR:\$PATH\""
                else
                    append_path_line "$HOME/.bashrc" "export PATH=\"$INSTALL_DIR:\$PATH\""
                fi
            fi
            ;;
        fish)
            fish_cfg="${XDG_CONFIG_HOME:-$HOME/.config}/fish/config.fish"
            append_path_line "$fish_cfg" "fish_add_path -m \"$INSTALL_DIR\""
            ;;
        *)
            append_path_line "$HOME/.profile" "export PATH=\"$INSTALL_DIR:\$PATH\""
            ;;
    esac
}

main() {
    parse_args "$@"
    need_cmd tar
    need_cmd awk
    need_cmd mktemp
    TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/klang-install.XXXXXX")"

    detect_platform
    resolve_version
    select_asset

    if [ -n "$BASE_URL" ]; then
        dl_base="${BASE_URL%/}"
    else
        dl_base="https://github.com/$REPO/releases/download/$VERSION"
    fi

    dl_dir="$TMP_DIR/download"
    extract_dir="$TMP_DIR/extract"
    mkdir -p "$dl_dir" "$extract_dir"

    archive_path="$dl_dir/$ASSET"
    sums_path="$dl_dir/SHA256SUMS"

    say "Downloading $ASSET ($VERSION, $PLATFORM)..."
    fetch_url "$dl_base/$ASSET" "$archive_path" 0 || err "failed to download $dl_base/$ASSET"
    fetch_url "$dl_base/SHA256SUMS" "$sums_path" 0 || err "failed to download $dl_base/SHA256SUMS"

    verify_checksum "$archive_path" "$sums_path" "$ASSET"

    say "Extracting into $INSTALL_DIR..."
    tar -xzf "$archive_path" -C "$extract_dir"

    k_bin="$(find "$extract_dir" -maxdepth 2 -type f -name k | head -n 1 || true)"
    [ -n "$k_bin" ] || err "extracted archive did not contain a 'k' executable"
    payload_dir="$(dirname "$k_bin")"
    chmod +x "$k_bin"

    installed_ver="$("$k_bin" version 2>/dev/null || true)"
    [ -n "$installed_ver" ] || err "'$k_bin version' failed after extraction"

    install_payload "$payload_dir" "$INSTALL_DIR"
    configure_path

    say ""
    say "Installed K ($installed_ver) to $INSTALL_DIR"
    case ":${PATH:-}:" in
        *":$INSTALL_DIR:"*) ;;
        *)
            say "To use 'k' in your current shell, run:"
            say "  export PATH=\"$INSTALL_DIR:\$PATH\""
            ;;
    esac
    if [ "$OS" = "macos" ]; then
        say "Note: macOS builds require Xcode or Xcode Command Line Tools (xcode-select --install)."
    elif [ "$OS" = "linux" ]; then
        say "Note: Linux builds require a C compiler and system libc development headers (e.g. build-essential / gcc)."
    fi
    say "To uninstall: rm -rf \"$INSTALL_DIR\""
}

main "$@"
