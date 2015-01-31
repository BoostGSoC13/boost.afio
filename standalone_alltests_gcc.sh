#!/bin/sh
if [ -z "$CXX" ]; then
  CXX=g++
fi
if [ "$CXX" != "${CXX#clang++}" ] && [ "$NODE_NAME" = "linux-gcc-clang" ]; then
  LIBATOMIC=-latomic
fi
if [ "$NODE_NAME" = "freebsd10-clang3.3" ]; then
  LIBATOMIC=-L/usr/local/include
fi
if [ ! -d asio ]; then
  sh -c "git clone https://github.com/chriskohlhoff/asio.git"
fi
rm -rf test_all
$CXX -o test_all -g -O2 -std=c++11 test/test_all.cpp detail/SpookyV2.cpp -Iinclude -Itest -DAFIO_STANDALONE=1 -Iasio/asio/include -DASIO_STANDALONE=1  -DBOOST_AFIO_RUNNING_IN_CI=1 -Wno-constexpr-not-const -Wno-c++1y-extensions -lboost_filesystem -lboost_system -lpthread $LIBATOMIC
