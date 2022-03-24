#!/bin/bash
export PATH=".:${PATH}:${HOME}/Qt/5.9.9/clang_64/bin"
make -f Makefile.osx revision -j12
make -f Makefile.osx release -j12
make -f Makefile.osx install -j12
#make -f Makefile.osx package -j12
