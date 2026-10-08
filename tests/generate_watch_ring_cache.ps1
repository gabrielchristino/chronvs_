param([switch]$Mother)
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
    $sources = @(rg --files "$lvglRoot/src" -g '*.c')
    $kind = if ($Mother) { 'mother' } else { 'ring' }
    $exe = ".pio/host-tests/generate-watch-$kind-cache.exe"
    $arguments = @('-std=c11', '-O2', '-DLV_ASSERT_HANDLER=abort();', '-include', 'stdlib.h',
        '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_CIRCLE_CACHE_SIZE=8', '-I', 'src',
        '-I', 'tests/Relogio_stubs', '-I', $lvglRoot, "tests/generate_watch_${kind}_cache.c") +
        $sources + @('-Wl,--wrap=lv_draw_mask_radius_init', '-lm', '-o', $exe)
    ($arguments | ForEach-Object { '"' + $_.Replace('\','/') + '"' }) |
        Set-Content '.pio/host-tests/generate-watch-ring-cache.rsp'
    & gcc '@.pio/host-tests/generate-watch-ring-cache.rsp'
    if ($LASTEXITCODE -ne 0) { throw "$kind cache generator compilation failed" }
    & "./$exe" ".pio/host-tests/watch_${kind}_cache.h"
    if ($LASTEXITCODE -ne 0) { throw "$kind cache generation failed" }
    $generated = (Get-Content ".pio/host-tests/watch_${kind}_cache.h" -Raw).Replace("`r`n","`n").TrimEnd()
    $checkedIn = (Get-Content "src/apps/watch_${kind}_cache.h" -Raw).Replace("`r`n","`n").TrimEnd()
    if ($generated -ne $checkedIn) { throw "Regenerated cache differs; review .pio/host-tests/watch_${kind}_cache.h" }
    Write-Output 'Generated cache matches the checked-in source.'
} finally { Pop-Location }
