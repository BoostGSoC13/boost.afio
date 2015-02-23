#include "test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(async_io_pagesize, "Tests that the utility functions work", 30)
{
    using namespace BOOST_AFIO_V1_NAMESPACE;
    namespace asio = BOOST_AFIO_V1_NAMESPACE::asio;
    typedef chrono::duration<double, ratio<1, 1>> secs_type;

    std::cout << "\n\nSystem page sizes are: " << std::endl;
    for(auto &i : utils::page_sizes(false))
      std::cout << "  " << i << " bytes" << std::endl;
    BOOST_CHECK(!utils::page_sizes(false).empty());
    std::cout << "\n\nActually available system page sizes are: " << std::endl;
    for(auto &i : utils::page_sizes(true))
      std::cout << "  " << i << " bytes" << std::endl;
    BOOST_CHECK(!utils::page_sizes(true).empty());
    
    std::vector<char, utils::file_buffer_allocator<char>> fba(8*1024*1024);
    auto fba_detail(utils::detail::calculate_large_page_allocation(8*1024*1024));
    std::cout << "\n\nAllocating 8Mb with the file buffer allocator yields an address at " << ((void *) fba.data())
              << " and may use pages of " << fba_detail.page_size_used << " and be actually "
              << fba_detail.actual_size << " bytes allocated." << std::endl;
    
    auto randomstring(utils::random_string(32));
    std::cout << "\n\n256 bits of random string might be: " << randomstring << " which is " << randomstring.size() << " bytes long." << std::endl;
    BOOST_CHECK(randomstring.size()==43);
    auto begin=chrono::high_resolution_clock::now();
    while(chrono::duration_cast<secs_type>(chrono::high_resolution_clock::now()-begin).count()<3);
    static const size_t ITEMS=1000000;
    std::vector<char> buffer(32*ITEMS, ' ');
    begin=chrono::high_resolution_clock::now();
    for(size_t n=0; n<buffer.size()/32; n++)
      utils::random_fill(buffer.data()+n*32, 32);
    auto end=chrono::high_resolution_clock::now();
    auto diff=chrono::duration_cast<secs_type>(end-begin);
    std::cout << "\n\nKernel can generate " << (buffer.size()/diff.count()/1024/1024) << " Mb/sec of 256 bit cryptographic randomness" << std::endl;

    std::vector<std::vector<char>> filenames1(ITEMS, std::vector<char>(64, ' '));
    begin=chrono::high_resolution_clock::now();
    for(size_t n=0; n<ITEMS; n++)
      utils::to_hex_string(const_cast<char *>(filenames1[n].data()), 64, buffer.data()+n*32, 32);
    end=chrono::high_resolution_clock::now();
    diff=chrono::duration_cast<secs_type>(end-begin);
    std::cout << "\n\nto_hex_string can convert " << (ITEMS*64/diff.count()/1024/1024) << " Mb/sec of 256 bit numbers to hex" << std::endl;

    std::vector<std::vector<char>> filenames2(ITEMS, std::vector<char>(43, ' '));
    begin=chrono::high_resolution_clock::now();
    for(size_t n=0; n<ITEMS; n++)
      utils::to_compact_string(const_cast<char *>(filenames2[n].data()), 43, buffer.data()+n*32, 32);
    end=chrono::high_resolution_clock::now();
    diff=chrono::duration_cast<secs_type>(end-begin);
    std::cout << "\n\nto_compact_string can convert " << (ITEMS*43/diff.count()/1024/1024) << " Mb/sec of 256 bit numbers to compact" << std::endl;

    std::vector<char> buffer1(32*ITEMS, ' ');
    begin=chrono::high_resolution_clock::now();
    for(size_t n=0; n<ITEMS; n++)
      utils::from_hex_string(buffer1.data()+n*32, 32, filenames1[n].data(), 64);
    end=chrono::high_resolution_clock::now();
    diff=chrono::duration_cast<secs_type>(end-begin);
    std::cout << "\n\nfrom_hex_string can convert " << (ITEMS*64/diff.count()/1024/1024) << " Mb/sec of hex to 256 bit numbers" << std::endl;

    std::vector<char> buffer2(32*ITEMS, ' ');
    begin=chrono::high_resolution_clock::now();
    for(size_t n=0; n<ITEMS; n++)
      utils::from_compact_string(buffer2.data()+n*32, 32, filenames2[n].data(), 43);
    end=chrono::high_resolution_clock::now();
    diff=chrono::duration_cast<secs_type>(end-begin);
    std::cout << "\n\nfrom_compact_string can convert " << (ITEMS*43/diff.count()/1024/1024) << " Mb/sec of compact to 256 bit numbers" << std::endl;

    BOOST_CHECK(!memcmp(buffer.data(), buffer1.data(), buffer.size()));    
    BOOST_CHECK(!memcmp(buffer.data(), buffer2.data(), buffer.size()));    

    auto dispatcher=make_async_file_io_dispatcher();
    std::cout << "\n\nThread source use count is: " << dispatcher->threadsource().use_count() << std::endl;
    BOOST_AFIO_CHECK_THROWS(dispatcher->op_from_scheduled_id(78));
}