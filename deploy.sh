#!/bin/bash
export PATH=".:${PATH}:${HOME}/Qt/5.15.2/clang_64/bin"

# Copy in installer folder
cp -R ./applebuild/Musescore\ ODLA.app ../ODLA/installer-mac/packages/it.kemoniariver.musescore/data

# Launch deploy script for Mac
deployPath=$(which macdeployqt)
qmlDir="${deployPath%macdeployqt}"../qml
macdeployqt ../ODLA/installer-mac/packages/it.kemoniariver.musescore/data/Musescore\ ODLA.app -qmldir=$qmlDir

