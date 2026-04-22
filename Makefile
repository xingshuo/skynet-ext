CXX = g++
CFLAGS = -g -Wall -Wextra -Werror -std=c++17

ifdef DEBUG
	CFLAGS += -DDEBUG_LOG_OUTPUT=1
endif

SHARED = -fPIC --shared
SKYNET_DIR ?= skynet

TOP=$(CURDIR)
BUILD_DIR=$(TOP)/build

PROJECT_SRC = src
MSTIMER_SRC = $(PROJECT_SRC)/ms-timer
MSTIMER_LIB = $(BUILD_DIR)/libmstimer.so

SIGNAL_SRC = $(PROJECT_SRC)/signal
SIGNAL_LIB = $(BUILD_DIR)/signal_mgr.so

FSNOTIFY_SRC = $(PROJECT_SRC)/fsnotify
FSNOTIFY_LIB = $(BUILD_DIR)/fsnotify.so

all: build
build:
	mkdir -p $(BUILD_DIR)

all: $(MSTIMER_LIB)

$(MSTIMER_LIB): $(MSTIMER_SRC)/*.cc
	$(CXX) $(CFLAGS) $(SHARED) -I$(PROJECT_SRC) -I$(SKYNET_DIR)/skynet-src $^ -o $@

all: $(BUILD_DIR)/lmstimer.so

$(BUILD_DIR)/lmstimer.so: binding/lua-mstimer.cc | $(MSTIMER_LIB)
	$(CXX) $(CFLAGS) $(SHARED) -I$(PROJECT_SRC) -I$(SKYNET_DIR)/3rd/lua -L$(BUILD_DIR) -lmstimer -Wl,-rpath $(BUILD_DIR) $^ -o $@

all: $(BUILD_DIR)/test_mstimer.so

$(BUILD_DIR)/test_mstimer.so: example/ms-timer/*.cc | $(MSTIMER_LIB)
	$(CXX) $(CFLAGS) $(SHARED) -Iexample/ms-timer -I$(PROJECT_SRC) -I$(SKYNET_DIR)/skynet-src -L$(BUILD_DIR) -lmstimer -Wl,-rpath $(BUILD_DIR) $^ -o $@

all: $(SIGNAL_LIB)

$(SIGNAL_LIB): $(SIGNAL_SRC)/*.cc
	$(CXX) $(CFLAGS) $(SHARED) -I$(PROJECT_SRC) -I$(SKYNET_DIR)/skynet-src $^ -o $@

all: $(FSNOTIFY_LIB)

$(FSNOTIFY_LIB): $(FSNOTIFY_SRC)/*.cc
	$(CXX) $(CFLAGS) $(SHARED) -I$(PROJECT_SRC) -I$(SKYNET_DIR)/skynet-src $^ -o $@

all: $(BUILD_DIR)/lfsnotify.so

$(BUILD_DIR)/lfsnotify.so: binding/lua-fsnotify.cc
	$(CXX) $(CFLAGS) $(SHARED) -I$(PROJECT_SRC) -I$(SKYNET_DIR)/3rd/lua $^ -o $@

all: $(BUILD_DIR)/test_signal.so

$(BUILD_DIR)/test_signal.so: example/signal/*.cc
	$(CXX) $(CFLAGS) $(SHARED) -Iexample/signal -I$(PROJECT_SRC) -I$(SKYNET_DIR)/skynet-src $^ -o $@

all: $(BUILD_DIR)/test_fsnotify.so

$(BUILD_DIR)/test_fsnotify.so: example/fsnotify/*.cc
	$(CXX) $(CFLAGS) $(SHARED) -Iexample/fsnotify -I$(PROJECT_SRC) -I$(SKYNET_DIR)/skynet-src $^ -o $@

check:
	cppcheck --enable=all --std=c++17 --suppress=missingIncludeSystem -I $(PROJECT_SRC) -I $(SKYNET_DIR)/skynet-src $(MSTIMER_SRC) 2>&1
	cppcheck --enable=all --std=c++17 --suppress=missingIncludeSystem -I $(PROJECT_SRC) -I $(SKYNET_DIR)/skynet-src $(SIGNAL_SRC) 2>&1
	cppcheck --enable=all --std=c++17 --suppress=missingIncludeSystem -I $(PROJECT_SRC) -I $(SKYNET_DIR)/skynet-src $(FSNOTIFY_SRC) 2>&1

clean:
	rm -rf build/