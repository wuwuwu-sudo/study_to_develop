#!/usr/bin/env bash
# ============================================================
# tests/run_tests.sh - Build and run the unit tests (Linux/macOS)
# Requirements: g++ (override with CXX env var, e.g. CXX=clang++)
# Usage:
#   bash tests/run_tests.sh
#   # or after chmod +x:
#   ./tests/run_tests.sh
# ============================================================
set -euo pipefail

CXX="${CXX:-g++}"

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "[ERROR] C++ compiler not found: $CXX"
    echo "Install g++ (sudo apt install g++) or set the CXX environment variable."
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
SRC="$ROOT/src"
TESTS="$SCRIPT_DIR"
BUILD="$TESTS/build"
OUT="$BUILD/server_tests"

mkdir -p "$BUILD"

SRC_FILES=(
    # ---- domain/models ----
    "$SRC/domain/models/dish.cpp"
    "$SRC/domain/models/merchant.cpp"
    "$SRC/domain/models/order.cpp"
    "$SRC/domain/models/order_item.cpp"
    "$SRC/domain/models/user.cpp"
    # ---- domain/services ----
    "$SRC/domain/services/price_calculator.cpp"
    "$SRC/domain/services/order_status_machine.cpp"
    # ---- domain/value_objects ----
    "$SRC/domain/value_objects/address.cpp"
    "$SRC/domain/value_objects/money.cpp"
    # ---- infrastructure/common ----
    "$SRC/infrastructure/common/config.cpp"
    "$SRC/infrastructure/common/exception.cpp"
    "$SRC/infrastructure/common/logger.cpp"
    # ---- infrastructure/session ----
    "$SRC/infrastructure/session/session_manager.cpp"
    "$SRC/infrastructure/session/session_store.cpp"
    # ---- application ----
    "$SRC/application/auth_service.cpp"
    "$SRC/application/dish_service.cpp"
    "$SRC/application/order_service.cpp"
    # ---- presentation/http ----
    "$SRC/presentation/http/http_parser.cpp"
    "$SRC/presentation/http/http_request.cpp"
    "$SRC/presentation/http/http_response.cpp"
    "$SRC/presentation/http/http_router.cpp"
    # ---- middleware ----
    "$SRC/middleware/auth_middleware.cpp"
    "$SRC/middleware/logging_middleware.cpp"
    "$SRC/middleware/middleware.cpp"
    "$SRC/middleware/rate_limit_middleware.cpp"
    # ---- presentation/handlers ----
    "$SRC/presentation/handlers/auth_handler.cpp"
    "$SRC/presentation/handlers/dish_handler.cpp"
    "$SRC/presentation/handlers/order_handler.cpp"
)

TEST_FILES=(
    "$TESTS/test_main.cpp"
    "$TESTS/domain/test_money.cpp"
    "$TESTS/domain/test_address.cpp"
    "$TESTS/domain/test_order_item.cpp"
    "$TESTS/domain/test_order.cpp"
    "$TESTS/domain/test_user.cpp"
    "$TESTS/domain/test_merchant.cpp"
    "$TESTS/domain/test_dish.cpp"
    "$TESTS/domain/test_price_calculator.cpp"
    "$TESTS/domain/test_order_status_machine.cpp"
    "$TESTS/infrastructure/test_config.cpp"
    "$TESTS/infrastructure/test_exception.cpp"
    "$TESTS/infrastructure/test_session.cpp"
    "$TESTS/application/test_auth_service.cpp"
    "$TESTS/application/test_dish_service.cpp"
    "$TESTS/application/test_order_service.cpp"
    "$TESTS/presentation/test_http_request.cpp"
    "$TESTS/presentation/test_http_response.cpp"
    "$TESTS/presentation/test_http_parser.cpp"
    "$TESTS/presentation/test_http_router.cpp"
    "$TESTS/presentation/test_handlers.cpp"
    "$TESTS/middleware/test_middleware.cpp"
)

echo "[1/2] Compiling test program..."
"$CXX" -std=c++17 -Wall -Wextra -I"$SRC" -I"$TESTS" \
    "${SRC_FILES[@]}" "${TEST_FILES[@]}" -o "$OUT"

echo ""
echo "[2/2] Running tests..."
"$OUT"
