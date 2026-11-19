$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -I src tests/gps_test.c src/services/gps_data.c src/core/calendar.c -lm -o .pio/host-tests/gps_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'GPS compilation failed' }
    & ./.pio/host-tests/gps_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'GPS parser tests failed' }
    & gcc -std=c11 -Wall -Wextra -Werror -I tests/gps_stubs -I src tests/gps_service_test.c src/services/gps_data.c src/services/agnss_data.c src/core/calendar.c -lm -o .pio/host-tests/gps_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'GPS UART compilation failed' }
    & ./.pio/host-tests/gps_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'GPS UART tests failed' }
    & gcc -std=c11 -Wall -Wextra -Werror -DCHRONVS_GPS_STANDBY_TEST -I tests/gps_stubs -I src tests/gps_service_test.c src/services/gps_data.c src/services/agnss_data.c src/core/calendar.c -lm -o .pio/host-tests/gps_standby_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'GPS standby compilation failed' }
    & ./.pio/host-tests/gps_standby_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'GPS standby tests failed' }
} finally { Pop-Location }
