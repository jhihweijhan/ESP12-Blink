#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY_NAME="mochi-sender"
INSTALL_DIR="$HOME/.local/bin"
CONFIG_DIR="$HOME/.config/mochi-sender"
ENV_FILE="$CONFIG_DIR/sender.env"
PLIST_LABEL="com.mochi-metrics.sender"
PLIST_DIR="$HOME/Library/LaunchAgents"
PLIST_FILE="$PLIST_DIR/$PLIST_LABEL.plist"

# --- Helpers ---

die() {
    echo "ERROR: $*" >&2
    exit 1
}

info() {
    echo "==> $*"
}

prompt_value() {
    local label="$1"
    local default="$2"
    local secret="${3:-0}"
    local input=""

    if [[ "$secret" == "1" ]]; then
        if [[ -n "$default" ]]; then
            read -r -s -p "$label [********]: " input
        else
            read -r -s -p "$label: " input
        fi
        echo
    else
        read -r -p "$label [$default]: " input
    fi

    if [[ -z "$input" ]]; then
        printf '%s' "$default"
    else
        printf '%s' "$input"
    fi
}

usage() {
    cat <<'EOF'
Usage: ./install-macos.sh [OPTIONS]

Mochi Metrics Go Sender - macOS Installer

Options:
  --mqtt-host=HOST    Set MQTT broker host (skip interactive prompt)
  --mqtt-port=PORT    Set MQTT broker port (default: 1883)
  --mqtt-user=USER    Set MQTT username
  --mqtt-pass=PASS    Set MQTT password
  --interval=SEC      Set send interval (default: 1.0)
  --uninstall         Remove sender, launchd agent, and config
  --help              Show this help message

Examples:
  ./install-macos.sh                          # Interactive setup
  ./install-macos.sh --mqtt-host=192.168.1.10 # Non-interactive with defaults
  ./install-macos.sh --uninstall              # Remove everything
EOF
}

# --- Prerequisite Checks ---

check_go() {
    if ! command -v go >/dev/null 2>&1; then
        die "Go is not installed. Please install Go 1.22+ from https://go.dev/dl/ or via: brew install go"
    fi

    local go_version
    go_version="$(go version | grep -oE 'go[0-9]+\.[0-9]+' | head -1 | sed 's/go//')"
    local major minor
    major="${go_version%%.*}"
    minor="${go_version#*.}"

    if [[ "$major" -lt 1 ]] || { [[ "$major" -eq 1 ]] && [[ "$minor" -lt 22 ]]; }; then
        die "Go 1.22+ required, found go${go_version}"
    fi
    info "Go ${go_version} detected"
}

# --- Uninstall ---

do_uninstall() {
    info "Uninstalling Mochi Sender..."

    if launchctl list "$PLIST_LABEL" >/dev/null 2>&1; then
        launchctl unload "$PLIST_FILE" 2>/dev/null || true
        info "Unloaded launchd agent: $PLIST_LABEL"
    fi

    [[ -f "$PLIST_FILE" ]] && rm -f "$PLIST_FILE" && info "Removed $PLIST_FILE"
    [[ -f "$INSTALL_DIR/$BINARY_NAME" ]] && rm -f "$INSTALL_DIR/$BINARY_NAME" && info "Removed $INSTALL_DIR/$BINARY_NAME"
    [[ -d "$CONFIG_DIR" ]] && rm -rf "$CONFIG_DIR" && info "Removed $CONFIG_DIR"

    info "Uninstall complete."
    exit 0
}

# --- Parse Arguments ---

ARG_MQTT_HOST=""
ARG_MQTT_PORT=""
ARG_MQTT_USER=""
ARG_MQTT_PASS=""
ARG_INTERVAL=""

parse_args() {
    for arg in "$@"; do
        case "$arg" in
            --mqtt-host=*) ARG_MQTT_HOST="${arg#*=}" ;;
            --mqtt-port=*) ARG_MQTT_PORT="${arg#*=}" ;;
            --mqtt-user=*) ARG_MQTT_USER="${arg#*=}" ;;
            --mqtt-pass=*) ARG_MQTT_PASS="${arg#*=}" ;;
            --interval=*)  ARG_INTERVAL="${arg#*=}" ;;
            --uninstall)   do_uninstall ;;
            --help)        usage; exit 0 ;;
            *) die "Unknown option: $arg (see --help)" ;;
        esac
    done
}

# --- Interactive Setup ---

load_existing_env_defaults() {
    if [[ -f "$ENV_FILE" ]]; then
        # shellcheck disable=SC1090
        source "$ENV_FILE"
    fi
}

collect_config() {
    load_existing_env_defaults

    local mqtt_host="${ARG_MQTT_HOST:-${MQTT_HOST:-127.0.0.1}}"
    local mqtt_port="${ARG_MQTT_PORT:-${MQTT_PORT:-1883}}"
    local mqtt_user="${ARG_MQTT_USER:-${MQTT_USER:-}}"
    local mqtt_pass="${ARG_MQTT_PASS:-${MQTT_PASS:-}}"
    local send_interval="${ARG_INTERVAL:-${SEND_INTERVAL_SEC:-1.0}}"

    # If mqtt-host was provided, skip interactive prompts
    if [[ -n "$ARG_MQTT_HOST" ]]; then
        CONF_MQTT_HOST="$mqtt_host"
        CONF_MQTT_PORT="$mqtt_port"
        CONF_MQTT_USER="$mqtt_user"
        CONF_MQTT_PASS="$mqtt_pass"
        CONF_INTERVAL="$send_interval"
        return
    fi

    # Interactive mode
    if [[ -t 0 ]]; then
        echo "Mochi Sender setup"
        echo "Press Enter to keep defaults."
        mqtt_host="$(prompt_value "MQTT host" "$mqtt_host")"
        mqtt_port="$(prompt_value "MQTT port" "$mqtt_port")"
        mqtt_user="$(prompt_value "MQTT username (optional)" "$mqtt_user")"
        mqtt_pass="$(prompt_value "MQTT password (optional)" "$mqtt_pass" "1")"
        send_interval="$(prompt_value "Send interval (seconds)" "$send_interval")"
    fi

    [[ "$mqtt_port" =~ ^[0-9]+$ ]] || die "MQTT port must be a number"

    CONF_MQTT_HOST="$mqtt_host"
    CONF_MQTT_PORT="$mqtt_port"
    CONF_MQTT_USER="$mqtt_user"
    CONF_MQTT_PASS="$mqtt_pass"
    CONF_INTERVAL="$send_interval"
}

write_env() {
    mkdir -p "$CONFIG_DIR"
    cat >"$ENV_FILE" <<EOF
# Auto-generated by install-macos.sh on $(date '+%Y-%m-%d %H:%M:%S')
MQTT_HOST=${CONF_MQTT_HOST}
MQTT_PORT=${CONF_MQTT_PORT}
MQTT_USER=${CONF_MQTT_USER}
MQTT_PASS=${CONF_MQTT_PASS}
SEND_INTERVAL_SEC=${CONF_INTERVAL}
EOF
    chmod 600 "$ENV_FILE"
    info "Config saved: $ENV_FILE"
}

# --- Build ---

build_sender() {
    info "Building Go sender..."
    cd "$SCRIPT_DIR"
    go build -o "$BINARY_NAME" .
    mkdir -p "$INSTALL_DIR"
    mv "$BINARY_NAME" "$INSTALL_DIR/$BINARY_NAME"
    chmod +x "$INSTALL_DIR/$BINARY_NAME"
    info "Installed: $INSTALL_DIR/$BINARY_NAME"
}

# --- launchd Agent ---

install_launchd_agent() {
    mkdir -p "$PLIST_DIR"

    cat >"$PLIST_FILE" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>${PLIST_LABEL}</string>
    <key>ProgramArguments</key>
    <array>
        <string>${INSTALL_DIR}/${BINARY_NAME}</string>
    </array>
    <key>EnvironmentVariables</key>
    <dict>
        <key>MQTT_HOST</key>
        <string>${CONF_MQTT_HOST}</string>
        <key>MQTT_PORT</key>
        <string>${CONF_MQTT_PORT}</string>
        <key>MQTT_USER</key>
        <string>${CONF_MQTT_USER}</string>
        <key>MQTT_PASS</key>
        <string>${CONF_MQTT_PASS}</string>
        <key>SEND_INTERVAL_SEC</key>
        <string>${CONF_INTERVAL}</string>
    </dict>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>/tmp/mochi-sender.log</string>
    <key>StandardErrorPath</key>
    <string>/tmp/mochi-sender.err</string>
</dict>
</plist>
EOF

    # Unload if already loaded
    launchctl unload "$PLIST_FILE" 2>/dev/null || true
    launchctl load "$PLIST_FILE"
    info "launchd agent loaded: $PLIST_LABEL"
}

# --- Main ---

main() {
    parse_args "$@"
    check_go
    collect_config
    write_env
    build_sender
    install_launchd_agent

    echo ""
    info "Installation complete!"
    info "Commands:"
    info "  launchctl list | grep mochi"
    info "  launchctl unload $PLIST_FILE"
    info "  launchctl load $PLIST_FILE"
    info "  tail -f /tmp/mochi-sender.log"
    info "  ./install-macos.sh --uninstall"
}

main "$@"
