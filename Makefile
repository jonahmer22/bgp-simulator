CC ?= gcc
CXX ?= g++
SRC_DIR := src
INC_DIR := include
TEST_DIR := tests
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj

SRC_FILES := $(wildcard $(SRC_DIR)/*.c)
CORE_SRCS := $(filter-out $(SRC_DIR)/main.c,$(SRC_FILES))
APP_SRCS := $(CORE_SRCS) $(SRC_DIR)/main.c

ARENA_SRC := deps/arena/arena.c
ARENA_OBJ := $(OBJ_DIR)/arena_dep.o

OBJ_FILES := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(APP_SRCS)) $(ARENA_OBJ)
CORE_OBJS := $(filter-out $(OBJ_DIR)/main.o,$(OBJ_FILES))

TEST_SRCS := $(wildcard $(TEST_DIR)/*.c++)
TEST_OBJS := $(patsubst $(TEST_DIR)/%.c++,$(OBJ_DIR)/test_%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst $(TEST_DIR)/%.c++,$(BUILD_DIR)/test_%,$(TEST_SRCS))

INCLUDES := -I$(INC_DIR) -Ideps/arena
CSTD := -std=c11
CXXSTD := -std=c++17
SPEED_FLAGS := -O3 -march=native -mtune=native -pipe -ffunction-sections -fdata-sections -fno-plt -DNDEBUG -ffast-math -fomit-frame-pointer -fno-stack-protector
WARN_FLAGS := -Wall -Wextra -Wpedantic
CFLAGS ?=
CFLAGS += $(CSTD) $(INCLUDES) $(SPEED_FLAGS) $(WARN_FLAGS) -pthread
CXXFLAGS ?=
CXXFLAGS += $(CXXSTD) $(INCLUDES) $(SPEED_FLAGS) $(WARN_FLAGS) -pthread
LDFLAGS ?=
LDFLAGS += -flto -Wl,-O3 -pthread
ifdef ASAN
SAN_FLAGS := -fsanitize=address -fno-omit-frame-pointer -g
CFLAGS += $(SAN_FLAGS)
CXXFLAGS += $(SAN_FLAGS)
LDFLAGS += $(SAN_FLAGS)
endif
GTEST_LIBS ?= -lgtest -lgtest_main
GTEST_INCLUDE_DIR ?=
GTEST_LIB_DIR ?=

ifeq ($(GTEST_INCLUDE_DIR),)
ifneq ($(wildcard /opt/homebrew/include/gtest/gtest.h),)
GTEST_INCLUDE_DIR := /opt/homebrew/include
endif
endif

ifeq ($(GTEST_LIB_DIR),)
ifneq ($(wildcard /opt/homebrew/lib/libgtest.dylib),)
GTEST_LIB_DIR := /opt/homebrew/lib
else ifneq ($(wildcard /opt/homebrew/lib/libgtest.a),)
GTEST_LIB_DIR := /opt/homebrew/lib
endif
endif

ifneq ($(GTEST_INCLUDE_DIR),)
CXXFLAGS += -I$(GTEST_INCLUDE_DIR)
endif
ifneq ($(GTEST_LIB_DIR),)
LDFLAGS += -L$(GTEST_LIB_DIR)
endif

.PHONY: all clean tests bench-prefix bench-subprefix bench-many bench

all: $(BUILD_DIR)/bgp_simulator

$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/test_%.o: $(TEST_DIR)/%.c++ | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ARENA_OBJ): $(ARENA_SRC) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/bgp_simulator: $(OBJ_FILES)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(OBJ_FILES) $(LDFLAGS) -o $@

$(BUILD_DIR)/test_%: $(CORE_OBJS) $(OBJ_DIR)/test_%.o
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CORE_OBJS) $(OBJ_DIR)/test_$*.o $(LDFLAGS) $(GTEST_LIBS) -o $@

tests: $(TEST_BINS)
	@set -e; for test_bin in $(TEST_BINS); do $$test_bin; done

define RUN_BENCH
bench-$(1): $(BUILD_DIR)/bgp_simulator
	@mkdir -p $(BUILD_DIR)
	$(BUILD_DIR)/bgp_simulator --relationships bench/$(1)/CAIDAASGraphCollector_2025.10.16.txt \
		--announcements bench/$(1)/anns.csv \
		--rov-asns bench/$(1)/rov_asns.csv \
		--output $(BUILD_DIR)/bench_$(1)_output.csv
	bench/compare_output.sh bench/$(1)/ribs.csv $(BUILD_DIR)/bench_$(1)_output.csv
endef

$(foreach dataset,prefix subprefix many,$(eval $(call RUN_BENCH,$(dataset))))

bench: bench-prefix bench-subprefix bench-many

clean:
	rm -rf $(BUILD_DIR)
