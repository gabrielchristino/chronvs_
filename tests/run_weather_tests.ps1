param([switch]$UI)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    $jsonRoot = Join-Path $env:USERPROFILE '.platformio/packages/framework-espidf/components/json/cJSON'
    if (!(Test-Path "$jsonRoot/cJSON.c")) { throw 'ESP-IDF cJSON sources not found' }
    $arguments = @('-std=c11','-O0','-Wall','-Wextra','-Werror','-I','tests/weather_stubs',
        '-I','tests/aion_stubs','-I','src','-I',$jsonRoot,'tests/weather_test.c',
        'src/services/weather_data.c',"$jsonRoot/cJSON.c",'-lm','-o','.pio/host-tests/weather_test.exe')
    & gcc @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Weather host compilation failed' }
    & ./.pio/host-tests/weather_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Weather service tests failed' }
    & gcc -std=c11 -Wall -Wextra -Werror -I tests/weather_stubs -I tests/aion_stubs -I src tests/wifi_session_test.c -o .pio/host-tests/wifi_session_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi session compilation failed' }
    & ./.pio/host-tests/wifi_session_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Wi-Fi session tests failed' }
    if ($UI) {
        $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
        $sources = @(rg --files "$lvglRoot/src" -g '*.c')
        $arguments = @('-std=c11','-O0','-DLV_CONF_INCLUDE_SIMPLE','-I','src','-I','tests/aion_stubs',
            '-I',$lvglRoot,'-I',$jsonRoot,'tests/weather_ui_test.c','src/services/weather_data.c',
            'src/ui/aion_widgets.c','src/ui/control_style.c','src/ui/app_input.c','src/ui/mnemo_font.c','src/ui/weather_icon.c',
            "$jsonRoot/cJSON.c") + $sources + @('-lm','-o','.pio/host-tests/weather_ui_test.exe')
        ($arguments | ForEach-Object { '"' + $_.Replace('\','/') + '"' }) | Set-Content '.pio/host-tests/weather-ui-compile.rsp'
        & gcc '@.pio/host-tests/weather-ui-compile.rsp'
        if ($LASTEXITCODE -ne 0) { throw 'Weather UI compilation failed' }
        & ./.pio/host-tests/weather_ui_test.exe
        if ($LASTEXITCODE -ne 0) { throw 'Weather UI tests failed' }
    }
} finally { Pop-Location }
