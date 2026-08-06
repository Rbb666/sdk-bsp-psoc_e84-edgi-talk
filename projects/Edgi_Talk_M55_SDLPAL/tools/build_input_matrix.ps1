param(
    [string]$ToolchainBin = "D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin",
    [string]$Scons = "D:\workspace_work\env-windows\.venv\Scripts\scons.exe",
    [string]$Python = "D:\workspace_work\env-windows\.venv\Scripts\python.exe",
    [string]$BuildMatrix = (Join-Path $PSScriptRoot "build_matrix.ps1"),
    [int]$Jobs = 16
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$configPath = Join-Path $projectRoot ".config"
$headerPath = Join-Path $projectRoot "rtconfig.h"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$originalConfig = [System.IO.File]::ReadAllText($configPath)
$originalHeader = [System.IO.File]::ReadAllText($headerPath)

function Set-ConfigBoolean(
    [string]$Text, [string]$Symbol, [bool]$Enabled
) {
    $configSymbol = "CONFIG_$Symbol"
    $escaped = [regex]::Escape($configSymbol)
    $pattern = "(?m)^(?:$escaped=y|# $escaped is not set)$"
    if (-not [regex]::IsMatch($Text, $pattern)) {
        throw "Config symbol was not found: $configSymbol"
    }
    $replacement = if ($Enabled) {
        "$configSymbol=y"
    } else {
        "# $configSymbol is not set"
    }
    return [regex]::Replace($Text, $pattern, $replacement)
}

function Set-ConfigInteger(
    [string]$Text, [string]$Symbol, [bool]$Enabled, [int]$Value
) {
    $configSymbol = "CONFIG_$Symbol"
    $escaped = [regex]::Escape($configSymbol)
    $pattern = "(?m)^(?:$escaped=[0-9]+|# $escaped is not set)$"
    if (-not [regex]::IsMatch($Text, $pattern)) {
        throw "Config value was not found: $configSymbol"
    }
    $replacement = if ($Enabled) {
        "$configSymbol=$Value"
    } else {
        "# $configSymbol is not set"
    }
    return [regex]::Replace($Text, $pattern, $replacement)
}

function Set-HeaderDefine(
    [string]$Text, [string]$Symbol, [bool]$Enabled,
    [string]$Value = ""
) {
    $escaped = [regex]::Escape($Symbol)
    $pattern = "(?m)^#define $escaped(?: [^`r`n]+)?`r?`n"
    $line = if ([string]::IsNullOrEmpty($Value)) {
        "#define $Symbol`n"
    } else {
        "#define $Symbol $Value`n"
    }
    if ($Enabled) {
        if ([regex]::IsMatch($Text, $pattern)) {
            return [regex]::Replace($Text, $pattern, $line)
        }
        if (-not [regex]::IsMatch($Text, '(?m)^#endif\s*$')) {
            throw "rtconfig.h final #endif was not found"
        }
        return [regex]::Replace(
            $Text, '(?m)^#endif\s*$', "$line`n#endif", 1
        )
    }
    return [regex]::Replace($Text, $pattern, "")
}

function New-InputConfig([string]$InputMode) {
    $keyboard = $InputMode -eq "keyboard"
    $updated = $originalConfig
    $updated = Set-ConfigBoolean $updated `
        "BSP_SDLPAL_INPUT_TOUCH" (-not $keyboard)
    $updated = Set-ConfigBoolean $updated `
        "BSP_SDLPAL_INPUT_USB_KEYBOARD" $keyboard
    foreach ($symbol in @(
        "RT_USING_CHERRYUSB",
        "RT_CHERRYUSB_HOST",
        "RT_CHERRYUSB_HOST_DWC2_INFINEON",
        "RT_CHERRYUSB_HOST_HID"
    )) {
        $updated = Set-ConfigBoolean $updated $symbol $keyboard
    }
    return Set-ConfigInteger $updated `
        "CONFIG_USBHOST_MAX_INTF_ALTSETTINGS" $keyboard 12
}

function New-InputHeader([string]$InputMode) {
    $keyboard = $InputMode -eq "keyboard"
    $updated = $originalHeader
    $updated = Set-HeaderDefine $updated `
        "BSP_SDLPAL_INPUT_TOUCH" (-not $keyboard)
    $updated = Set-HeaderDefine $updated `
        "BSP_SDLPAL_INPUT_USB_KEYBOARD" $keyboard
    foreach ($symbol in @(
        "RT_USING_CHERRYUSB",
        "RT_CHERRYUSB_HOST",
        "RT_CHERRYUSB_HOST_DWC2_INFINEON",
        "RT_CHERRYUSB_HOST_HID"
    )) {
        $updated = Set-HeaderDefine $updated $symbol $keyboard
    }
    return Set-HeaderDefine $updated `
        "CONFIG_USBHOST_MAX_INTF_ALTSETTINGS" $keyboard "12"
}

if (-not (Test-Path -LiteralPath $BuildMatrix)) {
    throw "Rotation matrix script not found: $BuildMatrix"
}

try {
    foreach ($inputMode in "touch", "keyboard") {
        Write-Host "== SDLPal input mode $inputMode =="
        [System.IO.File]::WriteAllText(
            $configPath, (New-InputConfig $inputMode), $utf8NoBom
        )
        [System.IO.File]::WriteAllText(
            $headerPath, (New-InputHeader $inputMode), $utf8NoBom
        )

        & $BuildMatrix -ToolchainBin $ToolchainBin -Scons $Scons `
            -Python $Python -InputMode $inputMode -Jobs $Jobs
        if (-not $?) {
            throw "Rotation matrix failed for input mode $inputMode"
        }
    }
}
finally {
    [System.IO.File]::WriteAllText($configPath, $originalConfig, $utf8NoBom)
    [System.IO.File]::WriteAllText($headerPath, $originalHeader, $utf8NoBom)
}

Write-Host "SDLPal input/rotation matrix passed"
