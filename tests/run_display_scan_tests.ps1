$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -I src tests/row_cache_test.c src/ui/row_cache.c -o .pio/host-tests/row_cache_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Row cache compilation failed' }
    & ./.pio/host-tests/row_cache_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Row cache tests failed' }
    $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
    foreach ($mode in @('observe','sync')) {
        $arguments = @('-std=c11','-Wall','-Wextra','-Werror','-DCHRONVS_DISPLAY_SCAN',
            '-DLV_CONF_INCLUDE_SIMPLE','-I','tests/display_stubs','-I','tests/Relogio_stubs',
            '-I','src','-I',$lvglRoot,'tests/display_scan_test.c','-o',".pio/host-tests/display-scan-$mode.exe")
        if ($mode -eq 'sync') { $arguments += '-DCHRONVS_DISPLAY_TE_SYNC' }
        & gcc @arguments
        if ($LASTEXITCODE -ne 0) { throw "Display scan $mode compilation failed" }
        & "./.pio/host-tests/display-scan-$mode.exe"
        if ($LASTEXITCODE -ne 0) { throw "Display scan $mode tests failed" }
    }
} finally { Pop-Location }
