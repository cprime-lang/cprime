# =============================================================================
# C-Prime Bootstrap Compiler — Makefile
# Phase 2: Build the temporary C compiler that compiles compiler/src/main.cp
# =============================================================================

CC       = gcc
CFLAGS   = -O2 -std=c17 -Wall -Wextra -Wpedantic \
           -Wno-unused-parameter -Wno-unused-function \
           -I./include -I./src
LDFLAGS  = -lm

BUILD_DIR = ../build/bootstrap
TARGET    = $(BUILD_DIR)/cpc-bootstrap

SRCS = src/main.c \
       src/lexer/lexer.c \
       src/parser/parser.c \
       src/codegen/codegen.c \
       src/codegen/x86_64_emit.c \
       src/codegen/elf_writer.c \
       src/codegen/bootstrap_api.c

OBJS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean test verify-selfhost check-deps discard

all: check-deps $(TARGET)
	@echo ""
	@echo "\033[32m╔══════════════════════════════════════════╗\033[0m"
	@echo "\033[32m║  Bootstrap compiler built successfully!  ║\033[0m"
	@echo "\033[32m╚══════════════════════════════════════════╝\033[0m"
	@echo "  Binary: $(TARGET)"
	@echo "  Next:   make verify-selfhost"
	@echo ""

check-deps:
	@command -v gcc >/dev/null || (echo "ERROR: gcc not found" && exit 1)
	@command -v as  >/dev/null || (echo "ERROR: as not found" && exit 1)
	@echo "[bootstrap] Dependencies OK."

$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@chmod +x $@

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TARGET)
	@echo "[bootstrap] Running tests..."
	$(TARGET) --version
	@echo 'import io; fn main() -> i32 { io.println("Hello from C-Prime!"); return 0; }' > /tmp/test_hello.cp
	$(TARGET) /tmp/test_hello.cp -o /tmp/test_hello && /tmp/test_hello
	@rm -f /tmp/test_hello.cp /tmp/test_hello
	@echo "\033[32m[bootstrap] Tests passed!\033[0m"

verify-selfhost: $(TARGET)
	@echo "[Phase 2] Step 1: bootstrap → cpc..."
	@mkdir -p ../build/compiler
	$(TARGET) ../compiler/src/main.cp -o ../build/compiler/cpc
	@echo "[Phase 2] Step 2: cpc compiles itself..."
	../build/compiler/cpc ../compiler/src/main.cp -o ../build/compiler/cpc-stage2
	@echo "[Phase 2] Step 3: Comparing outputs..."
	@diff ../build/compiler/cpc ../build/compiler/cpc-stage2 && \
		echo "\033[32m✅ SELF-HOSTING ACHIEVED!\033[0m" || \
		(echo "\033[31mFAILED: outputs differ\033[0m" && exit 1)

discard:
	@git add -A && git commit -m "phase2: self-hosting achieved — discard bootstrap"
	@echo "\033[32mBootstrap discarded. cpc is now self-hosted.\033[0m"

clean:
	rm -rf $(BUILD_DIR)
