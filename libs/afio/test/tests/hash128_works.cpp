#include "../test_functions.hpp"

#ifndef RANDOM_DECLARED
#define RANDOM_DECLARED
static char random_[25*1024*1024];
#endif

BOOST_AFIO_AUTO_TEST_CASE(Hash128_works, "Tests that niallsnasty128hash works", 3)
{
    using namespace boost::afio::detail;
    const std::string shouldbe("609f3fd85acc3bb4f8833ac53ab33458");
    auto scratch=std::unique_ptr<char>(new char[sizeof(random_)]);
    typedef std::chrono::duration<double, std::ratio<1>> secs_type;
    for(int n=0; n<100; n++)
    {
        memcpy(scratch.get(), random_, sizeof(random_));
    }
    {
        auto begin=std::chrono::high_resolution_clock::now();
        auto p=scratch.get();
        for(int n=0; n<1000; n++)
        {
            memcpy(p, random_, sizeof(random_));
        }
        auto end=std::chrono::high_resolution_clock::now();
        auto diff=std::chrono::duration_cast<secs_type>(end-begin);
        std::cout << "memcpy does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000ULL*sizeof(random_)) << " cycles/byte" << std::endl;
    }
    Hash128 hash;
    {
        auto begin=std::chrono::high_resolution_clock::now();
        for(int n=0; n<1000; n++)
        {
            hash.AddFastHashTo(random_, sizeof(random_));
        }
        auto end=std::chrono::high_resolution_clock::now();
        auto diff=std::chrono::duration_cast<secs_type>(end-begin);
        std::cout << "Niall's nasty 128 bit hash does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000ULL*sizeof(random_)) << " cycles/byte" << std::endl;
    }
    std::cout << "Hash is " << hash.asHexString() << endl;
    BOOST_CHECK(shouldbe==hash.asHexString());
}
