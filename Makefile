CC 			= zig cc
TARGET_NAME = main

BUILD 		?= debug
ZIG_TARGET 	?= native

ifeq ($(findstring windows,$(ZIG_TARGET)),windows)
    EXT = .exe
else
    EXT =
endif

TARGET = $(TARGET_NAME)$(EXT)

SRC_DIR = src
OBJ_DIR = obj/$(BUILD)-$(ZIG_TARGET)

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

BASE_CFLAGS = -ffunction-sections -fdata-sections -Wall -Wextra
BASE_LDFLAGS = -Wl,--gc-sections -static

ifneq ($(ZIG_TARGET),native)
    BASE_CFLAGS += -target $(ZIG_TARGET)
    BASE_LDFLAGS += -target $(ZIG_TARGET)
endif

ifeq ($(BUILD),release)
    CFLAGS = $(BASE_CFLAGS) -Oz -DNDEBUG
    LDFLAGS = $(BASE_LDFLAGS) -s
else
    CFLAGS = $(BASE_CFLAGS) -g -O0
    LDFLAGS = $(BASE_LDFLAGS)
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $(TARGET) [$(BUILD) mode, target: $(ZIG_TARGET), STATIC]..."
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete."

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	@echo "Cleaning build artifacts..."
	rm -rf obj $(TARGET_NAME) $(TARGET_NAME).exe

.PHONY: all clean
