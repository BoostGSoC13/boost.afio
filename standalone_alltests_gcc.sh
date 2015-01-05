#!/bin/sh
if [ ! -d asio ]; then
  git clone https://github.com/chriskohlhoff/asio.git
fi
rm -rf test_all
g++ -o test_all -g -O2 -std=c++11 test/test_all.cpp detail/SpookyV2.cpp -Iinclude -Itest -DAFIO_STANDALONE=1 -Iasio/asio/include -DASIO_STANDALONE=1  -DBOOST_AFIO_RUNNING_IN_CI=1 -lboost_filesystem -lboost_system -lpthread
