#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY_NAME="mochi-sender"
INSTALL_DIR="$HOME/.local/bin"
CONFIG_DIR="$HOME/.config/mochi-sender"
ENV_FILE="$CONFIG_DIR/sender.env"
SERVICE_DIR="$HOME/.config/systemd/user"
SERVICE_NAME="mochi-sender.service"
SERVICE_FILE="$SERVICE_DIR/$SERVICE_NAME"

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
        echo >&2
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
Usage: ./install.sh [OPTIONS]

Mochi Metrics Go Sender - Linux Installer

Options:
  --mqtt-host=HOST    Set MQTT broker host (skip interactive prompt)
  --mqtt-port=PORT    Set MQTT broker port (default: 1883)
  --mqtt-user=USER    Set MQTT username
  --mqtt-pass=PASS    Set MQTT password
  --interval=SEC      Set send interval (default: 1.0)
  --uninstall         Remove sender, service, and config
  --help              Show this help message

Examples:
  ./install.sh                          # Interactive setup
  ./install.sh --mqtt-host=192.168.1.10 # Non-interactive with defaults
  ./install.sh --uninstall              # Remove everything
EOF
}

# --- Prerequisite Checks ---

check_go() {
    if ! command -v go >/dev/null 2>&1; then
        die "Go is not installed. Please install Go 1.22+ from https://go.dev/dl/"
    fi

    local go_version
    go_version="$(go version | grep -oP 'go(\d+\.\d+)' | head -1 | sed 's/go//')"
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

    if systemctl --user is-active "$SERVICE_NAME" >/dev/null 2>&1; then
        systemctl --user stop "$SERVICE_NAME" || true
    fi
    if systemctl --user is-enabled "$SERVICE_NAME" >/dev/null 2>&1; then
        systemctl --user disable "$SERVICE_NAME" || true
    fi

    [[ -f "$SERVICE_FILE" ]] && rm -f "$SERVICE_FILE" && info "Removed $SERVICE_FILE"
    systemctl --user daemon-reload 2>/dev/null || true

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

    # If any arg was provided, skip interactive prompts
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
# Auto-generated by install.sh on $(date '+%Y-%m-%d %H:%M:%S')
MQTT_HOST="${CONF_MQTT_HOST}"
MQTT_PORT="${CONF_MQTT_PORT}"
MQTT_USER="${CONF_MQTT_USER}"
MQTT_PASS="${CONF_MQTT_PASS}"
SEND_INTERVAL_SEC="${CONF_INTERVAL}"
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

# --- systemd Service ---

install_service() {
    mkdir -p "$SERVICE_DIR"
    cat >"$SERVICE_FILE" <<EOF
[Unit]
Description=Mochi Metrics Sender (Go)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
ExecStart=%h/.local/bin/mochi-sender
EnvironmentFile=%h/.config/mochi-sender/sender.env
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
EOF

    systemctl --user daemon-reload
    systemctl --user enable "$SERVICE_NAME"
    systemctl --user restart "$SERVICE_NAME"
    info "Service enabled and (re)started."
    echo ""
    systemctl --user status "$SERVICE_NAME" --no-pager || true
}

# --- Main ---

main() {
    parse_args "$@"
    check_go
    collect_config
    write_env
    build_sender
    install_service

    echo ""
    info "Installation complete!"
    info "Commands:"
    info "  systemctl --user status $SERVICE_NAME"
    info "  systemctl --user restart $SERVICE_NAME"
    info "  journalctl --user -u $SERVICE_NAME -f"
    info "  ./install.sh --uninstall"
}

main "$@"
