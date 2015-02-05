#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(async_io_torture_autoflush, "Tortures the autoflush async i/o implementation", 60)
{
  using namespace BOOST_AFIO_V1_NAMESPACE;
  namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;
#ifndef BOOST_AFIO_RUNNING_IN_CI
  auto dispatcher = make_async_file_io_dispatcher(process_threadpool(), file_flags::SyncOnClose);
  std::cout << "\n\nSustained random autoflush i/o to 10 files of 1Mb:\n";
  evil_random_io(dispatcher, 10, 1 * 1024 * 1024);
#endif
}