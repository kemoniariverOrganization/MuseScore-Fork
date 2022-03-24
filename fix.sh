#!/bin/bash

# Here we have a dirty trick to resolve double directories created by code mscore and Musescore ODLA
cp -r ./applebuild/mscore.app/* ./applebuild/Musescore\ ODLA.app
rm -rf ./applebuild/mscore.app
