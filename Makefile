SRC_DIR := src
BLD_DIR := build
INC_DIR := include
CONF_DIR := conf

SERVER_TARGET := badchatserver
CLIENT_TARGET := badchatclient
VERSION := 0.0.0
BUG_ADDRESS := email@example.com

SERVER_SRCS := $(shell find $(SRC_DIR)/server -name "*.c")
SERVER_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BLD_DIR)/%.o,$(SERVER_SRCS))
SERVER_DEPS := $(patsubst $(SRC_DIR)/%.c,$(BLD_DIR)/%.d,$(SERVER_SRCS))

CLIENT_SRCS := $(shell find $(SRC_DIR)/client -name "*.c")
CLIENT_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BLD_DIR)/%.o,$(CLIENT_SRCS))
CLIENT_DEPS := $(patsubst $(SRC_DIR)/%.c,$(BLD_DIR)/%.d,$(CLIENT_SRCS))

COMMON_SRCS := $(shell find $(SRC_DIR)/common -name "*.c")
COMMON_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BLD_DIR)/%.o,$(COMMON_SRCS))
COMMON_DEPS := $(patsubst $(SRC_DIR)/%.c,$(BLD_DIR)/%.d,$(COMMON_SRCS))

DEBUG ?= 1

ifneq ($(strip $(DEBUG)),)
OPT_CFLAGS := -O0 -g
else
OPT_CFLAGS := -O2
endif

GNUTLS_CFLAGS := $(shell pkg-config gnutls --cflags)
GNUTLS_LDFLAGS := $(shell pkg-config gnutls --libs)

CFLAGS := $(OPT_CFLAGS) $(GNUTLS_CFLAGS) -I$(BLD_DIR) -I$(INC_DIR) -I$(SRC_DIR)/common -std=gnu23
LDFLAGS := $(GNUTLS_LDFLAGS) -lsodium -ldl

BUILD_HEADER := $(BLD_DIR)/build.h
BUILD_HEADER_GEN := $(BLD_DIR)/build_gen.h

.PHONY: all clean server_config

all: $(BLD_DIR)/$(SERVER_TARGET)

server_config: $(CONF_DIR)/server_config.so

$(CONF_DIR)/server_config.o: $(CONF_DIR)/server_config.c
	$(CC) $(OPT_CFLAGS) -fPIC -I$(INC_DIR) -c $(CONF_DIR)/server_config.c -o $(CONF_DIR)/server_config.o

$(CONF_DIR)/server_config.so: $(CONF_DIR)/server_config.o
	$(CC) $(OPT_CFLAGS) -shared $(CONF_DIR)/server_config.o -o $(CONF_DIR)/server_config.so

clean:
	$(RM) -r $(BLD_DIR)
	$(RM) $(CONF_DIR)/server_config.so $(CONF_DIR)/server_config.o

$(BLD_DIR):
	@mkdir -p $@

$(BLD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_HEADER)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

$(BLD_DIR)/$(SERVER_TARGET): $(SERVER_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BLD_DIR)/$(CLIENT_TARGET): $(CLIENT_OBJS) $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_HEADER_GEN): | $(BLD_DIR)
	@echo -e "#ifndef BUILD_H" > $@
	@echo -e "#define BUILD_H\n" >> $@
	@echo -e "#define PROGRAM_NAME_SERVER \"$(SERVER_TARGET)\"" >> $@
	@echo -e "#define PROGRAM_NAME_CLIENT \"$(CLIENT_TARGET)\"" >> $@
	@echo -e "#define PROGRAM_VERSION \"$(VERSION)\"" >> $@
	@echo -e "#define PROGRAM_BUG_ADDRESS \"$(BUG_ADDRESS)\"\n" >> $@
	@echo -e "#endif" >> $@

$(BUILD_HEADER): $(BUILD_HEADER_GEN)
	@if ! diff $@ $< >/dev/null 2>&1; then cp $< $@; fi

print-%:
	@echo $* = $($*)

-include $(SERVER_DEPS)
-include $(CLIENT_DEPS)
-include $(COMMON_DEPS)
