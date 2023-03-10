CXX=clang++
CXXFLAGS=-O0 -ggdb -fsanitize=address -fsanitize=undefined -Wall -Wextra -lboost_filesystem -ferror-limit=1 -std=c++17

all: wsunit
wsunit: wsunit.cpp wsunit.hpp
	$(CXX) $(CXXFLAGS) $< -o $@

.PHONY: clean
clean:
	-rm wsunit

.PHONY: dot
dot:
	dot -Tpng status/state.dot | feh -

.PHONY: gdb
gdb: wsunit
	gdb -ex run --args ./wsunit config status
