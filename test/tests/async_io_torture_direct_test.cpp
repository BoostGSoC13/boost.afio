#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(async_io_torture_direct, "Tortures the direct async i/o implementation", 60)
{
  using namespace BOOST_AFIO_V1_NAMESPACE;
  namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;
  auto dispatcher = make_async_file_io_dispatcher(process_threadpool(), file_flags::OSDirect);
  std::cout << "\n\nSustained random direct i/o to 10 files of 10Mb:\n";
  evil_random_io(dispatcher, 10, 10 * 1024 * 1024, 4096);
}