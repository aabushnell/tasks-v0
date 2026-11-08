CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall

runtime: main.cpp supervisor.cpp executor.cpp kernels.cpp \
         common.h supervisor.h executor.h kernels.h
	$(CXX) $(CXXFLAGS) -o $@ main.cpp supervisor.cpp executor.cpp kernels.cpp

clean:
	rm -f runtime

.PHONY: clean
