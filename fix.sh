#!/bin/bash
export PATH=".:${PATH}:${HOME}/Qt/5.15.2/clang_64/bin"

# Here we have a dirty trick to resolve double directories created by code mscore and Musescore ODLA
cp -r ./applebuild/mscore.app/* ./applebuild/Musescore\ ODLA.app
rm -rf ./applebuild/mscore.app