param([switch]$System)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
    $sources = @(rg --files "$lvglRoot/src" -g '*.c')
    $arguments = @('-std=c11', '-O0', '-DLV_CONF_INCLUDE_SIMPLE', '-I', 'src', '-I', 'tests/aion_stubs', '-I', $lvglRoot,
        'tests/aion_ui_test.c', 'src/services/aion_service.c', 'src/ui/aion_widgets.c', 'src/ui/control_style.c') + $sources + @('-o', '.pio/host-tests/aion_ui_test.exe')
    $executable = '.pio/host-tests/aion_ui_test.exe'
    if ($System) {
        $executable = '.pio/host-tests/system_ui_test.exe'
        $arguments = $arguments | ForEach-Object {
            if ($_ -eq 'tests/aion_ui_test.c') { 'tests/system_ui_test.c' }
            elseif ($_ -eq '.pio/host-tests/aion_ui_test.exe') { $executable }
            else { $_ }
        }
        $arguments += @('src/apps/watch_app.c','src/apps/app_list_app.c','src/apps/app_catalog.c',
            'src/apps/aion_app.c','src/apps/aion_pages.c','src/apps/calculator_app.c','src/core/calculator.c','src/core/app_manager.c','src/ui/system_ui.c',
            'src/apps/mnemo_app.c','src/core/mnemo_text.c','src/services/mnemo_service.c','src/ui/mnemo_font.c','src/ui/app_input.c')
        $arguments += @('-lm')
        $jsonRoot = Join-Path $env:USERPROFILE '.platformio/packages/framework-espidf/components/json/cJSON'
        $arguments += @('src/apps/weather_app.c','src/ui/weather_icon.c','src/services/weather_data.c',"$jsonRoot/cJSON.c",'-I',$jsonRoot)
        $arguments += @('src/apps/hemera_app.c','src/core/calendar.c')
        $arguments += @('src/platform/lvgl_memory.c','-DLV_MEM_POOL_ALLOC=chronvs_lvgl_pool_alloc',
            '-include','src/platform/lvgl_memory.h')
    }
    # GCC response file avoids the Windows command-line length limit.
    ($arguments | ForEach-Object { '"' + $_.Replace('\','/') + '"' }) | Set-Content '.pio/host-tests/ui-compile.rsp'
    & gcc '@.pio/host-tests/ui-compile.rsp'
    if ($LASTEXITCODE -ne 0) { throw 'LVGL host compilation failed' }
    & "./$executable"
    if ($LASTEXITCODE -ne 0) { throw 'Aion UI tests failed' }
} finally { Pop-Location }
