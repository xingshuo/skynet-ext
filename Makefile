
CXX = g++
CFLAGS = -g3 -Wall -std=c++11
SHARED = -fPIC --shared
SKYNET_DIR ?= skynet

PROJECT_SRC = src
MSTIMER_SRC = $(PROJECT_SRC)/ms-timer
MSTIMER_LIB = libmstimer.so

all: $(MSTIMER_LIB)

$(MSTIMER_LIB): $(MSTIMER_SRC)/*.cc
	$(CXX) $(CFLAGS) $(SHARED) -I$(PROJECT_SRC) -I$(SKYNET_DIR)/skynet-src $^ -o $@

testtm.so: example/ms-timer/*.cc $(MSTIMER_SRC)/*.cc
	$(CXX) $(CFLAGS) $(SHARED) -Iexample/ms-timer -I$(PROJECT_SRC) -I$(SKYNET_DIR)/skynet-src $^ -o $@

clean:
	rm -f $(MSTIMER_LIB) testtm.so