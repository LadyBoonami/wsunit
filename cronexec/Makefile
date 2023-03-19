#CXX=g++
#CXXFLAGS=-fmax-errors=1 -Og

CXX=clang++
CXXFLAGS=-ferror-limit=1 -O0

CXXFLAGS+=-ggdb -fsanitize=address -fsanitize=undefined -Wall -Wextra -std=c++17
LDFLAGS=-lboost_regex -fsanitize=address -fsanitize=undefined

hdrs=cronexec.hpp
srcs=cronexec.cpp
objs=$(srcs:.cpp=.o)

all: cronexec
cronexec: $(objs)
	$(CXX) $(LDFLAGS) $^ -o $@

$(objs): %.o: %.cpp $(hdrs)



.PHONY: clean
clean:
	-rm cronexec *.o
