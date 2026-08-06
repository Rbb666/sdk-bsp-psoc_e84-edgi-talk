param(
    [string]$ToolchainBin = "D:\workspace_work\env-windows\tools\gnu_gcc\arm_gcc\mingw\bin",
    [string]$Scons = "D:\workspace_work\env-windows\.venv\Scripts\scons.exe",
    [string]$Python = "D:\workspace_work\env-windows\.venv\Scripts\python.exe",
    [ValidateSet("touch", "keyboard")]
    [string]$InputMode = "keyboard",
    [int]$Jobs = 16
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$configPath = Join-Path $projectRoot ".config"
$headerPath = Join-Path $projectRoot "rtconfig.h"
$elfPath = Join-Path $projectRoot "rt-thread.elf"
$mapPath = Join-Path $projectRoot "rtthread.map"
$reportsPath = Join-Path $projectRoot "reports"
$checkerPath = Join-Path $PSScriptRoot "check_elf.py"
$nmTool = Join-Path $ToolchainBin "arm-none-eabi-nm.exe"
$sizeTool = Join-Path $ToolchainBin "arm-none-eabi-size.exe"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$originalConfig = [System.IO.File]::ReadAllText($configPath)
$originalHeader = [System.IO.File]::ReadAllText($headerPath)
$originalRttExecPath = $env:RTT_EXEC_PATH

$configPattern = '(?ms)^(?:CONFIG_M55_BSP_LCD_ROTATION_0=y|# CONFIG_M55_BSP_LCD_ROTATION_0 is not set)\r?\n' +
    '(?:CONFIG_M55_BSP_LCD_ROTATION_90=y|# CONFIG_M55_BSP_LCD_ROTATION_90 is not set)\r?\n' +
    '(?:CONFIG_M55_BSP_LCD_ROTATION_180=y|# CONFIG_M55_BSP_LCD_ROTATION_180 is not set)\r?\n' +
    '(?:CONFIG_M55_BSP_LCD_ROTATION_270=y|# CONFIG_M55_BSP_LCD_ROTATION_270 is not set)\r?\n' +
    'CONFIG_BSP_LCD_ROTATION_(?:0|90|180|270)=y\r?\n' +
    'CONFIG_BSP_LCD_ROTATION_DEGREES=(?:0|90|180|270)\r?\n' +
    '(?:(?:CONFIG_BSP_LCD_ROTATION_BACKEND_VGLITE=y|# CONFIG_BSP_LCD_ROTATION_BACKEND_VGLITE is not set)\r?\n)?' +
    '(?:(?:CONFIG_BSP_LCD_PANEL_SCAN_ROTATE_180=y|# CONFIG_BSP_LCD_PANEL_SCAN_ROTATE_180 is not set)\r?\n)?'
$headerPattern = '(?ms)^#define M55_BSP_LCD_ROTATION_(?:0|90|180|270)\r?\n' +
    '#define BSP_LCD_ROTATION_(?:0|90|180|270)\r?\n' +
    '#define BSP_LCD_ROTATION_DEGREES (?:0|90|180|270)\r?\n' +
    '(?:#define BSP_LCD_ROTATION_BACKEND_VGLITE\r?\n)?' +
    '(?:#define BSP_LCD_PANEL_SCAN_ROTATE_180\r?\n)?'

function New-RotationConfig([int]$Rotation) {
    $m55Lines = foreach ($value in 0, 90, 180, 270) {
        if ($value -eq $Rotation) {
            "CONFIG_M55_BSP_LCD_ROTATION_$value=y"
        } else {
            "# CONFIG_M55_BSP_LCD_ROTATION_$value is not set"
        }
    }
    $bspLines = "CONFIG_BSP_LCD_ROTATION_$Rotation=y"
    $extraLines = @(
        "CONFIG_BSP_LCD_ROTATION_DEGREES=$Rotation",
        $(if ($Rotation -in 90, 270) {
            "CONFIG_BSP_LCD_ROTATION_BACKEND_VGLITE=y"
        } else {
            "# CONFIG_BSP_LCD_ROTATION_BACKEND_VGLITE is not set"
        }),
        $(if ($Rotation -eq 180) {
            "CONFIG_BSP_LCD_PANEL_SCAN_ROTATE_180=y"
        } else {
            "# CONFIG_BSP_LCD_PANEL_SCAN_ROTATE_180 is not set"
        })
    )
    $replacement = (($m55Lines + $bspLines + $extraLines) -join "`n") + "`n"
    if (-not [regex]::IsMatch($originalConfig, $configPattern)) {
        throw "Rotation block was not found in .config"
    }
    return [regex]::Replace($originalConfig, $configPattern, $replacement)
}

function New-RotationHeader([int]$Rotation) {
    $lines = @(
        "#define M55_BSP_LCD_ROTATION_$Rotation",
        "#define BSP_LCD_ROTATION_$Rotation",
        "#define BSP_LCD_ROTATION_DEGREES $Rotation"
    )
    if ($Rotation -in 90, 270) {
        $lines += "#define BSP_LCD_ROTATION_BACKEND_VGLITE"
    }
    if ($Rotation -eq 180) {
        $lines += "#define BSP_LCD_PANEL_SCAN_ROTATE_180"
    }
    $replacement = ($lines -join "`n") + "`n"
    if (-not [regex]::IsMatch($originalHeader, $headerPattern)) {
        throw "Rotation block was not found in rtconfig.h"
    }
    return [regex]::Replace($originalHeader, $headerPattern, $replacement)
}

foreach ($requiredTool in $Scons, $Python, $nmTool, $sizeTool, $checkerPath) {
    if (-not (Test-Path -LiteralPath $requiredTool)) {
        throw "Required tool not found: $requiredTool"
    }
}

New-Item -ItemType Directory -Force -Path $reportsPath | Out-Null
$env:RTT_EXEC_PATH = $ToolchainBin

Push-Location $projectRoot
try {
    foreach ($rotation in 90, 180, 270, 0) {
        Write-Host "== SDLPal rotation $rotation =="
        [System.IO.File]::WriteAllText(
            $configPath, (New-RotationConfig $rotation), $utf8NoBom
        )
        [System.IO.File]::WriteAllText(
            $headerPath, (New-RotationHeader $rotation), $utf8NoBom
        )

        & $Scons -c
        if ($LASTEXITCODE -ne 0) {
            throw "Clean failed for rotation $rotation"
        }
        & $Scons "-j$Jobs"
        if ($LASTEXITCODE -ne 0) {
            throw "Build failed for rotation $rotation"
        }

        $sizeOutput = & $sizeTool -A $elfPath
        if ($LASTEXITCODE -ne 0) {
            throw "Size report failed for rotation $rotation"
        }
        $reportPath = Join-Path $reportsPath `
            "$InputMode-rotation-$rotation-size.txt"
        [System.IO.File]::WriteAllLines($reportPath, $sizeOutput, $utf8NoBom)

        $validationOutput = & $Python $checkerPath `
            --elf $elfPath --map $mapPath --nm $nmTool `
            --rotation $rotation --input-mode $InputMode
        if ($LASTEXITCODE -ne 0) {
            throw "ELF validation failed for rotation $rotation"
        }
        [System.IO.File]::AppendAllLines(
            $reportPath, [string[]]$validationOutput, $utf8NoBom
        )
        $validationOutput | ForEach-Object { Write-Host $_ }
    }
}
finally {
    [System.IO.File]::WriteAllText($configPath, $originalConfig, $utf8NoBom)
    [System.IO.File]::WriteAllText($headerPath, $originalHeader, $utf8NoBom)
    if ($null -eq $originalRttExecPath) {
        Remove-Item Env:RTT_EXEC_PATH -ErrorAction SilentlyContinue
    } else {
        $env:RTT_EXEC_PATH = $originalRttExecPath
    }
    Pop-Location
}

Write-Host "$InputMode rotation matrix passed; reports: $reportsPath"
