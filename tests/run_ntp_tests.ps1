$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -I tests/ntp_stubs -I tests/weather_stubs -I tests/Relogio_stubs -I src tests/ntp_test.c -o .pio/host-tests/ntp_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'NTP test compilation failed' }
    & ./.pio/host-tests/ntp_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'NTP tests failed' }
} finally { Pop-Location }
