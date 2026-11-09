CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall

all: runtime stencil.so

runtime: main.cpp supervisor.cpp env_source.cpp executor.cpp \
	 common.h supervisor.h executor.h env_source.h env_abi.h
	$(CXX) $(CXXFLAGS) -o $@ main.cpp supervisor.cpp env_source.cpp executor.cpp -ldl

stencil.so: env_stencil.cpp env_abi.h
	$(CXX) $(CXXFLAGS) -shared -fPIC -o $@ env_stencil.cpp

clean:
	rm -f runtime

.PHONY: clean
