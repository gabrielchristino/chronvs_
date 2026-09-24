$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=gnu11 -Wall -Wextra -Werror -I tests/voice_stubs -I tests/weather_stubs -I tests/Relogio_stubs -I src tests/voice_service_test.c src/services/voice_lab_service.c -o .pio/host-tests/voice_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Vox host compilation failed' }
    & ./.pio/host-tests/voice_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Vox tests failed' }
} finally { Pop-Location }
