#!/bin/zsh
# Install the dji-wisprer bridge: build, sign, install binary + LaunchAgent,
# then open the permission panes you need to approve.
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

echo "==> Writing LaunchAgent: $PLIST"
mkdir -p "$HOME/Library/LaunchAgents"
sed "s|__BIN__|$BIN|g" "$HERE/launchd/$LABEL.plist.template" > "$PLIST"

echo "==> Loading service"
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"

echo "==> Opening permission settings"
# Accessibility: required to inject the keystroke.
open "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility" 2>/dev/null || true
# A Finder window at the binary so you can drag it into the Accessibility list.
open "$APP_DIR" 2>/dev/null || true

cat <<EOF

============================================================
 Almost done — two manual steps:

 1) GRANT ACCESSIBILITY
    In the System Settings window that just opened
    (Privacy & Security > Accessibility), click "+", press
    Cmd+Shift+G, paste:
        $BIN
    Select it, and switch its toggle ON.
    (If Input Monitoring is also requested, allow it too.)

    Then reload the service so it picks up the grant:
        launchctl kickstart -k gui/\$(id -u)/$LABEL

 2) SET THE WISPR SHORTCUT
    Wispr Flow > Settings > General > Shortcuts > Hands-free
    > click the box so it says "listening", then PRESS THE
    DJI VOLUME BUTTON once. It should capture  ^⌥F18 . Save.

 Test: tap the DJI volume button -> dictation toggles on/off.
 Logs: /tmp/dji-wisprer.log   (and /tmp/dji-wisprer.err)
============================================================
EOF
