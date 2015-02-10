#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(async_io_pagesize, "Tests that the page size works, and other minor functions", 3)
{
    using namespace BOOST_AFIO_V1_NAMESPACE;
    namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;
    try
    {
        auto dispatcher=make_async_file_io_dispatcher();
        std::cout << "\n\nSystem page sizes are: " << std::endl;
        for(auto &i : dispatcher->page_sizes())
          std::cout << "  " << i << " bytes" << std::endl;
        std::cout << "\n\nThread source use count is: " << dispatcher->threadsource().use_count() << std::endl;
        BOOST_AFIO_CHECK_THROWS(dispatcher->op_from_scheduled_id(78));
    }
    catch(...)
    {
        std::cerr << "Exception thrown." << std::endl;
        BOOST_CHECK(false);
    }
}