#!/bin/sh
set -eu

SOURCE="/userdata/system/add-ons/sunshine/share/batocera/sunshine"
DEST="/userdata/system/services/sunshine"
CONFIG_DIR="/userdata/system/configs"
SERVICE_CONFIG="${CONFIG_DIR}/sunshine-service.conf"

[ -r "$SOURCE" ] || { echo "ERROR: service template not found: $SOURCE" >&2; exit 1; }
mkdir -p /userdata/system/services "$CONFIG_DIR"

if [ -e "$DEST" ]; then
    BACKUP_DIR="/userdata/system/service-backups"
    mkdir -p "$BACKUP_DIR"
    BACKUP="${BACKUP_DIR}/sunshine.$(date +%Y%m%d-%H%M%S)"
    cp -a "$DEST" "$BACKUP"
    echo "Existing service backed up to: $BACKUP"
fi

cp "$SOURCE" "$DEST"
chmod 0755 "$DEST"

if [ ! -e "$SERVICE_CONFIG" ]; then
    cat > "$SERVICE_CONFIG" <<'CFG'
# Optional Sunshine service overrides.
# Default configuration file:
# SUNSHINE_CONFIG=/userdata/system/.config/sunshine/sunshine.conf
# Optional console log path:
# SUNSHINE_CONSOLE_LOG=/userdata/system/.config/sunshine/service-console.log
# Optional boot prerequisite timeout in seconds:
# SUNSHINE_RUNTIME_TIMEOUT=60
CFG
fi

batocera-services enable sunshine
echo "Sunshine Batocera service installed and enabled."
echo "Start now with: batocera-services start sunshine"
