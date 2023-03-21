#CXX=g++
#CXXFLAGS=-fmax-errors=1 -Og

CXX=clang++
CXXFLAGS=-ferror-limit=1 -O0

CXXFLAGS+=-ggdb -fsanitize=address -fsanitize=undefined -Wall -Wextra -std=c++17
LDFLAGS=-lboost_filesystem -fsanitize=address -fsanitize=undefined

hdrs=wsunitd.hpp
srcs=depgraph.cpp main.cpp unit.cpp util.cpp
objs=$(srcs:.cpp=.o)

all: wsunitd
wsunitd: $(objs)
	$(CXX) $(LDFLAGS) $^ -o $@

$(objs): %.o: %.cpp $(hdrs)



.PHONY: clean
clean:
	-rm wsunitd *.o

.PHONY: gdb
gdb: wsunitd
	gdb -ex run --args ./wsunitd config status
