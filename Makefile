# ============================================================================
# 编译器和选项
# ============================================================================
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wpedantic
CXXFLAGS_DEBUG = -g -O0 -DDEBUG
CXXFLAGS_RELEASE = -O2 -DNDEBUG

# 包含路径
INCLUDES = -I$(SRC_DIR)

# 库依赖
LIBS = -lpthread -lsqlite3 -lssl -lcrypto

# ============================================================================
# 目录结构
# ============================================================================
SRC_DIR = src
BUILD_DIR = build

# ============================================================================
# 源文件和目标文件
# ============================================================================
# 直接查找所有 .cpp 文件（包括 src 根目录和所有子目录），自动去重
SOURCES = $(sort $(shell find $(SRC_DIR) -name "*.cpp" -type f))
# 生成目标文件路径
OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
# 生成依赖文件
DEPS = $(patsubst $(BUILD_DIR)/%.o,$(BUILD_DIR)/%.d,$(OBJECTS))

# 可执行文件名（直接在项目根目录生成）
TARGET = webserver

# ============================================================================
# 主要目标
# ============================================================================
.PHONY: all clean debug release run test info help

all: release

# 调试版本
debug: CXXFLAGS += $(CXXFLAGS_DEBUG)
debug: $(TARGET)

# 发布版本
release: CXXFLAGS += $(CXXFLAGS_RELEASE)
release: $(TARGET)

# 运行服务器
run: release
	@./$(TARGET)

# 清理
clean:
	@rm -rf $(BUILD_DIR) $(TARGET)
	@echo "Clean completed"

# 显示构建信息
info:
	@echo "=========== Build Information ==========="
	@echo "C++ Compiler: $(CXX)"
	@echo "C++ Flags: $(CXXFLAGS)"
	@echo "Include Paths: $(INCLUDES)"
	@echo "Link Libraries: $(LIBS)"
	@echo "Source Directory: $(SRC_DIR)"
	@echo "Build Directory: $(BUILD_DIR)"
	@echo "Target: $(TARGET)"
	@echo "Sources: $(SOURCES)"
	@echo "Objects: $(OBJECTS)"
	@echo "========================================"

# ============================================================================
# 链接规则
# ============================================================================
$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	@$(CXX) $(OBJECTS) $(LIBS) -o $@
	@echo "Build completed! Executable: ./$(TARGET)"

# ============================================================================
# 编译规则
# ============================================================================
# 编译每个 .cpp 文件为 .o 文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

# 创建必要的目录
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# ============================================================================
# 自动依赖包含
# ============================================================================
-include $(DEPS)

# ============================================================================
# 检查 nlohmann/json 库是否存在（使用标准路径）
# ============================================================================
check_json:
	@echo "Checking nlohmann/json library..."
	@if [ -f "/usr/include/nlohmann/json.hpp" ] || [ -f "/usr/local/include/nlohmann/json.hpp" ] || [ -f "/opt/homebrew/include/nlohmann/json.hpp" ]; then \
		echo "✓ nlohmann/json found"; \
	else \
		echo "✗ nlohmann/json not found in standard locations"; \
		echo "Please install nlohmann-json with: sudo apt install nlohmann-json3-dev (Ubuntu)"; \
		echo "or: brew install nlohmann-json (macOS)"; \
		exit 1; \
	fi

# 在编译前检查 json 库
$(TARGET): check_json

# ============================================================================
# 帮助信息
# ============================================================================
help:
	@echo "Usage: make [target]"
	@echo ""
	@echo "Targets:"
	@echo "  all              Build release version (default)"
	@echo "  debug            Build debug version with symbols and no optimization"
	@echo "  release          Build release version with optimization"
	@echo "  run              Build and run the server"
	@echo "  clean            Remove build artifacts"
	@echo "  info             Show build information"
	@echo "  check_json       Check if nlohmann/json is installed"
	@echo "  help             Show this help message"
	@echo ""
	@echo "Environment variables:"
	@echo "  CXX               C++ compiler (default: g++)"
	@echo "  CXXFLAGS          Additional compiler flags"
	@echo "  LIBS              Additional libraries"
	@echo ""
	@echo "Example:"
	@echo "  make CXX=clang++"
	@echo ""
	@echo "After build, run: ./webserver"