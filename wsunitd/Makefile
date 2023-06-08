ifeq ($(CXX),g++)
    CXXFLAGS=-fmax-errors=1
    ifneq ($(DEBUG),)
        CXXFLAGS+=-Og
    else
        CXXFLAGS+=-O2
    endif
else ifeq ($CXX),clang++)
    CXXFLAGS=-ferror-limit=1
    ifneq ($(DEBUG),)
        CXXFLAGS+=-O0
    else
        CXXFLAGS+=-O3
    endif
else
    CXXFLAGS=
endif

CXXFLAGS+=-Wall -Wextra -std=c++17
LDFLAGS=-lboost_filesystem

ifneq ($(DEBUG),)
    CXXFLAGS+=-ggdb -fsanitize=address -fsanitize=undefined
    LDFLAGS=-fsanitize=address -fsanitize=undefined
endif

hdrs=wsunitd.hpp
srcs=depgraph.cpp epoll.cpp main.cpp unit.cpp util.cpp
objs=$(srcs:.cpp=.o)

all: wsunitd
wsunitd: $(objs)
	$(CXX) $^ $(LDFLAGS) -o $@

$(objs): %.o: %.cpp $(hdrs)



.PHONY: clean
clean:
	-rm wsunitd *.o

.PHONY: gdb
gdb: wsunitd
	gdb -ex run --args ./wsunitd config status
