all: main clean-deps

CXX = clang++
override CXXFLAGS += \
    -g -O0 -Wno-everything -fno-caret-diagnostics \
    -Ilua-5.4.4/install/include \
    -I/nix/store/q0rhxcs2hfri6ja2fas671ywiszk20qr-iverilog-11.0/include/iverilog \
    -ldl -lvpi

#SRCS = $(shell find . -name '.ccls-cache' -type d -prune -o -type f -name '*.cpp' -print | sed -e 's/ /\\ /g')
SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o) lua-5.4.4/install/lib/liblua.a
DEPS = $(SRCS:.cpp=.d)

%.d: %.cpp
	@set -e; rm -f "$@"; \
	$(CXX) -MM $(CXXFLAGS) "$<" > "$@.$$$$"; \
	sed 's,\([^:]*\)\.o[ :]*,\1.o \1.d : ,g' < "$@.$$$$" > "$@"; \
	rm -f "$@.$$$$"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c "$<" -o "$@"

include $(DEPS)

main: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o "$@"

clean:
	rm -f $(OBJS) $(DEPS) main

clean-deps:
	rm -f $(DEPS)