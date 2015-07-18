#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(async_io_lstat_works, "Tests that async i/o lstat() works", 60)
{
    using namespace BOOST_AFIO_V1_NAMESPACE;
    namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;
    // Oh Windows, oh Windows, how strange you are ...
    for (size_t n = 0; n < 10; n++)
    {
      try
      {
        if (filesystem::exists("testdir"))
          filesystem::remove("testdir");
        break;
      }
      catch (...)
      {
        this_thread::sleep_for(chrono::milliseconds(10));
      }
    }

    auto dispatcher=make_async_file_io_dispatcher();
    auto test(dispatcher->dir(async_path_op_req("testdir", file_flags::create|file_flags::write)));
    {
      auto mkdir(dispatcher->dir(async_path_op_req::relative(test, "dir", file_flags::create|file_flags::write)));
      auto mkfile(dispatcher->file(async_path_op_req::relative(mkdir, "file", file_flags::create|file_flags::write)));
      auto mklink(dispatcher->symlink(async_path_op_req::absolute(mkdir, "testdir/linktodir", file_flags::create|file_flags::write)));

      auto mkdirstat=print_stat(when_all(mkdir).get().front());
      auto mkfilestat=print_stat(when_all(mkfile).get().front());
      auto mklinkstat=print_stat(when_all(mklink).get().front());
#ifdef BOOST_AFIO_USE_LEGACY_FILESYSTEM_SEMANTICS
      BOOST_CHECK(mkdirstat.st_type==filesystem::file_type::directory_file);
      BOOST_CHECK(mkfilestat.st_type==filesystem::file_type::regular_file);
      BOOST_CHECK(mklinkstat.st_type==filesystem::file_type::symlink_file);
#else
      BOOST_CHECK(mkdirstat.st_type==filesystem::file_type::directory);
      BOOST_CHECK(mkfilestat.st_type==filesystem::file_type::regular);
      BOOST_CHECK(mklinkstat.st_type==filesystem::file_type::symlink);
#endif

      // Some sanity stuff
      BOOST_CHECK(mkdirstat.st_ino!=mkfilestat.st_ino);
      BOOST_CHECK(mkfilestat.st_ino!=mklinkstat.st_ino);
      BOOST_CHECK(mkdirstat.st_ino!=mklinkstat.st_ino);
      BOOST_CHECK(mklink.get_handle()->target()==mkdir.get_handle()->path());
//      BOOST_CHECK(mkdir.get()->container()->native_handle()==test.get()->native_handle());

      auto rmlink(dispatcher->close(dispatcher->rmsymlink(mklink)));
      auto rmfile(dispatcher->close(dispatcher->rmfile(dispatcher->depends(rmlink, mkfile))));
      auto rmdir(dispatcher->close(dispatcher->rmdir(dispatcher->depends(rmfile, mkdir))));
      when_all(rmlink, rmfile, rmdir).get();
    }
    // For the laugh, do it synchronously
    test->unlink();
}
