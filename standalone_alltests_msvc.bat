if not exist asio git clone https://github.com/chriskohlhoff/asio.git
cl /Zi /EHsc /O2 test\test_all.cpp detail\SpookyV2.cpp /Iinclude /Itest /DAFIO_STANDALONE=1 /Iasio/asio/include /DASIO_STANDALONE=1 /DBOOST_TEST_DYN_LINK=1
