param([switch]$UI)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    if ($UI) {
        $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
        $sources = @(rg --files "$lvglRoot/src" -g '*.c')
        $arguments = @('-std=c11', '-O0', '-DLV_CONF_INCLUDE_SIMPLE', '-I', 'src', '-I', 'tests/aion_stubs', '-I', $lvglRoot,
            'tests/mnemo_ui_test.c', 'src/core/mnemo_text.c', 'src/ui/mnemo_font.c', 'src/ui/control_style.c', 'src/ui/app_input.c') + $sources + @('-o', '.pio/host-tests/mnemo_ui_test.exe')
        ($arguments | ForEach-Object { '"' + $_.Replace('\','/') + '"' }) | Set-Content '.pio/host-tests/mnemo-compile.rsp'
        & gcc '@.pio/host-tests/mnemo-compile.rsp'
        if ($LASTEXITCODE -ne 0) { throw 'Mnemo UI compilation failed' }
        & ./.pio/host-tests/mnemo_ui_test.exe
    } else {
        & gcc -std=c11 -Wall -Wextra -Werror -I tests/aion_stubs -I src tests/mnemo_test.c src/core/mnemo_text.c -o .pio/host-tests/mnemo_test.exe
        if ($LASTEXITCODE -ne 0) { throw 'Mnemo compilation failed' }
        & ./.pio/host-tests/mnemo_test.exe
    }
    if ($LASTEXITCODE -ne 0) { throw 'Mnemo tests failed' }
} finally { Pop-Location }
