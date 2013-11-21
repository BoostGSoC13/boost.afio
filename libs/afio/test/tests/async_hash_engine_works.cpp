#include "../test_functions.hpp"
#include "../../../../boost/afio/hash_engine.hpp"

BOOST_AFIO_AUTO_TEST_CASE(async_hash_engine_works, "Tests that the hash engine works as per reference", 300)
{
    using namespace boost::afio::hash;
	typedef async_hash_engine<SHA256> engine_t;
	engine_t engine(hash_threadpool());
    // These are taken from the FIPS test for SHA-256. If this works, it's probably standards compliant.
    const char *tests[][2]={
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"The quick brown fox jumps over the lazy dog",  "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"},
        {"The quick brown fox jumps over the lazy dog.", "ef537f25c895bfa782526529a9b63d97aa631564d5d789c2b765448c8635fb6c"},
        {"Niall Douglas joined Research In Motion's Platform Development Division in October 2012, having formerly run his own expert consultancy firm in Ireland where he acted as the national representative on ISO's Programming Languages Steering Committee, and previously having worked in a number of firms and roles including as a Chief Software Architect on the EuroFighter defence aircraft's support systems. He holds two undergraduate degrees, one in Software Engineering and the other double majoring in Economics and Management, and holds postgraduate qualifications in Business Information Systems, Educational and Social Research and Pure Mathematics (in progress). He is an affiliate researcher with the University of Waterloo's Institute of Complexity and Innovation, and is the Social Media Coordinator for the World Economics Association, with a book recently published on Financial Economics by Anthem Press. In the past he has sat on a myriad of representative, political and regulatory committees across multiple organisations and has contributed many tens of thousands of lines of source code to multiple open source projects. He is well represented on expert technical forums, with several thousand posts made over the past decade.", "dcafcaa53f243decbe8a3d2a71ddec68936af1553f883f6299bb15de0e3616e2"}
    };
	size_t testslen=sizeof(tests)/sizeof(tests[0]);
	std::vector<std::pair<engine_t::op_t, engine_t::block>> reqs; reqs.reserve(testslen);

	// Test 1-hash
	auto o=engine.begin(testslen);
	for(size_t n=0; n<testslen; n++)
		reqs.push_back(std::make_pair(o[n], engine_t::block(tests[n][0], strlen(tests[n][0]))));
	for(size_t n=0; n<testslen; n++)
	{
		engine.add(reqs[n].first, reqs[n].second);
		engine.add(reqs[n].first, engine_t::block()); // terminate
		auto hash=o[n]->hash_value.get();
		BOOST_CHECK(hash->asHexString()==tests[n][1]);
	}
	reqs.clear();
	// Test 4-hash
	o=engine.begin(testslen);
	for(size_t n=0; n<testslen; n++)
	{
		reqs.push_back(std::make_pair(o[n], engine_t::block(tests[n][0], strlen(tests[n][0]))));
		reqs.push_back(std::make_pair(o[n], engine_t::block())); // terminate
	}
	engine.add(reqs);
	for(size_t n=0; n<testslen; n++)
	{
		auto hash=o[n]->hash_value.get();
		BOOST_CHECK(hash->asHexString()==tests[n][1]);
	}
}
