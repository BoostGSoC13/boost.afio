#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(async_io_works_64_sync, "Tests that the synchronous async i/o implementation works", 60)
{
    using namespace BOOST_AFIO_V1_NAMESPACE;
    namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;
#ifndef BOOST_AFIO_RUNNING_IN_CI
    auto dispatcher=make_async_file_io_dispatcher(process_threadpool(), file_flags::always_sync);
    std::cout << "\n\n1000 file opens, writes 64Kb, closes, and deletes with synchronous i/o:\n";
    _1000_open_write_close_deletes(dispatcher, 65536);
#endif
}