$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -I src tests/agnss_data_test.c src/services/agnss_data.c -lm -o .pio/host-tests/agnss_data_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'AGNSS data compilation failed' }
    & ./.pio/host-tests/agnss_data_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'AGNSS data tests failed' }
    & gcc -std=c11 -Wall -Wextra -Werror -I src tests/agnss_orbits_test.c src/services/agnss_data.c src/services/agnss_orbits.c src/services/agnss_rinex.c -lm -o .pio/host-tests/agnss_orbits_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'AGNSS orbit compilation failed' }
    & ./.pio/host-tests/agnss_orbits_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'AGNSS orbit tests failed' }
    New-Item -ItemType Directory -Force '.pio/host-tests/agnss-sd' | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -I tests/agnss_stubs -I tests/weather_stubs -I tests/gps_stubs -I src tests/agnss_sources_test.c src/services/agnss_data.c src/services/agnss_orbits.c -lm -o .pio/host-tests/agnss_sources_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'AGNSS sources compilation failed' }
    & ./.pio/host-tests/agnss_sources_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'AGNSS sources tests failed' }
    & gcc -std=c11 -Wall -Wextra -Werror -I tests/agnss_stubs -I tests/weather_stubs -I tests/gps_stubs -I src tests/agnss_service_test.c src/services/agnss_data.c -lm -o .pio/host-tests/agnss_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'AGNSS service compilation failed' }
    & ./.pio/host-tests/agnss_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'AGNSS service tests failed' }
} finally { Pop-Location }
