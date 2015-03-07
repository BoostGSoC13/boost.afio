#!/bin/bash -x
find example -print0 -name "*.cpp" | xargs -0 clang-format-3.6 -i
clang-format-3.6 -i include/boost/afio/afio.hpp
#clang-format-3.6 -i include/boost/afio/config.hpp
find include/boost/afio/detail -print0 -name "*.hpp" | xargs -0 clang-format-3.6 -i
find include/boost/afio/detail -print0 -name "*.ipp" | xargs -0 clang-format-3.6 -i
find test -print0 -name "*.hpp" | xargs -0 clang-format-3.6 -i
find test -print0 -name "*.cpp" | xargs -0 clang-format-3.6 -i

PWD=$(pwd)
find example -print0 -name "*.cpp" | xargs -0 "$PWD/scripts/IndentCmacros.py"
scripts/IndentCmacros.py include/boost/afio/afio.hpp
scripts/IndentCmacros.py include/boost/afio/config.hpp
find include/boost/afio/detail -print0 -name "*.hpp" | xargs -0 "$PWD/scripts/IndentCmacros.py"
find include/boost/afio/detail -print0 -name "*.ipp" | xargs -0 "$PWD/scripts/IndentCmacros.py"
find test -print0 -name "*.hpp" | xargs -0 "$PWD/scripts/IndentCmacros.py"
find test -print0 -name "*.cpp" | xargs -0 "$PWD/scripts/IndentCmacros.py"
