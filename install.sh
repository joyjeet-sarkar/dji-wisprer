#!/bin/zsh
# Install the dji-wisprer bridge: build, sign, ask which key to emit, install the
# binary + LaunchAgent, then open the permission panes you need to approve.
set -e

LABEL="com.djiwisprer.bridge"
APP_DIR="$HOME/Library/Application Support/dji-wisprer"
BIN="$APP_DIR/dji-wisprer"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
HERE="${0:A:h}"   # directory of this script

echo "==> Building"
make -C "$HERE" all

echo "==> Installing binary to: $BIN"
mkdir -p "$APP_DIR"
cp "$HERE/build/dji-wisprer" "$BIN"

echo "==> Ad-hoc code-signing (stable identity for permissions)"
codesign --force --sign - --identifier "$LABEL" "$BIN"

# ---------------------------------------------------------------------------
# Ask which key the DJI button should send to Wispr.
#
# This does NOT replace your keyboard shortcut — Wispr lets you bind several
# shortcuts per action, so your keyboard key (e.g. Fn) keeps working AND the
# DJI button gets its own. Pick a key here that does not collide with your
# keyboard one. Ctrl+Opt+F18 is a safe, unique default.
# ---------------------------------------------------------------------------
echo
echo "Which key should the DJI button send to Wispr?"
echo "  1) Ctrl+Opt+F18   (recommended — unique 'phantom' chord; bind to Wispr Hands-free)"
echo "  2) Fn / Globe      (experimental — macOS rarely lets software synthesize Fn)"
echo "  3) Custom          (advanced — enter a macOS keycode + modifiers)"
read "choice?Enter 1, 2, or 3 [1]: "
choice="${choice:-1}"

case "$choice" in
  2)
    EMIT="fn"
    ENV_XML="        <key>DJI_WISPRER_EMIT</key><string>fn</string>"
    BIND_HINT="Fn / Globe (note: may not register; see README)"
    ;;
  3)
    read "kc?macOS virtual keycode (decimal, e.g. 79 = F18): "
    read "md?Modifiers, comma list of control,option,command,shift (blank for none): "
    EMIT="custom"
    ENV_XML="        <key>DJI_WISPRER_EMIT</key><string>custom</string>
        <key>DJI_WISPRER_KEYCODE</key><string>${kc}</string>
        <key>DJI_WISPRER_MODS</key><string>${md}</string>"
    BIND_HINT="your custom keycode ${kc} + [${md}]"
    ;;
  *)
    EMIT="chord"
    ENV_XML="        <key>DJI_WISPRER_EMIT</key><string>chord</string>"
    BIND_HINT="Ctrl+Opt+F18  ( ^⌥F18 )"
    ;;
esac
echo "==> DJI button will emit: $BIND_HINT"

echo "==> Writing LaunchAgent: $PLIST"
mkdir -p "$HOME/Library/LaunchAgents"
cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$BIN</string>
    </array>
    <key>EnvironmentVariables</key>
    <dict>
$ENV_XML
    </dict>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>ThrottleInterval</key>
    <integer>5</integer>
    <key>StandardOutPath</key>
    <string>/tmp/dji-wisprer.log</string>
    <key>StandardErrorPath</key>
    <string>/tmp/dji-wisprer.err</string>
</dict>
</plist>
EOF

echo "==> Loading service"
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"

echo "==> Opening permission settings"
open "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility" 2>/dev/null || true
open "$APP_DIR" 2>/dev/null || true

cat <<EOF

============================================================
 Almost done — two manual steps (see setup.md for detail):

 1) GRANT ACCESSIBILITY
    Privacy & Security > Accessibility > "+", press Cmd+Shift+G, paste:
        $BIN
    Select it, toggle it ON. (Allow Input Monitoring too if asked.)
    Then reload:  launchctl kickstart -k gui/\$(id -u)/$LABEL

 2) SET THE WISPR SHORTCUT
    Wispr Flow > Settings > General > Shortcuts > Hands-free > click the
    box ("listening"), then PRESS THE DJI VOLUME BUTTON once. It captures
    $BIND_HINT. Save. Your keyboard shortcut is untouched and still works.

 Test: tap the DJI volume button -> dictation toggles on/off.
 Logs: /tmp/dji-wisprer.log
============================================================
EOF
