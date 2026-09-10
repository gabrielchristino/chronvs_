$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=c11 -O0 -Wall -Wextra -I src -I tests/sound_stubs -I tests/aion_stubs tests/sound_test.c -o .pio/host-tests/sound_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Sound compilation failed' }
    & ./.pio/host-tests/sound_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Sound tests failed' }
} finally { Pop-Location }
