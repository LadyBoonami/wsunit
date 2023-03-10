#CXX=g++
#CXXFLAGS=-fmax-errors=1 -Og

CXX=clang++
CXXFLAGS=-ferror-limit=1 -O0

CXXFLAGS+=-ggdb -fsanitize=address -fsanitize=undefined -Wall -Wextra -std=c++17
LDFLAGS=-lboost_filesystem -fsanitize=address -fsanitize=undefined

hdrs=wsunit.hpp
srcs=depgraph.cpp main.cpp unit.cpp util.cpp
objs=$(srcs:.cpp=.o)

all: wsunit
wsunit: $(objs)
	$(CXX) $(LDFLAGS) $^ -o $@

$(objs): %.o: %.cpp $(hdrs)



.PHONY: clean
clean:
	-rm wsunit *.o

.PHONY: dot
dot:
	dot -Tpng status/state.dot | feh -

.PHONY: gdb
gdb: wsunit
	gdb -ex run --args ./wsunit config status
