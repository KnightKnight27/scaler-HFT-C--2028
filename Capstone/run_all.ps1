$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $root "build"
$resultsDir = Join-Path $root "results"
New-Item -ItemType Directory -Force -Path $buildDir, $resultsDir | Out-Null

$executable = Join-Path $buildDir "bench.exe"
Push-Location $root
try {
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic order_book.cpp ordered_map_order_book.cpp sorted_vector_order_book.cpp fixed_array_order_book.cpp bench.cpp -o $executable
    if ($LASTEXITCODE -ne 0) {
        throw "C++ build failed with exit code $LASTEXITCODE"
    }

    $verifyPath = Join-Path $resultsDir "verify.txt"
    & $executable --verify 2>&1 | ForEach-Object { $_.ToString() } | Set-Content -Encoding utf8 -Path $verifyPath
    if ($LASTEXITCODE -ne 0) {
        throw "Correctness verification failed with exit code $LASTEXITCODE"
    }
    Get-Content $verifyPath

    foreach ($implementation in @("A", "B", "C")) {
        $outputPath = Join-Path $resultsDir "bench_$implementation.txt"
        & $executable --impl $implementation 2>&1 | ForEach-Object { $_.ToString() } | Set-Content -Encoding utf8 -Path $outputPath
        if ($LASTEXITCODE -ne 0) {
            throw "Benchmark $implementation failed with exit code $LASTEXITCODE"
        }
        Get-Content $outputPath
    }

    foreach ($implementation in @("A", "B", "C")) {
        @(
            "NOT COLLECTED: Linux perf is not available in this Windows run."
            "For genuine perf stat and perf report output, run ./run_all.sh on Linux."
            "No perf counter values are estimated or substituted."
        ) | Set-Content -Encoding utf8 (Join-Path $resultsDir "perf_stat_$implementation.txt")
        @(
            "NOT COLLECTED: Linux perf call-graph profiling is not available in this Windows run."
            "For genuine perf report output, run ./run_all.sh on Linux."
            "No profile data is estimated or substituted."
        ) | Set-Content -Encoding utf8 (Join-Path $resultsDir "perf_report_$implementation.txt")
    }
}
finally {
    Pop-Location
}
