#!/bin/zsh
# Remove the dji-wisprer bridge completely.
LABEL="com.djiwisprer.bridge"
APP_DIR="$HOME/Library/Application Support/dji-wisprer"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"

echo "==> Stopping service"
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true

echo "==> Removing files"
rm -f "$PLIST"
rm -rf "$APP_DIR"

cat <<EOF
==> Done.
Note: you may still see a stale "dji-wisprer" entry in
System Settings > Privacy & Security > Accessibility.
Remove it manually with the "-" button if you like.
Also reset your Wispr hands-free shortcut if you changed it.
EOF
