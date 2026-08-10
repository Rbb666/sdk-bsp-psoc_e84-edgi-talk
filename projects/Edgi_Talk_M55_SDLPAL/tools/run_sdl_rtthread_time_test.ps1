param(
    [string]$Compiler = "D:\softwoare\tools_dept\mingw64\bin\gcc.exe"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$testRoot = Join-Path $PSScriptRoot "host-tests\sdl_rtthread_time"
$shimRoot = Join-Path $projectRoot "sdlpal\port\rtthread"
$output = Join-Path ([IO.Path]::GetTempPath()) ("sdlpal-rtthread-time-{0}.exe" -f $PID)

if (-not (Test-Path -LiteralPath $Compiler)) {
    throw "Host compiler not found: $Compiler"
}

$compilerArgs = @(
    "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic",
    ("-I{0}" -f (Join-Path $testRoot "fakes")),
    ("-I{0}" -f (Join-Path $shimRoot "include")),
    "-DPAL_ENGINE_BRIDGE_REQUIRE_TARGET_HOOKS=1",
    "-DPAL_SDL_SHIM_EXTERNAL_SURFACES_ONLY=1",
    "-DPAL_SDL_SHIM_DYNAMIC_SURFACES=1",
    (Join-Path $testRoot "test_sdl_rtthread_time.c"),
    (Join-Path $shimRoot "sdl_shim.c"),
    "-o", $output
)

try {
    & $Compiler @compilerArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    & $output
    exit $LASTEXITCODE
}
finally {
    if (Test-Path -LiteralPath $output) {
        Remove-Item -LiteralPath $output -Force
    }
}
