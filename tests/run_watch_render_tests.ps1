$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Force '.pio/host-tests' | Out-Null
    $lvglRoot = '.vendor-reference/example/ESP-IDF-5.3.2/ESP32-S3-Touch-LCD-1.46-Test/components/lvgl__lvgl'
    $sources = @(rg --files "$lvglRoot/src" -g '*.c')
    $results = @()
    foreach ($size in @(4, 8)) {
        $exe = ".pio/host-tests/watch-render-$size.exe"
        $arguments = @('-std=c11', '-O2', '-DLV_ASSERT_HANDLER=abort();', '-include', 'stdlib.h',
            '-DLV_CONF_INCLUDE_SIMPLE', "-DLV_CIRCLE_CACHE_SIZE=$size", '-I', 'src',
            '-I', 'tests/Relogio_stubs', '-I', $lvglRoot, 'tests/watch_render_test.c') +
            $sources + @('-Wl,--wrap=lv_draw_mask_radius_init', '-lm', '-o', $exe)
        ($arguments | ForEach-Object { '"' + $_.Replace('\','/') + '"' }) |
            Set-Content '.pio/host-tests/watch-render-compile.rsp'
        & gcc '@.pio/host-tests/watch-render-compile.rsp'
        if ($LASTEXITCODE -ne 0) { throw 'Watch render compilation failed' }
        $result = & "./$exe" ".pio/host-tests/watch-render-$size.rgb565"
        if ($LASTEXITCODE -ne 0) { throw 'Watch render tests failed' }
        Write-Output $result
        $results += $result
    }
    $before = [regex]::Match($results[0], 'checksum=(\w+)').Groups[1].Value
    $after = [regex]::Match($results[1], 'checksum=(\w+)').Groups[1].Value
    if (!$before -or $before -ne $after) { throw 'Rendered pixels differ between cache sizes' }
    $beforeHash = (Get-FileHash '.pio/host-tests/watch-render-4.rgb565').Hash
    $afterHash = (Get-FileHash '.pio/host-tests/watch-render-8.rgb565').Hash
    if ($beforeHash -ne $afterHash) { throw 'Pixel stream SHA256 differs between cache sizes' }
    Write-Output "Pixel streams SHA256: $afterHash"
    $beforeMisses = [int][regex]::Match($results[0], 'large_misses=(\d+)').Groups[1].Value
    $afterMisses = [int][regex]::Match($results[1], 'large_misses=(\d+)').Groups[1].Value
    if ($afterMisses -ge $beforeMisses) { throw 'Larger cache did not reduce large-circle recalculation' }
} finally { Pop-Location }
