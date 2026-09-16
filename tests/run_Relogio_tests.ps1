$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -I tests/Relogio_stubs -I src tests/Relogio_service_test.c src/services/Relogio_service.c src/core/calendar.c src/core/Notas_text.c -o .pio/host-tests/Relogio_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed' }
    & ./.pio/host-tests/Relogio_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Relogio service tests failed' }
} finally { Pop-Location }
