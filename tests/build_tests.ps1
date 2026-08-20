# ============================================================
# tests/build_tests.ps1 - Build and run the unit tests
# Cross-platform: works on Windows PowerShell 5.1 and on
# PowerShell Core (pwsh) on Linux/macOS.
# Requirements: g++ (override with the CXX/GCC env var)
# Usage:
#   powershell -ExecutionPolicy Bypass -File tests/build_tests.ps1
# ============================================================
param(
    [switch]$SkipRun
)

$ErrorActionPreference = "Continue"

# ---- OS detection ----
if ($PSVersionTable.PSEdition -eq 'Core') {
    $isWindows = $env:OS -match 'Windows'
} else {
    $isWindows = $true
}

# ---- compiler selection ----
if ($env:CXX) {
    $gcc = $env:CXX
} elseif ($env:GCC) {
    $gcc = $env:GCC
} elseif ($isWindows) {
    $gcc = "D:\MinDW w64\mingw64\bin\g++.exe"
} else {
    $gcc = "g++"
}

if ($isWindows) {
    if (-not (Test-Path $gcc)) {
        Write-Host "[ERROR] g++ not found at: $gcc" -ForegroundColor Red
        Write-Host "Set the GCC/CXX environment variable or edit this script."
        exit 1
    }
} else {
    if (-not (Get-Command $gcc -ErrorAction SilentlyContinue)) {
        Write-Host "[ERROR] C++ compiler not found: $gcc" -ForegroundColor Red
        Write-Host "Install g++ or set the CXX environment variable."
        exit 1
    }
}

$root  = Split-Path -Parent $PSScriptRoot
$src   = Join-Path $root "src"
$tests = $PSScriptRoot
$build = Join-Path $tests "build"
$exeName = if ($isWindows) { "server_tests.exe" } else { "server_tests" }
$out   = Join-Path $build $exeName

# 说明：文件路径统一使用正斜杠 /，Windows 的 g++ 与 Linux 的 g++ 均接受。
$srcFiles = @(
    # ---- domain/models ----
    "$src/domain/models/dish.cpp",
    "$src/domain/models/merchant.cpp",
    "$src/domain/models/order.cpp",
    "$src/domain/models/order_item.cpp",
    "$src/domain/models/user.cpp",
    # ---- domain/services ----
    "$src/domain/services/price_calculator.cpp",
    "$src/domain/services/order_status_machine.cpp",
    # ---- domain/value_objects ----
    "$src/domain/value_objects/address.cpp",
    "$src/domain/value_objects/money.cpp",
    # ---- infrastructure/common ----
    "$src/infrastructure/common/config.cpp",
    "$src/infrastructure/common/exception.cpp",
    "$src/infrastructure/common/logger.cpp",
    # ---- infrastructure/session ----
    "$src/infrastructure/session/session_manager.cpp",
    "$src/infrastructure/session/session_store.cpp",
    # ---- application ----
    "$src/application/auth_service.cpp",
    "$src/application/dish_service.cpp",
    "$src/application/order_service.cpp",
    # ---- presentation/http ----
    "$src/presentation/http/http_parser.cpp",
    "$src/presentation/http/http_request.cpp",
    "$src/presentation/http/http_response.cpp",
    "$src/presentation/http/http_router.cpp",
    # ---- middleware ----
    "$src/middleware/auth_middleware.cpp",
    "$src/middleware/logging_middleware.cpp",
    "$src/middleware/middleware.cpp",
    "$src/middleware/rate_limit_middleware.cpp",
    # ---- presentation/handlers ----
    "$src/presentation/handlers/auth_handler.cpp",
    "$src/presentation/handlers/dish_handler.cpp",
    "$src/presentation/handlers/order_handler.cpp"
)

$testFiles = @(
    "$tests/test_main.cpp",
    "$tests/domain/test_money.cpp",
    "$tests/domain/test_address.cpp",
    "$tests/domain/test_order_item.cpp",
    "$tests/domain/test_order.cpp",
    "$tests/domain/test_user.cpp",
    "$tests/domain/test_merchant.cpp",
    "$tests/domain/test_dish.cpp",
    "$tests/domain/test_price_calculator.cpp",
    "$tests/domain/test_order_status_machine.cpp",
    "$tests/infrastructure/test_config.cpp",
    "$tests/infrastructure/test_exception.cpp",
    "$tests/infrastructure/test_session.cpp",
    "$tests/application/test_auth_service.cpp",
    "$tests/application/test_dish_service.cpp",
    "$tests/application/test_order_service.cpp",
    "$tests/presentation/test_http_request.cpp",
    "$tests/presentation/test_http_response.cpp",
    "$tests/presentation/test_http_parser.cpp",
    "$tests/presentation/test_http_router.cpp",
    "$tests/presentation/test_handlers.cpp",
    "$tests/middleware/test_middleware.cpp"
)

New-Item -ItemType Directory -Force -Path $build | Out-Null

Write-Host "[1/2] Compiling test program..."
$log = Join-Path $build "compile.log"
& $gcc -std=c++17 -Wall -Wextra "-I$src" "-I$tests" @($srcFiles + $testFiles) -o $out 2>&1 | Tee-Object -FilePath $log

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Compilation failed. See $log" -ForegroundColor Red
    exit 1
}

if ($SkipRun) {
    Write-Host "[OK] Build finished: $out"
    exit 0
}

Write-Host ""
Write-Host "[2/2] Running tests..."
& $out
exit $LASTEXITCODE
