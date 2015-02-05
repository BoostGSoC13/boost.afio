#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(async_io_sync, "Tests async fsync", 5)
{
  using namespace BOOST_AFIO_V1_NAMESPACE;
  namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;
  std::vector<char> buffer(64, 'n');
  auto dispatcher = make_async_file_io_dispatcher(process_threadpool(), file_flags::AlwaysSync);
  std::cout << "\n\nTesting synchronous directory and file creation:\n";
  {
    auto mkdir(dispatcher->dir(async_path_op_req("testdir", file_flags::Create)));
    auto mkfile(dispatcher->file(async_path_op_req(mkdir, "testdir/foo", file_flags::Create | file_flags::ReadWrite)));
    auto writefile1(dispatcher->write(async_data_op_req<std::vector<char>>(mkfile, buffer, 0)));
    auto sync1(dispatcher->sync(writefile1));
    auto writefile2(dispatcher->write(async_data_op_req<std::vector<char>>(sync1, buffer, 0)));
    auto closefile1(dispatcher->close(writefile2));
    auto openfile(dispatcher->file(async_path_op_req(closefile1, "testdir/foo", file_flags::Read | file_flags::OSMMap)));
    char b[64];
    auto readfile(dispatcher->read(make_async_data_op_req(openfile, b, 0)));
    auto closefile2 = dispatcher->close(readfile);
    auto delfile(dispatcher->rmfile(async_path_op_req(closefile2, "testdir/foo")));
    auto deldir(dispatcher->rmdir(async_path_op_req(delfile, "testdir")));
    BOOST_CHECK_NO_THROW(when_all(mkdir).get());
    BOOST_CHECK_NO_THROW(when_all(mkfile).get());
    BOOST_CHECK_NO_THROW(when_all(writefile1).get());
    BOOST_CHECK_NO_THROW(when_all(sync1).get());
    BOOST_CHECK_NO_THROW(when_all(writefile2).get());
    BOOST_CHECK_NO_THROW(when_all(closefile1).get());
    BOOST_CHECK_NO_THROW(when_all(openfile).get());
    BOOST_CHECK_NO_THROW(when_all(readfile).get());
    BOOST_CHECK_NO_THROW(when_all(closefile2).get());
    BOOST_CHECK_NO_THROW(when_all(delfile).get());
    BOOST_CHECK_NO_THROW(when_all(deldir).wait());  // virus checkers sometimes make this spuriously fail
  }
}