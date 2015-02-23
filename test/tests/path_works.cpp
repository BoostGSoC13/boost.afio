#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(path_works, "Tests that the path functions work as they are supposed to", 20)
{
    using namespace BOOST_AFIO_V1_NAMESPACE;
    auto dispatcher = make_async_file_io_dispatcher();
    async_io_op op=dispatcher->file(async_path_op_req("testfile", file_flags::Create|file_flags::ReadWrite));
    auto h=op.get();
    auto originalpath=h->path();
    print_stat(h);
    auto originalpath2=h->path();
    BOOST_CHECK(originalpath==originalpath2);    

#ifdef WIN32
    // Verify pass through
    path p("testfile");
    BOOST_CHECK(p.native()==L"testfile");
    
    p=path::make_absolute("testfile");
    BOOST_CHECK(p.native()!=L"testfile");
    // Make sure it prepended \??\ to make a NT kernel path
    BOOST_CHECK((p.native()[0]=='\\' && p.native()[1]=='?' && p.native()[2]=='?' && p.native()[3]=='\\'));
    BOOST_CHECK((isalpha(p.native()[4]) && p.native()[5]==':'));
    // Make sure it converts back via fast path
    auto fp=p.filesystem_path();
    BOOST_CHECK((fp.native()[0]=='\\' && fp.native()[1]=='\\' && fp.native()[2]=='?' && fp.native()[3]=='\\'));
    BOOST_CHECK(p.native().substr(4)==fp.native().substr(4));
    // Make sure it converts back perfectly via slow path
    fp=normalise_path(p);
    auto a=fp.native(), b=filesystem::absolute("testfile").native();
    // Filesystem has been known to return lower case drive letters ...
    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
    std::transform(b.begin(), b.end(), b.begin(), ::tolower);
    std::cout << a << std::endl;
    std::cout << b << std::endl;
    BOOST_CHECK(a==b);
    
    // Make sure it handles extended path inputs
    p=filesystem::path("\\\\?")/filesystem::absolute("testfile");
    BOOST_CHECK((p.native()[0]=='\\' && p.native()[1]=='?' && p.native()[2]=='?' && p.native()[3]=='\\'));
    BOOST_CHECK((isalpha(p.native()[4]) && p.native()[5]==':'));

    // Make sure native NT path inputs are preserved
    filesystem::path o("\\\\.\\Device1");
    p=o;
    BOOST_CHECK(p.native()==L"\\Device1");
    fp=p.filesystem_path();
    BOOST_CHECK(fp==o);
        
#endif

    std::cout << "\nRenaming testfile to hellobaby using OS ..." << std::endl;
    filesystem::rename("testfile", "hellobaby");
    print_stat(h);
    auto afterrename=h->path();
    BOOST_CHECK((originalpath.parent_path()/"hellobaby")==afterrename);
    std::cout << "\nDeleting hellobaby file using OS ..." << std::endl;
    filesystem::remove("hellobaby");
    auto afterdelete=print_stat(h);
    BOOST_CHECK(h->path()==BOOST_AFIO_V1_NAMESPACE::path());
}
