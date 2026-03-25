#Requires -Version 5.1
<#
.SYNOPSIS
    Mochi Metrics Go Sender - Windows Installer
.DESCRIPTION
    Compiles the Go sender, installs it, and registers a Scheduled Task for auto-start at logon.
    IMPORTANT: Run this script as Administrator (right-click PowerShell -> Run as Administrator).
.PARAMETER MqttHost
    MQTT broker host (default: 127.0.0.1)
.PARAMETER MqttPort
    MQTT broker port (default: 1883)
.PARAMETER MqttUser
    MQTT username (optional)
.PARAMETER MqttPass
    MQTT password (optional)
.PARAMETER Interval
    Send interval in seconds (default: 1.0)
.PARAMETER Uninstall
    Remove sender, scheduled task, and config
#>
param(
    [string]$MqttHost = "",
    [string]$MqttPort = "",
    [string]$MqttUser = "",
    [string]$MqttPass = "",
    [string]$Interval = "",
    [switch]$Uninstall,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

# --- Admin Check ---
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "ERROR: This script requires Administrator privileges." -ForegroundColor Red
    Write-Host "Right-click PowerShell -> 'Run as Administrator', then re-run this script." -ForegroundColor Yellow
    exit 1
}

$BinaryName = "mochi-sender.exe"
$InstallDir = "$env:LOCALAPPDATA\MochiSender"
$EnvFile = "$InstallDir\sender.env"
$TaskName = "MochiMetricsSender"
$WrapperScript = "$InstallDir\start-sender.ps1"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition

# --- Helpers ---

function Write-Info { param([string]$Msg) Write-Host "==> $Msg" }

function Show-Usage {
    Write-Host @"
Usage: .\install.ps1 [OPTIONS]

Mochi Metrics Go Sender - Windows Installer

Options:
  -MqttHost HOST    Set MQTT broker host (skip interactive prompt)
  -MqttPort PORT    Set MQTT broker port (default: 1883)
  -MqttUser USER    Set MQTT username
  -MqttPass PASS    Set MQTT password
  -Interval SEC     Set send interval (default: 1.0)
  -Uninstall        Remove sender, task, and config
  -Help             Show this help message

Examples:
  .\install.ps1                            # Interactive setup
  .\install.ps1 -MqttHost 192.168.1.10    # Non-interactive with defaults
  .\install.ps1 -Uninstall                # Remove everything
"@
}

# --- Prerequisite Checks ---

function Test-GoInstalled {
    $goCmd = Get-Command go -ErrorAction SilentlyContinue
    if (-not $goCmd) {
        throw "Go is not installed. Please install Go 1.22+ from https://go.dev/dl/"
    }

    $versionOutput = & go version
    if ($versionOutput -match 'go(\d+)\.(\d+)') {
        $major = [int]$Matches[1]
        $minor = [int]$Matches[2]
        if ($major -lt 1 -or ($major -eq 1 -and $minor -lt 22)) {
            throw "Go 1.22+ required, found go${major}.${minor}"
        }
        Write-Info "Go ${major}.${minor} detected"
    } else {
        throw "Cannot parse Go version from: $versionOutput"
    }
}

# --- Uninstall ---

function Invoke-Uninstall {
    Write-Info "Uninstalling Mochi Sender..."

    $task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($task) {
        if ($task.State -eq "Running") {
            Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
        }
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
        Write-Info "Removed scheduled task: $TaskName"
    }

    if (Test-Path $InstallDir) {
        Remove-Item -Recurse -Force $InstallDir
        Write-Info "Removed $InstallDir"
    }

    Write-Info "Uninstall complete."
    exit 0
}

# --- Interactive Setup ---

function Read-ConfigValue {
    param(
        [string]$Label,
        [string]$Default,
        [switch]$Secret
    )
    if ($Secret) {
        if ($Default) {
            $prompt = "$Label [********]"
        } else {
            $prompt = "$Label"
        }
        $secure = Read-Host -Prompt $prompt -AsSecureString
        $bstr = [System.Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
        $plain = [System.Runtime.InteropServices.Marshal]::PtrToStringAuto($bstr)
        [System.Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
        if ([string]::IsNullOrEmpty($plain)) { return $Default }
        return $plain
    } else {
        $input = Read-Host -Prompt "$Label [$Default]"
        if ([string]::IsNullOrEmpty($input)) { return $Default }
        return $input
    }
}

function Get-Config {
    # Load existing config if present
    $conf = @{
        MQTT_HOST = "127.0.0.1"
        MQTT_PORT = "1883"
        MQTT_USER = ""
        MQTT_PASS = ""
        SEND_INTERVAL_SEC = "1.0"
    }

    if (Test-Path $EnvFile) {
        Get-Content $EnvFile | ForEach-Object {
            if ($_ -match '^(\w+)=(.*)$') {
                $conf[$Matches[1]] = $Matches[2]
            }
        }
    }

    # Override with CLI args
    if ($MqttHost) { $conf.MQTT_HOST = $MqttHost }
    if ($MqttPort) { $conf.MQTT_PORT = $MqttPort }
    if ($MqttUser) { $conf.MQTT_USER = $MqttUser }
    if ($MqttPass) { $conf.MQTT_PASS = $MqttPass }
    if ($Interval) { $conf.SEND_INTERVAL_SEC = $Interval }

    # If MqttHost was given via CLI, skip interactive
    if (-not $MqttHost) {
        Write-Host "Mochi Sender setup"
        Write-Host "Press Enter to keep defaults."
        $conf.MQTT_HOST = Read-ConfigValue "MQTT host" $conf.MQTT_HOST
        $conf.MQTT_PORT = Read-ConfigValue "MQTT port" $conf.MQTT_PORT
        $conf.MQTT_USER = Read-ConfigValue "MQTT username (optional)" $conf.MQTT_USER
        $conf.MQTT_PASS = Read-ConfigValue "MQTT password (optional)" $conf.MQTT_PASS -Secret
        $conf.SEND_INTERVAL_SEC = Read-ConfigValue "Send interval (seconds)" $conf.SEND_INTERVAL_SEC
    }

    return $conf
}

function Save-EnvFile {
    param([hashtable]$Config)
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    $lines = @(
        "# Auto-generated by install.ps1 on $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
        "MQTT_HOST=$($Config.MQTT_HOST)"
        "MQTT_PORT=$($Config.MQTT_PORT)"
        "MQTT_USER=$($Config.MQTT_USER)"
        "MQTT_PASS=$($Config.MQTT_PASS)"
        "SEND_INTERVAL_SEC=$($Config.SEND_INTERVAL_SEC)"
    )
    $lines | Out-File -FilePath $EnvFile -Encoding UTF8
    Write-Info "Config saved: $EnvFile"
}

# --- Build ---

function Build-Sender {
    Write-Info "Building Go sender..."
    Push-Location $ScriptDir
    try {
        & go build -o "$InstallDir\$BinaryName" .
        if ($LASTEXITCODE -ne 0) { throw "go build failed" }
    } finally {
        Pop-Location
    }
    Write-Info "Installed: $InstallDir\$BinaryName"
}

# --- Scheduled Task ---

function Install-SenderTask {
    param([hashtable]$Config)

    # Create a wrapper script that loads env vars and starts the sender
    $wrapperContent = @"
# Auto-generated wrapper - loads env and starts mochi-sender
`$envFile = "$EnvFile"
if (Test-Path `$envFile) {
    Get-Content `$envFile | ForEach-Object {
        if (`$_ -match '^(\w+)=(.*)$') {
            [Environment]::SetEnvironmentVariable(`$Matches[1], `$Matches[2], 'Process')
        }
    }
}
& "$InstallDir\$BinaryName"
"@
    $wrapperContent | Out-File -FilePath $WrapperScript -Encoding UTF8
    Write-Info "Created wrapper: $WrapperScript"

    # Stop and remove existing task if present
    $existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($existing) {
        Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
        # Kill any running sender process to release the old binary
        Get-Process -Name "mochi-sender" -ErrorAction SilentlyContinue | Stop-Process -Force
        Start-Sleep -Seconds 1
        Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
    }

    $action = New-ScheduledTaskAction `
        -Execute "powershell.exe" `
        -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$WrapperScript`""

    $trigger = New-ScheduledTaskTrigger -AtLogon
    $settings = New-ScheduledTaskSettingsSet `
        -AllowStartIfOnBatteries `
        -DontStopIfGoingOnBatteries `
        -RestartCount 3 `
        -RestartInterval (New-TimeSpan -Minutes 1)

    # RunLevel Highest: required for WMI MSAcpi_ThermalZoneTemperature (CPU temp)
    $principal = New-ScheduledTaskPrincipal -UserId "$env:USERNAME" -LogonType Interactive -RunLevel Highest

    try {
        Register-ScheduledTask `
            -TaskName $TaskName `
            -Action $action `
            -Trigger $trigger `
            -Settings $settings `
            -Principal $principal `
            -Description "Mochi Metrics Sender (Go)" `
            -ErrorAction Stop | Out-Null
    } catch {
        throw "Failed to register scheduled task: $_"
    }

    Write-Info "Scheduled task registered: $TaskName"

    # Start immediately
    try {
        Start-ScheduledTask -TaskName $TaskName -ErrorAction Stop
    } catch {
        Write-Host "WARNING: Could not start task immediately: $_" -ForegroundColor Yellow
        Write-Host "The task will start automatically at next logon." -ForegroundColor Yellow
        return
    }
    Write-Info "Task started."
}

# --- Main ---

if ($Help) {
    Show-Usage
    exit 0
}

if ($Uninstall) {
    Invoke-Uninstall
}

Test-GoInstalled
$config = Get-Config
Save-EnvFile -Config $config
Build-Sender
Install-SenderTask -Config $config

Write-Host ""
Write-Info "Installation complete!"
Write-Info "Commands:"
Write-Info "  Get-ScheduledTask -TaskName $TaskName"
Write-Info "  Start-ScheduledTask -TaskName $TaskName"
Write-Info "  Stop-ScheduledTask -TaskName $TaskName"
Write-Info "  .\install.ps1 -Uninstall"
