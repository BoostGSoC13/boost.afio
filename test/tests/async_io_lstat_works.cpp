#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(async_io_lstat_works, "Tests that async i/o lstat() works", 60)
{
    using namespace BOOST_AFIO_V1_NAMESPACE;
    namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;
    if(filesystem::exists("testdir"))
        filesystem::remove_all("testdir");

    try
    {
        auto dispatcher=make_async_file_io_dispatcher();
        {
            auto test(dispatcher->dir(async_path_op_req("testdir", file_flags::Create)));
            auto mkdir(dispatcher->dir(async_path_op_req(test, "testdir/dir", file_flags::Create|file_flags::FastDirectoryEnumeration)));
            auto mkfile(dispatcher->file(async_path_op_req(mkdir, "testdir/dir/file", file_flags::Create)));
            auto mklink(dispatcher->symlink(async_path_op_req(mkdir, "testdir/linktodir", file_flags::Create)));

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
            BOOST_CHECK(mklink.get()->target()==mkdir.get()->path());
            BOOST_CHECK(mkdir.get()->container()->native_handle()==test.get()->native_handle());
        }

        // Let the handles close before deleting
        while(dispatcher->fd_count()) this_thread::yield();
        auto rmlink(dispatcher->rmsymlink(async_path_op_req("testdir/linktodir")));
        auto rmfile(dispatcher->rmfile(async_path_op_req(rmlink, "testdir/dir/file")));
        auto rmdir(dispatcher->rmdir(async_path_op_req(rmfile, "testdir/dir")));
        auto rmtest(dispatcher->rmdir(async_path_op_req(rmdir, "testdir")));
        when_all(rmtest).wait();
    }
    catch(...)
    {
        std::cerr << "Exception thrown." << std::endl;
        BOOST_CHECK(false);
    }
}
