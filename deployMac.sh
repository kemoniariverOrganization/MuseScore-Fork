#!/bin/bash
export PATH=".:${PATH}:${HOME}/Qt/5.15.2/clang_64/bin"
make -f Makefile.osx revision -j12
make -f Makefile.osx release -j12
make -f Makefile.osx install -j12
#make -f Makefile.osx package -j12

# Here we have a dirty trick to resolve double directories created by code mscore and Musescore ODLA
cp -r ./applebuild/mscore.app/* ./applebuild/Musescore\ ODLA.app
rm -rf ./applebuild/mscore.app


deployPath=$(which macdeployqt)
qmlDir="${deployPath%macdeployqt}"../qml
macdeployqt ./applebuild/Musescore\ ODLA.app -qmldir=$qmlDir
