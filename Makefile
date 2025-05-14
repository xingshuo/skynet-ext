CXX = g++
CFLAGS = -g3 -Wall -std=c++11

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

clean:
	rm -rf build/