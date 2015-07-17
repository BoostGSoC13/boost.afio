#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(async_io_sync, "Tests async fsync", 5)
{
    using namespace BOOST_AFIO_V1_NAMESPACE;
    namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;
    std::vector<char> buffer(64, 'n');
    auto dispatcher = make_async_file_io_dispatcher(process_threadpool(), file_flags::always_sync);
    std::cout << "\n\nTesting synchronous directory and file creation:\n";
    {
      auto mkdir(dispatcher->dir(async_path_op_req("testdir", file_flags::create)));
      auto mkfile(dispatcher->file(async_path_op_req::relative(mkdir, "foo", file_flags::create | file_flags::read_write)));
      auto writefile1(dispatcher->write(async_data_op_req < std::vector < char >> (mkfile, buffer, 0)));
      auto sync1(dispatcher->sync(writefile1));
      auto writefile2(dispatcher->write(async_data_op_req < std::vector < char >> (sync1, buffer, 0)));
      auto closefile1(dispatcher->close(writefile2));
      auto openfile(dispatcher->file(async_path_op_req::relative(closefile1, file_flags::read|file_flags::os_mmap)));
      char b[64];
      auto readfile(dispatcher->read(make_async_data_op_req(openfile, b, 0)));
      auto closefile2=dispatcher->close(readfile);
      auto delfile(dispatcher->rmfile(dispatcher->depends(closefile2, closefile1)));
      auto deldir(dispatcher->rmdir(dispatcher->depends(delfile, mkdir)));
      BOOST_CHECK_NO_THROW(mkdir.get());
      BOOST_CHECK_NO_THROW(mkfile.get());
      BOOST_CHECK_NO_THROW(writefile1.get());
      BOOST_CHECK_NO_THROW(sync1.get());
      BOOST_CHECK_NO_THROW(writefile2.get());
      BOOST_CHECK_NO_THROW(closefile1.get());
      BOOST_CHECK_NO_THROW(openfile.get());
      BOOST_CHECK_NO_THROW(readfile.get());
      BOOST_CHECK_NO_THROW(closefile2.get());
      BOOST_CHECK_NO_THROW(delfile.get());
      BOOST_CHECK_NO_THROW(deldir.h.wait());  // virus checkers sometimes make this spuriously fail
    }
}