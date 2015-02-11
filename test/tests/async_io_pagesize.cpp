#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(async_io_pagesize, "Tests that the page size works, and other minor functions", 3)
{
    using namespace BOOST_AFIO_V1_NAMESPACE;
    namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;

    auto dispatcher=make_async_file_io_dispatcher();
    std::cout << "\n\nSystem page sizes are: " << std::endl;
    for(auto &i : dispatcher->page_sizes(false))
      std::cout << "  " << i << " bytes" << std::endl;
    BOOST_CHECK(!dispatcher->page_sizes(false).empty());
    std::cout << "\n\nActually available system page sizes are: " << std::endl;
    for(auto &i : dispatcher->page_sizes(true))
      std::cout << "  " << i << " bytes" << std::endl;
    BOOST_CHECK(!dispatcher->page_sizes(true).empty());
    auto randomstring(dispatcher->random_string(32));
    std::cout << "\n\nA 32 byte random hex string might be: " << randomstring << std::endl;
    BOOST_CHECK(randomstring.size()==32);
    std::vector<char, file_buffer_allocator<char>> fba(8*1024*1024);
    auto fba_detail(detail::calculate_large_page_allocation(8*1024*1024));
    std::cout << "\n\nAllocating 8Mb with the file buffer allocator yields an address at " << ((void *) fba.data())
              << " and may use pages of " << fba_detail.page_size_used << " and be actually "
              << fba_detail.actual_size << " bytes allocated." << std::endl;
    std::cout << "\n\nThread source use count is: " << dispatcher->threadsource().use_count() << std::endl;
    BOOST_AFIO_CHECK_THROWS(dispatcher->op_from_scheduled_id(78));
}