param([switch]$System, [switch]$FlatWatch, [switch]$MotherCache, [switch]$Panels, [switch]$KeepCloseRedraw, [switch]$PanelCodeO2, [switch]$PanelNoContent, [switch]$PanelNoIcons, [switch]$LauncherIconCache)
$ErrorActionPreference = 'Stop'
if ($LauncherIconCache -and (-not $System -or $PanelNoContent -or $PanelNoIcons)) { throw 'LauncherIconCache requires System with full icons' }
if ($MotherCache -and (-not $System -or $FlatWatch)) { throw 'MotherCache requires System with the orbital watch' }
if (($Panels -or $KeepCloseRedraw) -and -not $System) { throw 'Panel diagnostics require System' }
if ($PanelCodeO2 -and -not $System) { throw 'PanelCodeO2 requires System' }
if ($PanelNoContent -and -not $System) { throw 'PanelNoContent requires System' }
if ($PanelNoIcons -and (-not $System -or $PanelNoContent)) { throw 'PanelNoIcons requires System without PanelNoContent' }
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
    $sources = @(rg --files "$lvglRoot/src" -g '*.c')
    # Fail host assertions immediately instead of LVGL's embedded infinite loop.
    $arguments = @('-std=c11', '-O0', '-g', '-DLV_ASSERT_HANDLER=abort();', '-include', 'stdlib.h', '-DLV_CONF_INCLUDE_SIMPLE', '-I', 'src', '-I', 'tests/Relogio_stubs', '-I', $lvglRoot,
        'tests/Relogio_ui_test.c', 'src/services/Relogio_service.c', 'src/core/calendar.c', 'src/core/Notas_text.c', 'src/ui/Notas_font.c', 'src/ui/Relogio_widgets.c', 'src/ui/control_style.c') + $sources + @('-o', '.pio/host-tests/Relogio_ui_test.exe')
    $executable = '.pio/host-tests/Relogio_ui_test.exe'
    if ($System) {
        if ($LauncherIconCache) { $arguments += '-DCHRONVS_LAUNCHER_ICON_CACHE' }
        if ($PanelCodeO2) { $arguments += '-DCHRONVS_PANEL_CODE_O2' }
        if ($PanelNoContent) { $arguments += '-DCHRONVS_PANEL_NO_CONTENT' }
        if ($PanelNoIcons) { $arguments += '-DCHRONVS_PANEL_NO_ICONS' }
        if ($Panels) {
            $arguments += '-DCHRONVS_PANEL_PROFILE'
            if (-not $KeepCloseRedraw) { $arguments += '-DCHRONVS_PANEL_NO_CLOSE_REDRAW' }
        }
        if ($MotherCache) { $arguments += @('-DCHRONVS_WATCH_MOTHER_CACHE') }
        if ($FlatWatch) { $arguments += @('-DCHRONVS_WATCH_FLAT_BACKGROUND') }
        $arguments += @('-Wl,--wrap=lv_draw_rect')
        $arguments += @('-DCHRONVS_DISPLAY_PROFILE')
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
        $arguments += @('src/apps/weather_app.c','src/ui/weather_icon.c','src/services/weather_data.c',"$jsonRoot/cJSON.c",'-I',$jsonRoot)
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
