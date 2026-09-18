param([switch]$System)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
    $sources = @(rg --files "$lvglRoot/src" -g '*.c')
    $arguments = @('-std=c11', '-O0', '-DLV_CONF_INCLUDE_SIMPLE', '-I', 'src', '-I', 'tests/Relogio_stubs', '-I', $lvglRoot,
        'tests/Relogio_ui_test.c', 'src/services/Relogio_service.c', 'src/core/calendar.c', 'src/core/Notas_text.c', 'src/ui/Notas_font.c', 'src/ui/Relogio_widgets.c', 'src/ui/control_style.c') + $sources + @('-o', '.pio/host-tests/Relogio_ui_test.exe')
    $executable = '.pio/host-tests/Relogio_ui_test.exe'
    if ($System) {
        $executable = '.pio/host-tests/system_ui_test.exe'
        $arguments = $arguments | ForEach-Object {
            if ($_ -eq 'tests/Relogio_ui_test.c') { 'tests/system_ui_test.c' }
            elseif ($_ -eq '.pio/host-tests/Relogio_ui_test.exe') { $executable }
            else { $_ }
        }
        $arguments += @('src/apps/watch_app.c','src/apps/app_list_app.c','src/apps/app_catalog.c',
            'src/apps/Relogio_app.c','src/apps/Relogio_pages.c','src/apps/calculator_app.c','src/core/calculator.c','src/core/app_manager.c','src/ui/system_ui.c',
            'src/apps/Notas_app.c','src/services/Notas_service.c','src/ui/app_input.c')
        $arguments += @('-lm')
        $jsonRoot = Join-Path $env:USERPROFILE '.platformio/packages/framework-espidf/components/json/cJSON'
        $arguments += @('src/apps/weather_app.c','src/ui/weather_icon.c','src/ui/weather_art.c','src/ui/weather_face.c','src/services/weather_data.c',"$jsonRoot/cJSON.c",'-I',$jsonRoot)
        $arguments += @('src/apps/Calendario_app.c','src/apps/Calendario_reminders.c','src/ui/Relogio_alert.c')
        $arguments += @('src/platform/lvgl_memory.c','-DLV_MEM_POOL_ALLOC=chronvs_lvgl_pool_alloc',
            '-include','src/platform/lvgl_memory.h')
    }
    # GCC response file avoids the Windows command-line length limit.
    ($arguments | ForEach-Object { '"' + $_.Replace('\','/') + '"' }) | Set-Content '.pio/host-tests/ui-compile.rsp'
    & gcc '@.pio/host-tests/ui-compile.rsp'
    if ($LASTEXITCODE -ne 0) { throw 'LVGL host compilation failed' }
    & "./$executable"
    if ($LASTEXITCODE -ne 0) { throw 'Relogio UI tests failed' }
} finally { Pop-Location }
