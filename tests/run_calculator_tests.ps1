$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=c11 -O0 -Wall -Wextra -Werror -I src tests/calculator_test.c `
        src/core/calculator.c -lm -o .pio/host-tests/calculator_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Calculator compilation failed' }
    & ./.pio/host-tests/calculator_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Calculator tests failed' }
} finally {
    Pop-Location
}
