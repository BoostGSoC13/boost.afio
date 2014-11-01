#!/bin/sh
if [ ! -d asio ]; then
  git clone https://github.com/chriskohlhoff/asio.git
fi
g++ -o test_all -g -O3 -std=c++11 test/test_all.cpp detail/SpookyV2.cpp -Iinclude -Itest -DAFIO_STANDALONE=1 -Iasio/asio/include -DASIO_STANDALONE=1 -DBOOST_TEST_DYN_LINK=1 -lboost_filesystem -lboost_unit_test_framework -lboost_system -lpthread
