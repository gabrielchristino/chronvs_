$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -I tests/aion_stubs -I src tests/aion_service_test.c src/services/aion_service.c -o .pio/host-tests/aion_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed' }
    & ./.pio/host-tests/aion_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Aion service tests failed' }
} finally { Pop-Location }
