CXX = g++
BREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /usr/local)
CXXFLAGS = -std=c++17 -Wall -O2 -I$(BREW_PREFIX)/include
LDFLAGS = -L$(BREW_PREFIX)/lib -lzmq

BIN_MOD = limit_order_book_moderate
BIN_VOL = limit_order_book_volatile
BIN_VERY = limit_order_book_very_volatile
BIN_MOST = limit_order_book_most_volatile

SRC_MOD = LimitOrderBookIndexModerateVolatile.cpp
SRC_VOL = LimitOrderBookVolatile.cpp
SRC_VERY = LimitOrderBookVeryVolatile.cpp 
SRC_MOST = LimitOrderBookMostVolatile.cpp

all: compile_all run_server

compile_all:
	@echo "--- Compiling Engines ---"
	$(CXX) $(CXXFLAGS) -o $(BIN_MOD) $(SRC_MOD) $(LDFLAGS)
	$(CXX) $(CXXFLAGS) -o $(BIN_VOL) $(SRC_VOL) $(LDFLAGS)
	$(CXX) $(CXXFLAGS) -o $(BIN_VERY) $(SRC_VERY) $(LDFLAGS)
	$(CXX) $(CXXFLAGS) -o $(BIN_MOST) $(SRC_MOST) $(LDFLAGS)

run_server:
	@echo "--- Starting Orchestrator ---"
	./venv/bin/uvicorn server:socket_app --host 0.0.0.0 --port 8000 --reload