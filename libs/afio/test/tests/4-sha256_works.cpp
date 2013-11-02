#include "../test_functions.hpp"

BOOST_AFIO_AUTO_TEST_CASE(SHA256_load, "Tests that SIMD load works", 3)
{
    union { uint32_t a[4]; __m128i b; } x;
    x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
    BOOST_CHECK(x.a[0] == 0xfee1baad);
    BOOST_CHECK(x.a[1] == 0x98765432);
    BOOST_CHECK(x.a[2] == 0xdeadbeef);
    BOOST_CHECK(x.a[3] == 0x12345678);
}

BOOST_AFIO_AUTO_TEST_CASE(SHA256_store, "Tests that SIMD store works", 3)
{
    union { uint32_t a[4]; __m128i b; } x;
    uint32_t c[4];
    x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
    store_epi32(x.b, &c[0], &c[1], &c[2], &c[3]);
    BOOST_CHECK(c[0] == 0x12345678);
    BOOST_CHECK(c[1] == 0xdeadbeef);
    BOOST_CHECK(c[2] == 0x98765432);
    BOOST_CHECK(c[3] == 0xfee1baad);
}

#if !BOOST_AFIO_HAVE_NEON128
BOOST_AFIO_AUTO_TEST_CASE(SHA256_shaload, "Tests that SHA LOAD works", 3)
{
    union { uint32_t a[4]; __m128i b; } x;
    uint32_t d[4]={ 0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad };
    const __sha256_block_t *blks[4]={ (__sha256_block_t *) (d+0), (__sha256_block_t *) (d+1), (__sha256_block_t *) (d+2), (__sha256_block_t *) (d+3) };
    x.b=LOAD(blks, 0);
    BOOST_CHECK(x.a[0] == 0xadbae1fe);
    BOOST_CHECK(x.a[1] == 0x32547698);
    BOOST_CHECK(x.a[2] == 0xefbeadde);
    BOOST_CHECK(x.a[3] == 0x78563412);
}
#endif

#if BOOST_AFIO_HAVE_NEON128
BOOST_AFIO_AUTO_TEST_CASE(SHA256_shaload4, "Tests that SHA LOAD4 works", 3)
{
    union { uint32_t a[4]; __m128i b; } x[4];
    uint32_t a[4]={ 0x11111111, 0x11111111, 0x11111111, 0x11111111 };
    uint32_t b[4]={ 0x22222222, 0x22222222, 0x22222222, 0x22222222 };
    uint32_t c[4]={ 0x33333333, 0x33333333, 0x33333333, 0x33333333 };
    uint32_t d[4]={ 0x44444444, 0x44444444, 0x44444444, 0x44444444 };
    const __sha256_block_t *blks[4]={ (__sha256_block_t *) (a), (__sha256_block_t *) (b), (__sha256_block_t *) (c), (__sha256_block_t *) (d) };
    LOAD4(&x[0].b, blks, 0);
    cout << "should: 0x78563412, 0xefbeadde, 0x32547698, 0xadbae1fe" << endl;
    cout << "     0: 0x" << hex << x[0].a[0] << " 0x" << x[0].a[1] << " 0x" << x[0].a[2] << " 0x" << x[0].a[3] << endl;
    cout << "     1: 0x" << hex << x[1].a[0] << " 0x" << x[1].a[1] << " 0x" << x[1].a[2] << " 0x" << x[1].a[3] << endl;
    cout << "     2: 0x" << hex << x[2].a[0] << " 0x" << x[2].a[1] << " 0x" << x[2].a[2] << " 0x" << x[2].a[3] << endl;
    cout << "     3: 0x" << hex << x[3].a[0] << " 0x" << x[3].a[1] << " 0x" << x[3].a[2] << " 0x" << x[3].a[3] << endl;
    BOOST_CHECK(x[0].a[0] == 0x78563412);
    BOOST_CHECK(x[0].a[1] == 0x78563412);
    BOOST_CHECK(x[0].a[2] == 0x78563412);
    BOOST_CHECK(x[0].a[3] == 0x78563412);
    BOOST_CHECK(x[1].a[0] == 0xefbeadde);
    BOOST_CHECK(x[1].a[1] == 0xefbeadde);
    BOOST_CHECK(x[1].a[2] == 0xefbeadde);
    BOOST_CHECK(x[1].a[3] == 0xefbeadde);
    BOOST_CHECK(x[2].a[0] == 0x32547698);
    BOOST_CHECK(x[2].a[1] == 0x32547698);
    BOOST_CHECK(x[2].a[2] == 0x32547698);
    BOOST_CHECK(x[2].a[3] == 0x32547698);
    BOOST_CHECK(x[3].a[0] == 0xadbae1fe);
    BOOST_CHECK(x[3].a[1] == 0xadbae1fe);
    BOOST_CHECK(x[3].a[2] == 0xadbae1fe);
    BOOST_CHECK(x[3].a[3] == 0xadbae1fe);
}
#endif

BOOST_AFIO_AUTO_TEST_CASE(SHA256_const, "Tests that SHA CONST works", 3)
{
    union { uint32_t a[4]; __m128i b; } x;
    x.b=SHA256_CONST(4);
    BOOST_CHECK(x.a[0] == 0x3956c25b);
    BOOST_CHECK(x.a[1] == 0x3956c25b);
    BOOST_CHECK(x.a[2] == 0x3956c25b);
    BOOST_CHECK(x.a[3] == 0x3956c25b);
}

BOOST_AFIO_AUTO_TEST_CASE(SHA256_BIGSIGMA0_256, "Tests that BIGSIGMA0_256 works", 3)
{
    union { uint32_t a[4]; __m128i b; } x;
    x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
    x.b=BIGSIGMA0_256(x.b);
    BOOST_CHECK(x.a[0] == 0x2c3d2e5d);
    BOOST_CHECK(x.a[1] == 0xded99cdf);
    BOOST_CHECK(x.a[2] == 0xb62e25ac);
    BOOST_CHECK(x.a[3] == 0x66146474);
}

BOOST_AFIO_AUTO_TEST_CASE(SHA256_BIGSIGMA1_256, "Tests that BIGSIGMA1_256 works", 3)
{
    union { uint32_t a[4]; __m128i b; } x;
    x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
    x.b=BIGSIGMA1_256(x.b);
    BOOST_CHECK(x.a[0] == 0x92990c22);
    BOOST_CHECK(x.a[1] == 0x7718ced6);
    BOOST_CHECK(x.a[2] == 0x345e14a3);
    BOOST_CHECK(x.a[3] == 0x3561abda);
}

BOOST_AFIO_AUTO_TEST_CASE(SHA256_SIGMA0_256, "Tests that SIGMA0_256 works", 3)
{
    union { uint32_t a[4]; __m128i b; } x;
    x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
    x.b=SIGMA0_256(x.b);
    BOOST_CHECK(x.a[0] == 0x2a8a8b98);
    BOOST_CHECK(x.a[1] == 0xe3328033);
    BOOST_CHECK(x.a[2] == 0xabd31b0b);
    BOOST_CHECK(x.a[3] == 0xe7fce6ee);
}

BOOST_AFIO_AUTO_TEST_CASE(SHA256_SIGMA1_256, "Tests that SIGMA1_256 works", 3)
{
    union { uint32_t a[4]; __m128i b; } x;
    x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
    x.b=SIGMA1_256(x.b);
    BOOST_CHECK(x.a[0] == 0xea3cf8c2);
    BOOST_CHECK(x.a[1] == 0xe0b902a0);
    BOOST_CHECK(x.a[2] == 0x689dbfec);
    BOOST_CHECK(x.a[3] == 0xa1f78649);
}

BOOST_AFIO_AUTO_TEST_CASE(SHA256_Ch, "Tests that Ch works", 3)
{
    __m128i a=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
    __m128i b=load_epi32(0xdeadbeef, 0x98765432, 0xfee1baad, 0x12345678);
    __m128i c=load_epi32(0x98765432, 0xfee1baad, 0x12345678, 0xdeadbeef);
    union { uint32_t a[4]; __m128i b; } x;
    x.b=Ch(a, b, c);
    BOOST_CHECK(x.a[0] == 0x122c166a);
    BOOST_CHECK(x.a[1] == 0x9a601268);
    BOOST_CHECK(x.a[2] == 0xb8641422);
    BOOST_CHECK(x.a[3] == 0x9a66166a);
}

BOOST_AFIO_AUTO_TEST_CASE(SHA256_Maj, "Tests that Maj works", 3)
{
    __m128i a=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
    __m128i b=load_epi32(0xdeadbeef, 0x98765432, 0xfee1baad, 0x12345678);
    __m128i c=load_epi32(0x98765432, 0xfee1baad, 0x12345678, 0xdeadbeef);
    union { uint32_t a[4]; __m128i b; } x;
    x.b=Maj(a, b, c);
    BOOST_CHECK(x.a[0] == 0xdea5beed);
    BOOST_CHECK(x.a[1] == 0x9a745638);
    BOOST_CHECK(x.a[2] == 0xdee5beaf);
    BOOST_CHECK(x.a[3] == 0x9a34567a);
}

BOOST_AFIO_AUTO_TEST_CASE(SHA256_works, "Tests that this SHA-256 works as per reference", 3)
{
    using namespace boost::afio::detail;
    // These are taken from the FIPS test for SHA-256. If this works, it's probably standards compliant.
    const char *tests[][2]={
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"The quick brown fox jumps over the lazy dog",  "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"},
        {"The quick brown fox jumps over the lazy dog.", "ef537f25c895bfa782526529a9b63d97aa631564d5d789c2b765448c8635fb6c"},
        {"Niall Douglas joined Research In Motion's Platform Development Division in October 2012, having formerly run his own expert consultancy firm in Ireland where he acted as the national representative on ISO's Programming Languages Steering Committee, and previously having worked in a number of firms and roles including as a Chief Software Architect on the EuroFighter defence aircraft's support systems. He holds two undergraduate degrees, one in Software Engineering and the other double majoring in Economics and Management, and holds postgraduate qualifications in Business Information Systems, Educational and Social Research and Pure Mathematics (in progress). He is an affiliate researcher with the University of Waterloo's Institute of Complexity and Innovation, and is the Social Media Coordinator for the World Economics Association, with a book recently published on Financial Economics by Anthem Press. In the past he has sat on a myriad of representative, political and regulatory committees across multiple organisations and has contributed many tens of thousands of lines of source code to multiple open source projects. He is well represented on expert technical forums, with several thousand posts made over the past decade.", "dcafcaa53f243decbe8a3d2a71ddec68936af1553f883f6299bb15de0e3616e2"}
    };
    for(size_t n=0; n<sizeof(tests)/sizeof(tests[0]); n++)
    {
        Hash256 hash;
        hash.AddSHA256To(tests[n][0], strlen(tests[n][0]));
        BOOST_CHECK(hash.asHexString()==tests[n][1]);
    }
    Hash256 hashes[4];
    const char *datas[4];
    size_t lengths[4];
    for(size_t n=0; n<4; n++)
    {
        datas[n]=tests[n][0];
        lengths[n]=strlen(datas[n]);
    }
    Hash256::BatchAddSHA256To(4, hashes, datas, lengths);
    for(size_t n=0; n<4; n++)
    {
        BOOST_CHECK(hashes[n].asHexString()==tests[n][1]);
    }
}
