$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=c11 -Wall -Wextra -Werror -I tests/gps_stubs -I src tests/run_service_test.c -lm -o .pio/host-tests/run_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Run recorder compilation failed' }
    & ./.pio/host-tests/run_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Run recorder tests failed' }
    & 'C:\Users\gabri\.platformio\penv\Scripts\python.exe' -m unittest discover -s tests -p 'test_export_run.py'
    if ($LASTEXITCODE -ne 0) { throw 'GPX export tests failed' }
} finally { Pop-Location }
