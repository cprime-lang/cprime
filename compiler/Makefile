# =============================================================================
# C-Prime Compiler — Makefile
# Builds cpc using the bootstrap compiler, or the real cpc if available.
# =============================================================================

BOOTSTRAP    = ../build/bootstrap/cpc-bootstrap
CPC          = ../build/compiler/cpc
BUILD_DIR    = ../build/compiler
TARGET       = $(BUILD_DIR)/cpc

# All .cp source files in the compiler
SRCS = src/main.cp \
       src/lexer/lexer.cp \
       src/parser/parser.cp \
       src/ast/ast.cp \
       src/semantic/semantic.cp \
       src/semantic/borrow_check.cp \
       src/optimizer/optimizer.cp \
       src/codegen/codegen.cp \
       src/codegen/x86_64/emit.cp \
       src/codegen/x86_64/elf.cp \
       src/codegen/arm64/emit.cp \
       src/utils/error.cp \
       src/utils/arena.cp

.PHONY: all clean test stage2 stage3

# Phase 2: bootstrap → stage1 compiler
all: stage1

stage1: $(BOOTSTRAP)
	@mkdir -p $(BUILD_DIR)
	@echo "[cpc] Stage 1: Bootstrap compiler → cpc"
	$(BOOTSTRAP) src/main.cp -o $(TARGET)
	@echo "[cpc] Stage 1 complete: $(TARGET)"

# Phase 3: stage1 compiler compiles itself (self-hosting)
stage2: stage1
	@echo "[cpc] Stage 2: cpc compiles itself..."
	$(TARGET) src/main.cp -o $(BUILD_DIR)/cpc-stage2
	@echo "[cpc] Stage 2 complete."

stage3: stage2
	@echo "[cpc] Stage 3: Verifying self-hosting..."
	diff $(BUILD_DIR)/cpc $(BUILD_DIR)/cpc-stage2
	@echo "[cpc] ✅ Self-hosting verified!"

test:
	cd .. && ./scripts/test.sh unit

clean:
	rm -f $(TARGET) $(BUILD_DIR)/cpc-stage2
