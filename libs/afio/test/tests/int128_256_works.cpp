#include "../test_functions.hpp"

#define BOOST_CHECK_FALSE(a) BOOST_CHECK(!(a))

BOOST_AFIO_AUTO_TEST_CASE(Int128_works, "Tests that Int128 works", 240)
{
    using namespace boost::afio::detail;
    char _hash1[16], _hash2[16];
    memset(_hash1, 0, sizeof(_hash1));
    memset(_hash2, 0, sizeof(_hash2));
    _hash1[5]=78;
    _hash2[15]=1;
    Int128 hash1(_hash1), hash2(_hash2), null;
    std::cout << "hash1=0x" << hash1.asHexString() << std::endl;
    std::cout << "hash2=0x" << hash2.asHexString() << std::endl;
    BOOST_CHECK(hash1==hash1);
    BOOST_CHECK(hash2==hash2);
    BOOST_CHECK(null==null);
    BOOST_CHECK(hash1!=null);
    BOOST_CHECK(hash2!=null);
    BOOST_CHECK(hash1!=hash2);

    BOOST_CHECK(hash1>hash2);
    BOOST_CHECK_FALSE(hash1<hash2);
    BOOST_CHECK(hash2<hash1);
    BOOST_CHECK_FALSE(hash2>hash1);

    BOOST_CHECK(hash1>=hash2);
    BOOST_CHECK_FALSE(hash1<=hash2);
    BOOST_CHECK(hash1<=hash1);
    BOOST_CHECK_FALSE(hash1<hash1);
    BOOST_CHECK(hash2<=hash2);
    BOOST_CHECK_FALSE(hash2<hash2);

    BOOST_CHECK(alignment_of<Int128>::value==16);
    std::vector<Int128> hashes(4096);
    BOOST_CHECK(std::vector<Int128>::allocator_type::alignment==16);

    {
        typedef std::chrono::duration<double, std::ratio<1>> secs_type;
        auto begin=std::chrono::high_resolution_clock::now();
        for(int m=0; m<10000; m++)
            Int128::FillFastRandom(hashes);
        auto end=std::chrono::high_resolution_clock::now();
        auto diff=std::chrono::duration_cast<secs_type>(end-begin);
        std::cout << "FillFastRandom 128-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(10000*hashes.size()*sizeof(Int128)) << " cycles/byte" << std::endl;
    }
    {
        typedef std::chrono::duration<double, std::ratio<1>> secs_type;
        auto begin=std::chrono::high_resolution_clock::now();
        for(int m=0; m<10000; m++)
            Int128::FillQualityRandom(hashes);
        auto end=std::chrono::high_resolution_clock::now();
        auto diff=std::chrono::duration_cast<secs_type>(end-begin);
        std::cout << "FillQualityRandom 128-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(10000*hashes.size()*sizeof(Int128)) << " cycles/byte" << std::endl;
    }
    std::vector<char> comparisons1(hashes.size());
    {
        typedef std::chrono::duration<double, std::ratio<1>> secs_type;
        auto begin=std::chrono::high_resolution_clock::now();
        for(int m=0; m<1000; m++)
            for(size_t n=0; n<hashes.size()-1; n++)
                comparisons1[n]=hashes[n]>hashes[n+1];
        auto end=std::chrono::high_resolution_clock::now();
        auto diff=std::chrono::duration_cast<secs_type>(end-begin);
        std::cout << "Comparisons 128-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000*(hashes.size()-1)) << " cycles/op" << std::endl;
    }
    std::vector<char> comparisons2(hashes.size());
    {
        typedef std::chrono::duration<double, std::ratio<1>> secs_type;
        auto begin=std::chrono::high_resolution_clock::now();
        for(int m=0; m<1000; m++)
            for(size_t n=0; n<hashes.size()-1; n++)
                comparisons2[n]=memcmp(&hashes[n], &hashes[n+1], sizeof(hashes[n]))>0;
        auto end=std::chrono::high_resolution_clock::now();
        auto diff=std::chrono::duration_cast<secs_type>(end-begin);
        std::cout << "Comparisons memcmp does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000*(hashes.size()-1)) << " cycles/op" << std::endl;
    }
    BOOST_CHECK((comparisons1==comparisons2));
}

BOOST_AFIO_AUTO_TEST_CASE(Int256_works, "Tests that Int256 works", 240)
{
    using namespace boost::afio::detail;
    char _hash1[32], _hash2[32];
    memset(_hash1, 0, sizeof(_hash1));
    memset(_hash2, 0, sizeof(_hash2));
    _hash1[5]=78;
    _hash2[31]=1;
    Int256 hash1(_hash1), hash2(_hash2), null;
    std::cout << "hash1=0x" << hash1.asHexString() << std::endl;
    std::cout << "hash2=0x" << hash2.asHexString() << std::endl;
    BOOST_CHECK(hash1==hash1);
    BOOST_CHECK(hash2==hash2);
    BOOST_CHECK(null==null);
    BOOST_CHECK(hash1!=null);
    BOOST_CHECK(hash2!=null);
    BOOST_CHECK(hash1!=hash2);

    BOOST_CHECK(hash1>hash2);
    BOOST_CHECK_FALSE(hash1<hash2);
    BOOST_CHECK(hash2<hash1);
    BOOST_CHECK_FALSE(hash2>hash1);

    BOOST_CHECK(hash1>=hash2);
    BOOST_CHECK_FALSE(hash1<=hash2);
    BOOST_CHECK(hash1<=hash1);
    BOOST_CHECK_FALSE(hash1<hash1);
    BOOST_CHECK(hash2<=hash2);
    BOOST_CHECK_FALSE(hash2<hash2);

    BOOST_CHECK(alignment_of<Int256>::value==32);
    std::vector<Int256> hashes(4096);
    BOOST_CHECK(std::vector<Int256>::allocator_type::alignment==32);

    {
        typedef std::chrono::duration<double, std::ratio<1>> secs_type;
        auto begin=std::chrono::high_resolution_clock::now();
        for(int m=0; m<10000; m++)
            Int256::FillFastRandom(hashes);
        auto end=std::chrono::high_resolution_clock::now();
        auto diff=std::chrono::duration_cast<secs_type>(end-begin);
        std::cout << "FillFastRandom 256-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(10000*hashes.size()*sizeof(Int256)) << " cycles/byte" << std::endl;
    }
    {
        typedef std::chrono::duration<double, std::ratio<1>> secs_type;
        auto begin=std::chrono::high_resolution_clock::now();
        for(int m=0; m<10000; m++)
            Int256::FillQualityRandom(hashes);
        auto end=std::chrono::high_resolution_clock::now();
        auto diff=std::chrono::duration_cast<secs_type>(end-begin);
        std::cout << "FillQualityRandom 256-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(10000*hashes.size()*sizeof(Int256)) << " cycles/byte" << std::endl;
    }
    std::vector<char> comparisons1(hashes.size());
    {
        typedef std::chrono::duration<double, std::ratio<1>> secs_type;
        auto begin=std::chrono::high_resolution_clock::now();
        for(int m=0; m<1000; m++)
            for(size_t n=0; n<hashes.size()-1; n++)
                comparisons1[n]=hashes[n]>hashes[n+1];
        auto end=std::chrono::high_resolution_clock::now();
        auto diff=std::chrono::duration_cast<secs_type>(end-begin);
        std::cout << "Comparisons 256-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000*(hashes.size()-1)) << " cycles/op" << std::endl;
    }
    std::vector<char> comparisons2(hashes.size());
    {
        typedef std::chrono::duration<double, std::ratio<1>> secs_type;
        auto begin=std::chrono::high_resolution_clock::now();
        for(int m=0; m<1000; m++)
            for(size_t n=0; n<hashes.size()-1; n++)
                comparisons2[n]=memcmp(&hashes[n], &hashes[n+1], sizeof(hashes[n]))>0;
        auto end=std::chrono::high_resolution_clock::now();
        auto diff=std::chrono::duration_cast<secs_type>(end-begin);
        std::cout << "Comparisons memcmp does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000*(hashes.size()-1)) << " cycles/op" << std::endl;
    }
    BOOST_CHECK((comparisons1==comparisons2));
}
