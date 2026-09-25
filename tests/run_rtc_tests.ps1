$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -include esp_err.h -I tests/ntp_stubs -I tests/weather_stubs -I tests/Relogio_stubs -I src tests/rtc_refresh_test.c src/services/Relogio_service.c src/core/calendar.c src/core/Notas_text.c -o .pio/host-tests/rtc_refresh_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'RTC test compilation failed' }
    & ./.pio/host-tests/rtc_refresh_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'RTC tests failed' }
} finally { Pop-Location }
