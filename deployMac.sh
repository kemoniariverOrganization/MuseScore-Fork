#!/bin/bash
export PATH=".:${PATH}:${HOME}/Qt/5.15.2/clang_64/bin"
make -f Makefile.osx revision -j 12
make -f Makefile.osx release -j 12
make -f Makefile.osx install -j 12
make -f Makefile.osx package -j 12