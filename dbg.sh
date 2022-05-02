#!/bin/bash

set -e

#iverilog-vpi main.cpp -llua
g++ -g -O0 -fpic -fno-diagnostics-show-caret -Wall \
    -c -o main.o main.cpp \
    -I lua-5.4.4/install/include \
    -I /nix/store/q0rhxcs2hfri6ja2fas671ywiszk20qr-iverilog-11.0/include/iverilog
g++ -g -O0 -shared -o main.vpi main.o -Llua-5.4.4/install/lib -l:liblua.a  

iverilog -o hello.vvp hello.v
gdb --args vvp -M. -m main hello.vvp