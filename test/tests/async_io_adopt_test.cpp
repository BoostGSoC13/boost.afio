#include "test_functions.hpp"

using namespace BOOST_AFIO_V1_NAMESPACE;
namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;

struct test_handle : async_io_handle
{
    test_handle(async_file_io_dispatcher_base *parent) : async_io_handle(parent, std::shared_ptr<async_io_handle>(),
        file_flags::None) {}
    virtual void close()
    {
        // Do nothing
    }
    virtual void *native_handle() const
    {
        return nullptr;
    }
    using async_io_handle::path;
    virtual filesystem::path path(bool refresh=false)
    {
      return "foo";
    }
    virtual directory_entry direntry(metadata_flags wanted=directory_entry::metadata_fastpath()) const
    {
        return directory_entry();
    }
    virtual filesystem::path target() const
    {
        return filesystem::path();
    }
    virtual void *try_mapfile()
    {
        return nullptr;
    }
};

BOOST_AFIO_AUTO_TEST_CASE(async_io_adopt, "Tests foreign fd adoption", 5)
{
    auto dispatcher = make_async_file_io_dispatcher(process_threadpool());
    std::cout << "\n\nTesting foreign fd adoption:\n";
    auto h=std::make_shared<test_handle>(dispatcher.get());
    auto adopted=dispatcher->adopt(h);
    BOOST_CHECK_NO_THROW(when_all(adopted).get());
}