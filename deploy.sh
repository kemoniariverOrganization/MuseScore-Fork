#!/bin/bash
export PATH=".:${PATH}:${HOME}/Qt/5.15.2/clang_64/bin"
deployPath=$(which macdeployqt)
qmlDir="${deployPath%macdeployqt}"../qml
macdeployqt ./applebuild/Musescore\ ODLA.app -qmldir=$qmlDir
