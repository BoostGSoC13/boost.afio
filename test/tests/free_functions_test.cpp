#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(free_functions_work, "Tests that the free functions work as intended", 5)
{
  using namespace BOOST_AFIO_V2_NAMESPACE;
  using BOOST_AFIO_V2_NAMESPACE::rmdir;
  current_dispatcher_guard h(make_async_file_io_dispatcher());
  async_dir("testdir", file_flags::create)()
    .then(async_file("testfile", file_flags::create | file_flags::write))
    .then(async_rmfile())
    .get();
  rmdir("testdir");
}
