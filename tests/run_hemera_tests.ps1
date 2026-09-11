$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
    $sources = @(rg --files "$lvglRoot/src" -g '*.c')
    $arguments = @('-std=c11','-O0','-DLV_CONF_INCLUDE_SIMPLE','-I','src','-I','tests/aion_stubs',
        '-I',$lvglRoot,'tests/hemera_ui_test.c','src/core/calendar.c','src/ui/aion_widgets.c',
        'src/ui/control_style.c','src/ui/app_input.c','src/ui/mnemo_font.c','src/apps/hemera_reminders.c',
        'src/services/aion_service.c','src/core/mnemo_text.c') + $sources + @('-lm','-o','.pio/host-tests/hemera_ui_test.exe')
    ($arguments | ForEach-Object { '"' + $_.Replace('\','/') + '"' }) | Set-Content '.pio/host-tests/hemera-ui-compile.rsp'
    & gcc '@.pio/host-tests/hemera-ui-compile.rsp'
    if ($LASTEXITCODE -ne 0) { throw 'Hemera UI compilation failed' }
    & ./.pio/host-tests/hemera_ui_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Hemera tests failed' }
} finally { Pop-Location }
