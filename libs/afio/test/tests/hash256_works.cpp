#include "../test_functions.hpp"

#ifndef RANDOM_DECLARED
#define RANDOM_DECLARED
static char random_[25*1024*1024];
#endif

BOOST_AFIO_AUTO_TEST_CASE(Hash256_works, "Tests that niallsnasty256hash works", 60)
{
    using namespace boost::afio::detail;
    auto scratch=std::unique_ptr<char>(new char[sizeof(random_)]);
    typedef std::chrono::duration<double, std::ratio<1>> secs_type;
    double SHA256cpb, BatchSHA256cpb;
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
    {
        const std::string shouldbe("609f3fd85acc3bb4f8833ac53ab3345823dc6462d245a5830fe001a9767d09f0");
        Hash256 hash;
        {
            auto begin=std::chrono::high_resolution_clock::now();
            for(int n=0; n<1000; n++)
            {
                hash.AddFastHashTo(random_, sizeof(random_));
            }
            auto end=std::chrono::high_resolution_clock::now();
            auto diff=std::chrono::duration_cast<secs_type>(end-begin);
            std::cout << "Niall's nasty 256 bit hash does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000ULL*sizeof(random_)) << " cycles/byte" << std::endl;
        }
        std::cout << "Hash is " << hash.asHexString() << endl;
        BOOST_CHECK(shouldbe==hash.asHexString());
    }
    {
        const std::string shouldbe("ea1483962ca908676335418b06b6f98603d3d32b0962cda299a81cacdb5b1cb0");
        Hash256 hash;
        {
            auto begin=std::chrono::high_resolution_clock::now();
            for(int n=0; n<100; n++)
            {
                hash.AddSHA256To(random_, sizeof(random_));
            }
            auto end=std::chrono::high_resolution_clock::now();
            auto diff=std::chrono::duration_cast<secs_type>(end-begin);
            std::cout << "Reference SHA-256 hash does " << (CPU_CYCLES_PER_SEC*diff.count())/(100*sizeof(random_)) << " cycles/byte" << std::endl;
            SHA256cpb=(CPU_CYCLES_PER_SEC*diff.count())/(100*sizeof(random_));
        }
        std::cout << "Hash is " << hash.asHexString() << std::endl;
        BOOST_CHECK(shouldbe==hash.asHexString());
    }
    {
        const std::string shouldbe("ea1483962ca908676335418b06b6f98603d3d32b0962cda299a81cacdb5b1cb0");
        Hash256 hashes[4];
        const char *datas[4]={random_, random_, random_, random_};
        size_t lengths[4]={sizeof(random_), sizeof(random_), sizeof(random_), sizeof(random_)};
        {
            auto begin=std::chrono::high_resolution_clock::now();
            for(int n=0; n<100; n++)
            {
                Hash256::BatchAddSHA256To(4, hashes, datas, lengths);
            }
            auto end=std::chrono::high_resolution_clock::now();
            auto diff=std::chrono::duration_cast<secs_type>(end-begin);
            std::cout << "Batch SHA-256 hash does " << (CPU_CYCLES_PER_SEC*diff.count())/(100ULL*4*sizeof(random_)) << " cycles/byte" << std::endl;
            BatchSHA256cpb=(CPU_CYCLES_PER_SEC*diff.count())/(100ULL*4*sizeof(random_));
            std::cout << "   ... which is " << ((SHA256cpb-BatchSHA256cpb)*100/SHA256cpb) << "% faster than the straight SHA-256." << std::endl;
        }
        std::cout << "Hash is " << hashes[0].asHexString() << std::endl;
        BOOST_CHECK(shouldbe==hashes[0].asHexString());
        BOOST_CHECK(shouldbe==hashes[1].asHexString());
        BOOST_CHECK(shouldbe==hashes[2].asHexString());
        BOOST_CHECK(shouldbe==hashes[3].asHexString());
    }
}

