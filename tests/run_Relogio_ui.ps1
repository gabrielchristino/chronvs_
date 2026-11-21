param([switch]$System, [switch]$FlatWatch, [switch]$MotherCache, [switch]$Panels, [switch]$KeepCloseRedraw, [switch]$PanelCodeO2, [switch]$PanelNoContent, [switch]$PanelNoIcons, [switch]$LauncherIconCache, [switch]$GPSStandby, [switch]$DisplayBench, [switch]$BenchAuto, [switch]$CircleCache16, [switch]$SceneCache, [switch]$WatchReference, [switch]$PanelAutoTest, [switch]$SelectiveClip, [switch]$ArcClip, [switch]$ArcReference, [switch]$OpaqueIconCache, [switch]$SliderReference)
$ErrorActionPreference = 'Stop'
if ($OpaqueIconCache) { $LauncherIconCache = $true }
if ($GPSStandby -and -not $System) { throw 'GPSStandby requires System' }
if ($ArcClip -and -not $System) { throw 'ArcClip requires System' }
if ($SelectiveClip -and -not $System) { throw 'SelectiveClip requires System' }
if ($PanelAutoTest -and (-not $System -or $DisplayBench)) { throw 'PanelAutoTest requires System without DisplayBench' }
if ($DisplayBench -and -not $System) { throw 'DisplayBench requires System' }
if ($BenchAuto -and -not $DisplayBench) { throw 'BenchAuto requires DisplayBench' }
if ($SceneCache -and (-not $System -or $FlatWatch)) { throw 'SceneCache requires System and the real watch' }
if ($CircleCache16 -and -not $System) { throw 'CircleCache16 requires System' }
if ($WatchReference -and (-not $System -or $SceneCache)) { throw 'WatchReference requires System without SceneCache' }
# The normal System suite follows the adopted firmware. Historical display
# experiments retain their own controls; WatchReference explicitly selects it.
$standardWatch = $System -and -not ($WatchReference -or $FlatWatch -or $MotherCache -or $Panels -or $KeepCloseRedraw -or $PanelCodeO2 -or $PanelNoContent -or $PanelNoIcons -or $LauncherIconCache -or $DisplayBench -or $CircleCache16)
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
        $arguments += @('src/ui/circle_fill.c','src/ui/row_cache.c')
        $arguments += 'src/ui/arc_clip.c'
        if ($ArcClip) { $arguments += '-DCHRONVS_ARC_INNER_CLIP' }
        if ($ArcReference -or $ArcClip) { $arguments += '-DCHRONVS_BRIGHTNESS_ARC_REFERENCE' }
        if ($SliderReference) { $arguments += '-DCHRONVS_BRIGHTNESS_SLIDER_REFERENCE' }
        if ($SelectiveClip) { $arguments += @('-DCHRONVS_PANEL_SELECTIVE_CLIP','src/ui/circle_clip.c') }
        if ($PanelAutoTest) { $arguments += @('-DCHRONVS_PANEL_AUTOTEST','src/platform/panel_autotest.c') }
        if ($DisplayBench) { $arguments += @('-DCHRONVS_DISPLAY_BENCH','src/platform/display_bench.c') }
        if ($BenchAuto) { $arguments += '-DCHRONVS_DISPLAY_BENCH_AUTORUN' }
        if ($CircleCache16 -or $standardWatch) { $arguments += '-DLV_CIRCLE_CACHE_SIZE=16' }
        if ($SceneCache -or $standardWatch) { $arguments += '-DCHRONVS_WATCH_SCENE_CACHE' }
        if ($GPSStandby) { $arguments += '-DCHRONVS_GPS_STANDBY_TEST' }
        if ($LauncherIconCache) { $arguments += '-DCHRONVS_LAUNCHER_ICON_CACHE' }
        if ($OpaqueIconCache) { $arguments += '-DCHRONVS_LAUNCHER_ICON_OPAQUE' }
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
        $arguments += @('src/apps/Calendario_app.c','src/apps/Calendario_reminders.c','src/ui/Relogio_alert.c','src/ui/power_menu.c')
        $arguments += @('src/apps/gps_app.c','src/apps/run_app.c','tests/run_ui_service.c')
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
