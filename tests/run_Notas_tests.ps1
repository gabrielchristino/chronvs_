param([switch]$UI)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    if ($UI) {
        $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
        $sources = @(rg --files "$lvglRoot/src" -g '*.c')
        $arguments = @('-std=c11', '-O0', '-DLV_CONF_INCLUDE_SIMPLE', '-I', 'src', '-I', 'tests/Relogio_stubs', '-I', $lvglRoot,
            'tests/Notas_ui_test.c', 'src/core/Notas_text.c', 'src/ui/Notas_font.c', 'src/ui/control_style.c', 'src/ui/app_input.c') + $sources + @('-o', '.pio/host-tests/Notas_ui_test.exe')
        ($arguments | ForEach-Object { '"' + $_.Replace('\','/') + '"' }) | Set-Content '.pio/host-tests/Notas-compile.rsp'
        & gcc '@.pio/host-tests/Notas-compile.rsp'
        if ($LASTEXITCODE -ne 0) { throw 'Notas UI compilation failed' }
        & ./.pio/host-tests/Notas_ui_test.exe
    } else {
        & gcc -std=c11 -Wall -Wextra -Werror -I tests/Relogio_stubs -I src tests/Notas_test.c src/core/Notas_text.c -o .pio/host-tests/Notas_test.exe
        if ($LASTEXITCODE -ne 0) { throw 'Notas compilation failed' }
        & ./.pio/host-tests/Notas_test.exe
    }
    if ($LASTEXITCODE -ne 0) { throw 'Notas tests failed' }
} finally { Pop-Location }
