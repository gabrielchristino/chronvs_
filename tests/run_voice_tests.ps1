param([string]$Python = (Join-Path $env:USERPROFILE '.platformio/penv/Scripts/python.exe'))
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    & gcc -std=gnu11 -Wall -Wextra -Werror -I tests/voice_stubs -I tests/weather_stubs -I tests/Relogio_stubs -I src tests/voice_service_test.c src/services/voice_lab_service.c -o .pio/host-tests/voice_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Vox host compilation failed' }
    & ./.pio/host-tests/voice_service_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Vox tests failed' }
    & gcc -std=gnu11 -Wall -Wextra -Werror -DCHRONVS_VOX_CAPTURE -I tests/voice_stubs -I tests/weather_stubs -I tests/Relogio_stubs -I src tests/voice_capture_test.c src/services/voice_lab_service.c -o .pio/host-tests/voice_capture_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Vox capture host compilation failed' }
    & ./.pio/host-tests/voice_capture_test.exe | Set-Content -Encoding ascii .pio/host-tests/voice_capture.txt
    if ($LASTEXITCODE -ne 0) { throw 'Vox capture tests failed' }
    & $Python tests/voice_dataset_test.py
    if ($LASTEXITCODE -ne 0) { throw 'Vox PCM/dataset tests failed' }
    & $Python tests/voice_logging_test.py
    if ($LASTEXITCODE -ne 0) { throw 'Vox console/log policy tests failed' }
    & $Python tests/voice_serial_test.py
    if ($LASTEXITCODE -ne 0) { throw 'Vox serial reconnection tests failed' }
    & $Python tests/voice_hands_free_test.py
    if ($LASTEXITCODE -ne 0) { throw 'Vox hands-free collection tests failed' }
} finally { Pop-Location }
