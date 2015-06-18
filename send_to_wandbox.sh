#!/bin/bash
include/boost/afio/bindlib/scripts/GenSingleHeader.py -DAFIO_STANDALONE=1 -DSPINLOCK_STANDALONE=1 -Eafio_iocp.ipp -Evalgrind -Ent_kernel_stuff -Econcurrent_unordered_map include/boost/afio/afio.hpp > include/boost/afio/single_include.hpp
sed "1s/.*/#include \"afio_single_include.hpp\"/" example/readwrite_example.cpp > include/send_to_wandbox.cpp
cd include
sed "s/#include/@include/g" boost/afio/single_include.hpp > afio_single_include.hpp
g++ -std=c++11 -E afio_single_include.hpp > afio_single_include2.hpp
sed "s/@include/#include/g" afio_single_include2.hpp > afio_single_include.hpp
sed "s/# [0-9][0-9]* \".*\".*//g" afio_single_include.hpp > afio_single_include2.hpp
sed "/^$/d" afio_single_include2.hpp > afio_single_include.hpp
boost/afio/bindlib/scripts/send_to_wandbox.sh send_to_wandbox.cpp afio_single_include.hpp
cd ..

