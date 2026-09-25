$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
    $sources = @(rg --files "$lvglRoot/src" -g '*.c')
    $arguments = @('-std=c11', '-O0', '-DLV_CONF_INCLUDE_SIMPLE', '-I', 'src', '-I',
        'tests/Relogio_stubs', '-I', $lvglRoot, 'tests/watch_geometry_test.c') +
        $sources + @('-lm', '-o', '.pio/host-tests/watch_geometry_test.exe')
    ($arguments | ForEach-Object { '"' + $_.Replace('\','/') + '"' }) |
        Set-Content '.pio/host-tests/watch-geometry-compile.rsp'
    & gcc '@.pio/host-tests/watch-geometry-compile.rsp'
    if ($LASTEXITCODE -ne 0) { throw 'Watch geometry compilation failed' }
    & ./.pio/host-tests/watch_geometry_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Watch geometry tests failed' }
} finally { Pop-Location }
