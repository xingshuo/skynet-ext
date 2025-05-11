
CXX = g++
CFLAGS = -g3 -Wall -std=c++11
SHARED = -fPIC --shared
SKYNET_DIR ?= skynet

MSTIMER_SRC = ms-timer
MSTIMER_LIB = libmstimer.so

all: $(MSTIMER_LIB)

$(MSTIMER_LIB): $(MSTIMER_SRC)/*.cc
	$(CXX) $(CFLAGS) $(SHARED) -I$(SKYNET_DIR)/skynet-src -I$(MSTIMER_SRC) $^ -o $@

testtm.so: example/ms-timer/*.cc $(MSTIMER_SRC)/*.cc
	$(CXX) $(CFLAGS) $(SHARED) -I$(SKYNET_DIR)/skynet-src -Iexample/ms-timer -I$(MSTIMER_SRC) $^ -o $@

clean:
	rm -f $(MSTIMER_LIB) testtm.so