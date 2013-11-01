/* NiallsCPP11Utilities SIMD unit tests
(C) 2013 Niall Douglas http://www.nedprod.com/
File Created: April 2013
*/

#define CATCH_CONFIG_RUNNER
#include "NiallsCPP11Utilities.hpp"
#include <fstream>
#include "Int128_256.hpp"
#include "catch.hpp"
#if HAVE_M128
#include "hashes/sha256/sha256-sse.c"
#endif
#if HAVE_NEON128
#include "hashes/sha256/sha256-neon.c"
#endif

using namespace NiallsCPP11Utilities;
using namespace std;

int main(int argc, char * const argv[]) {
	int ret=Catch::Main(argc, argv);
#ifdef _MSC_VER
	printf("Press Return to exit ...\n");
	getchar();
#endif
	return ret;
}

TEST_CASE("SHA256/load", "Tests that SIMD load works")
{
	union { uint32_t a[4]; __m128i b; } x;
	x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
	CHECK(x.a[0] == 0xfee1baad);
	CHECK(x.a[1] == 0x98765432);
	CHECK(x.a[2] == 0xdeadbeef);
	CHECK(x.a[3] == 0x12345678);
}

TEST_CASE("SHA256/store", "Tests that SIMD store works")
{
	union { uint32_t a[4]; __m128i b; } x;
	uint32_t c[4];
	x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
	store_epi32(x.b, &c[0], &c[1], &c[2], &c[3]);
	CHECK(c[0] == 0x12345678);
	CHECK(c[1] == 0xdeadbeef);
	CHECK(c[2] == 0x98765432);
	CHECK(c[3] == 0xfee1baad);
}

#if !HAVE_NEON128
TEST_CASE("SHA256/shaload", "Tests that SHA LOAD works")
{
	union { uint32_t a[4]; __m128i b; } x;
	uint32_t d[4]={ 0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad };
	const __sha256_block_t *blks[4]={ (__sha256_block_t *) (d+0), (__sha256_block_t *) (d+1), (__sha256_block_t *) (d+2), (__sha256_block_t *) (d+3) };
	x.b=LOAD(blks, 0);
	CHECK(x.a[0] == 0xadbae1fe);
	CHECK(x.a[1] == 0x32547698);
	CHECK(x.a[2] == 0xefbeadde);
	CHECK(x.a[3] == 0x78563412);
}
#endif

#if HAVE_NEON128
TEST_CASE("SHA256/shaload4", "Tests that SHA LOAD4 works")
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
	CHECK(x[0].a[0] == 0x78563412);
	CHECK(x[0].a[1] == 0x78563412);
	CHECK(x[0].a[2] == 0x78563412);
	CHECK(x[0].a[3] == 0x78563412);
	CHECK(x[1].a[0] == 0xefbeadde);
	CHECK(x[1].a[1] == 0xefbeadde);
	CHECK(x[1].a[2] == 0xefbeadde);
	CHECK(x[1].a[3] == 0xefbeadde);
	CHECK(x[2].a[0] == 0x32547698);
	CHECK(x[2].a[1] == 0x32547698);
	CHECK(x[2].a[2] == 0x32547698);
	CHECK(x[2].a[3] == 0x32547698);
	CHECK(x[3].a[0] == 0xadbae1fe);
	CHECK(x[3].a[1] == 0xadbae1fe);
	CHECK(x[3].a[2] == 0xadbae1fe);
	CHECK(x[3].a[3] == 0xadbae1fe);
}
#endif

TEST_CASE("SHA256/const", "Tests that SHA CONST works")
{
	union { uint32_t a[4]; __m128i b; } x;
	x.b=SHA256_CONST(4);
	CHECK(x.a[0] == 0x3956c25b);
	CHECK(x.a[1] == 0x3956c25b);
	CHECK(x.a[2] == 0x3956c25b);
	CHECK(x.a[3] == 0x3956c25b);
}

TEST_CASE("SHA256/BIGSIGMA0_256", "Tests that BIGSIGMA0_256 works")
{
	union { uint32_t a[4]; __m128i b; } x;
	x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
	x.b=BIGSIGMA0_256(x.b);
	CHECK(x.a[0] == 0x2c3d2e5d);
	CHECK(x.a[1] == 0xded99cdf);
	CHECK(x.a[2] == 0xb62e25ac);
	CHECK(x.a[3] == 0x66146474);
}

TEST_CASE("SHA256/BIGSIGMA1_256", "Tests that BIGSIGMA1_256 works")
{
	union { uint32_t a[4]; __m128i b; } x;
	x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
	x.b=BIGSIGMA1_256(x.b);
	CHECK(x.a[0] == 0x92990c22);
	CHECK(x.a[1] == 0x7718ced6);
	CHECK(x.a[2] == 0x345e14a3);
	CHECK(x.a[3] == 0x3561abda);
}

TEST_CASE("SHA256/SIGMA0_256", "Tests that SIGMA0_256 works")
{
	union { uint32_t a[4]; __m128i b; } x;
	x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
	x.b=SIGMA0_256(x.b);
	CHECK(x.a[0] == 0x2a8a8b98);
	CHECK(x.a[1] == 0xe3328033);
	CHECK(x.a[2] == 0xabd31b0b);
	CHECK(x.a[3] == 0xe7fce6ee);
}

TEST_CASE("SHA256/SIGMA1_256", "Tests that SIGMA1_256 works")
{
	union { uint32_t a[4]; __m128i b; } x;
	x.b=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
	x.b=SIGMA1_256(x.b);
	CHECK(x.a[0] == 0xea3cf8c2);
	CHECK(x.a[1] == 0xe0b902a0);
	CHECK(x.a[2] == 0x689dbfec);
	CHECK(x.a[3] == 0xa1f78649);
}

TEST_CASE("SHA256/Ch", "Tests that Ch works")
{
	__m128i a=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
	__m128i b=load_epi32(0xdeadbeef, 0x98765432, 0xfee1baad, 0x12345678);
	__m128i c=load_epi32(0x98765432, 0xfee1baad, 0x12345678, 0xdeadbeef);
	union { uint32_t a[4]; __m128i b; } x;
	x.b=Ch(a, b, c);
	CHECK(x.a[0] == 0x122c166a);
	CHECK(x.a[1] == 0x9a601268);
	CHECK(x.a[2] == 0xb8641422);
	CHECK(x.a[3] == 0x9a66166a);
}

TEST_CASE("SHA256/Maj", "Tests that Maj works")
{
	__m128i a=load_epi32(0x12345678, 0xdeadbeef, 0x98765432, 0xfee1baad);
	__m128i b=load_epi32(0xdeadbeef, 0x98765432, 0xfee1baad, 0x12345678);
	__m128i c=load_epi32(0x98765432, 0xfee1baad, 0x12345678, 0xdeadbeef);
	union { uint32_t a[4]; __m128i b; } x;
	x.b=Maj(a, b, c);
	CHECK(x.a[0] == 0xdea5beed);
	CHECK(x.a[1] == 0x9a745638);
	CHECK(x.a[2] == 0xdee5beaf);
	CHECK(x.a[3] == 0x9a34567a);
}

TEST_CASE("SHA256/works", "Tests that this SHA-256 works as per reference")
{
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
		CHECK(hash.asHexString()==tests[n][1]);
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
		CHECK(hashes[n].asHexString()==tests[n][1]);
	}
}
