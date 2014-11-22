#!/bin/sh
if [ ! -d asio ]; then
  git clone https://github.com/chriskohlhoff/asio.git
fi
rm -rf test_all
g++ -o test_all -g -O3 -std=c++11 test/test_all_multiabi.cpp detail/SpookyV2.cpp -DBOOST_THREAD_VERSION=3 -Iinclude -Itest -Iasio/asio/include -lboost_thread -lboost_chrono -lboost_filesystem -lboost_system -lpthread
