#!/usr/bin/env bash
# hyprtab installer
#   1. dependency check (lists anything missing and aborts)
#   2. build and install the binary to ~/.local/bin/hyprtab
#   3. create the systemd user service (hyprtab.service), left disabled by default
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DST="$HOME/.local/bin/hyprtab"
UNIT_DIR="$HOME/.config/systemd/user"
UNIT="$UNIT_DIR/hyprtab.service"

missing=()

check_cmd() { # $1: command name  $2: hint (package)
    command -v "$1" >/dev/null 2>&1 || missing+=("$1 not found (package: $2)")
}

check_pkg() { # $1: pkg-config name  $2: hint
    pkg-config --exists "$1" 2>/dev/null || missing+=("pkg-config $1 missing (package: $2)")
}

echo "== Dependency check =="
check_cmd cmake      "cmake"
check_cmd c++        "c++ (package: g++ or clang)"
check_cmd pkg-config "pkg-config"
check_cmd install    "coreutils"
check_cmd nproc      "coreutils"
check_cmd hyprctl    "hyprland"
check_cmd systemctl  "systemd"
check_pkg Qt6Quick   "Qt6 Quick dev files (package: qt6-declarative-dev)"
check_pkg Qt6Qml     "Qt6 QML dev files (package: qt6-declarative-dev)"

# QML runtime modules (QtQuick etc.)
qml_imports=""
if command -v qtpaths6 >/dev/null 2>&1; then
    qml_imports="$(qtpaths6 --path QmlImports 2>/dev/null || true)"
fi
if [[ -z "$qml_imports" && -d /usr/lib/qt6/qml ]]; then
    qml_imports=/usr/lib/qt6/qml
fi
if [[ -z "$qml_imports" || ! -d "$qml_imports/QtQuick" ]]; then
    missing+=("QML runtime module QtQuick missing (package: qml6-module-qtquick etc.) [searched: ${qml_imports:-not found}]")
fi

if ((${#missing[@]} > 0)); then
    echo "error: missing dependencies, aborting:" >&2
    printf '  - %s\n' "${missing[@]}" >&2
    echo "example (Debian/Ubuntu): sudo apt install cmake g++ pkg-config qt6-declarative-dev qml6-module-qtquick hyprland" >&2
    exit 1
fi
echo "all dependencies present"

echo "== Build =="
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build" -j"$(nproc)"

echo "== Install: $BIN_DST =="
mkdir -p "$HOME/.local/bin"
# install via rename so a running instance does not cause "Text file busy"
install -m 755 "$ROOT/build/hyprtab" "$BIN_DST.tmp"
mv -f "$BIN_DST.tmp" "$BIN_DST"

echo "== systemd user service: $UNIT =="
mkdir -p "$UNIT_DIR"
cat > "$UNIT" <<EOF
[Unit]
Description=hyprtab - Hyprland Alt-Tab window switcher
After=graphical-session.target

[Service]
Type=simple
ExecStart=$BIN_DST
Restart=on-failure
RestartSec=2

[Install]
WantedBy=default.target
EOF

# daemon-reload only; the service is intentionally NOT enabled/started here.
systemctl --user daemon-reload

cat <<EOF

  Install summary:

    [x] binary: installed to $BIN_DST

    [x] systemd user service (hyprtab.service): installed, disabled by default

    Installation completed.


  To start the switcher automatically at login, enable the service:

    systemctl --user enable hyprtab.service

  To run it manually for now instead:

    systemctl --user start hyprtab.service

  Verify that it is running:

    hyprtab ping    # -> pong

EOF
