#!/bin/bash -x
find example -name "*.cpp" -exec clang-format-3.6 -i {} ';'
clang-format-3.6 -i include/boost/afio/afio.hpp
#clang-format-3.6 -i include/boost/afio/config.hpp
find include/boost/afio/detail -name "*.hpp" -exec clang-format-3.6 -i {} ';'
find include/boost/afio/detail -name "*.ipp" -exec clang-format-3.6 -i {} ';'
find test -name "*.hpp" -exec clang-format-3.6 -i {} ';'
find test -name "*.cpp" -exec clang-format-3.6 -i {} ';'

PWD=$(pwd)
find example -name "*.cpp" -exec "$PWD/scripts/IndentCmacros.py" {} ';'
scripts/IndentCmacros.py include/boost/afio/afio.hpp
scripts/IndentCmacros.py include/boost/afio/config.hpp
find include/boost/afio/detail -name "*.hpp" -exec "$PWD/scripts/IndentCmacros.py" {} ';'
find include/boost/afio/detail -name "*.ipp" -exec "$PWD/scripts/IndentCmacros.py" {} ';'
find test -name "*.hpp" -exec "$PWD/scripts/IndentCmacros.py" {} ';'
find test -name "*.cpp" -exec "$PWD/scripts/IndentCmacros.py" {} ';'
