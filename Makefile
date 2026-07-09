SRC_DIR := src
BLD_DIR := build

TARGET := badchatserver
VERSION := 0.0.0
BUG_ADDRESS := email@example.com

SRCS := $(shell find $(SRC_DIR) -name "*.c")
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BLD_DIR)/%.o,$(SRCS))
DEPS := $(patsubst $(SRC_DIR)/%.c,$(BLD_DIR)/%.d,$(SRCS))

CFLAGS := -O0 -g -I$(BLD_DIR)
LDFLAGS :=

BUILD_HEADER := $(BLD_DIR)/build.h
BUILD_HEADER_GEN := $(BLD_DIR)/build_gen.h

.PHONY: all clean

all: $(BLD_DIR)/$(TARGET)

clean:
	$(RM) -r $(BLD_DIR)

$(BLD_DIR):
	@mkdir -p $@

$(BLD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_HEADER)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BLD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_HEADER_GEN): | $(BLD_DIR)
	@echo -e "#ifndef BUILD_H" > $@
	@echo -e "#define BUILD_H\n" >> $@
	@echo -e "#define PROGRAM_NAME \"$(TARGET)\"" >> $@
	@echo -e "#define PROGRAM_VERSION \"$(VERSION)\"" >> $@
	@echo -e "#define PROGRAM_BUG_ADDRESS \"$(BUG_ADDRESS)\"\n" >> $@
	@echo -e "#endif" >> $@

$(BUILD_HEADER): $(BUILD_HEADER_GEN)
	@if ! diff $@ $< >/dev/null 2>&1; then cp $< $@; fi

-include $(DEPS)
