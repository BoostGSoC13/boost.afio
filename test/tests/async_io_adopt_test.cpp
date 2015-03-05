#include "test_functions.hpp"

using namespace BOOST_AFIO_V1_NAMESPACE;
namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;
namespace afio = BOOST_AFIO_V1_NAMESPACE;

struct test_handle : async_io_handle
{
    test_handle(async_file_io_dispatcher_base *parent) : async_io_handle(parent, file_flags::None) {}
    virtual void close() override final
    {
        // Do nothing
    }
    virtual async_io_handle::open_states is_open() const override final
    {
      return async_io_handle::open_states::open;
    }
    virtual void *native_handle() const override final
    {
        return nullptr;
    }
    using async_io_handle::path;
    virtual afio::path path(bool refresh=false) override final
    {
      return "foo";
    }
    virtual afio::path path() const override final
    {
      return "foo";
    }
    virtual directory_entry direntry(metadata_flags wanted=directory_entry::metadata_fastpath()) override final
    {
        return directory_entry();
    }
    virtual afio::path target() override final
    {
        return afio::path();
    }
    virtual void *try_mapfile() override final
    {
        return nullptr;
    }
    virtual void link(const async_path_op_req &req) override final
    {
    }
    virtual void unlink() override final
    {
    }
    virtual void atomic_relink(const async_path_op_req &req) override final
    {
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