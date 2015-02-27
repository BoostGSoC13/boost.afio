#include "afio_pch.hpp"

//[adopt_example
struct test_handle : boost::afio::async_io_handle
{
    test_handle(boost::afio::async_file_io_dispatcher_base *parent) :
        boost::afio::async_io_handle(parent,
        boost::afio::file_flags::None) {}
    virtual void close() override final
    {
        // Do nothing
    }
    virtual void *native_handle() const override final
    {
        return nullptr;
    }
    virtual boost::afio::path path(bool refresh=false) override final
    {
        return boost::afio::path();
    }
    virtual boost::afio::path path() const override final
    {
        return boost::afio::path();
    }
    virtual boost::afio::directory_entry direntry(boost::afio::metadata_flags
        wanted=boost::afio::directory_entry::metadata_fastpath()) override final
    {
        return boost::afio::directory_entry();
    }
    virtual boost::afio::path target() override final
    {
        return boost::afio::path();
    }
    virtual void *try_mapfile() override final
    {
        return nullptr;
    }
};

int main(void)
{
    auto dispatcher = boost::afio::make_async_file_io_dispatcher(
        boost::afio::process_threadpool());
    auto h=std::make_shared<test_handle>(dispatcher.get());
    auto adopted=dispatcher->adopt(h);
    when_all(adopted).wait();
    return 0;
}
//]
