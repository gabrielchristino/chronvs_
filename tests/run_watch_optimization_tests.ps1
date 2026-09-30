$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
    $sources = @(rg --files "$lvglRoot/src" -g '*.c')
    $hashes = @()
    foreach ($variant in @('Og', 'O2')) {
        $exe = ".pio/host-tests/watch-optimization-$variant.exe"
        $pixels = ".pio/host-tests/watch-optimization-$variant.rgb565"
        $arguments = @('-std=c11', '-O2', '-DLV_ASSERT_HANDLER=abort();', '-include', 'stdlib.h',
            '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_CIRCLE_CACHE_SIZE=8', '-I', 'src',
            '-I', 'tests/Relogio_stubs', '-I', $lvglRoot, 'tests/watch_render_test.c') +
            $sources + @('-Wl,--wrap=lv_draw_mask_radius_init', '-lm', '-o', $exe)
        # Only watch_app.c changes optimization; LVGL stays O2 in both runs.
        if ($variant -eq 'Og') { $arguments += '-DCHRONVS_TEST_WATCH_BASELINE' }
        ($arguments | ForEach-Object { '"' + $_.Replace('\','/') + '"' }) |
            Set-Content '.pio/host-tests/watch-optimization.rsp'
        & gcc '@.pio/host-tests/watch-optimization.rsp'
        if ($LASTEXITCODE -ne 0) { throw "Watch $variant compilation failed" }
        & "./$exe" $pixels
        if ($LASTEXITCODE -ne 0) { throw "Watch $variant rendering failed" }
        $hashes += (Get-FileHash $pixels).Hash
    }
    if ($hashes[0] -ne $hashes[1]) { throw 'Watch pixels differ between Og and O2' }
    Write-Output "60 watch scenarios match: $($hashes[1])"
} finally { Pop-Location }
