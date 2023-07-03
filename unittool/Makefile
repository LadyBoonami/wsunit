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
LDFLAGS=-lboost_regex

ifneq ($(DEBUG),)
    CXXFLAGS+=-ggdb -fsanitize=address -fsanitize=undefined
    LDFLAGS=-fsanitize=address -fsanitize=undefined
endif

hdrs=unittool.hpp
srcs=cronexec.cpp runas.cpp unittool.cpp
objs=$(srcs:.cpp=.o)

all: unittool
unittool: $(objs)
	$(CXX) $(LDFLAGS) $^ -o $@

$(objs): %.o: %.cpp $(hdrs)



.PHONY: clean
clean:
	-rm unittool *.o
