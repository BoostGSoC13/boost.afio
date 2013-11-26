#include "../test_functions.hpp"
#include "../../../../boost/afio/hash_engine.hpp"

#ifndef RANDOM_DECLARED
#define RANDOM_DECLARED
static char random_[25*1024*1024];
#endif

/* Some interesting figures about the engine:

With 8 threads available:
  1-SHA256: single streams: 27.63 cycles/byte
  1-SHA256: max throughput:  7.70 cycles/byte
  4-SHA256: single streams: 27.05 cycles/byte
  4-SHA256: max throughput:  2.92 cycles/byte

*/

BOOST_AFIO_AUTO_TEST_CASE(async_hash_engine_works, "Tests that the hash engine works as per reference", 300)
{
	static const size_t LOAD_HASHES=256;
	typedef std::chrono::duration<double, std::ratio<1>> secs_type;

	ranctx gen;
	raninit(&gen, 0x78adbcff);
	for(size_t n=0; n<sizeof(random_)/sizeof(u4); n++)
	{
		((u4 *) random_)[n]=ranval(&gen);
	}

	using namespace boost::afio::hash;
	typedef async_hash_engine<SHA256> engine_t;
	engine_t engine(hash_threadpool());
    /* This is a SHA-256 validation test which ensures the correct hash is always generated
	no matter what length or combination of lengths is input. The residuals mean whether the
	engine correctly copes with the extra SHA termination round needed when the data length
	modulus 64 is >= 56, or indeed spots when it needs to early terminate when the modulus
	is < 56. Six strings with the longest last test if the engine correctly fuses an initial
	three threads of a 4-SHA256 plus two 1-SHA256 into a single 4-SHA256 thread.
	*/
    const char *tests[][2]={
		/* residual>=56 test */ {"Lorem ipsum dolor sit amet, at esse affert vis. Volutpat gloriatur dissentiunt te ius, velit volumus liberavisse an usu. Mediocrem adolescens usu id, tation offendit vim ne, an iriure ornatus pri. In verear salutatus nam. Mel no elitr omnes graeci, id postea evertitur maiestatis cum. Quas fabulas percipit his eu, wisi minimum mea te, his ex pericula forensibus. His impedit ad.", "45a31e2bd48adae4a03c6895d0ff24db3b8d61a1c8bb10f898de314c10bcda70"},
        /* null string test  */ {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
		/* residual=0 test   */ {"Lorem ipsum dolor sit amet, at esse affert vis. Volutpat gloriatur dissentiunt te ius, velit volumus liberavisse an usu. Mediocrem adolescens usu id, tation offendit vim ne, an iriure ornatus pri. In verear salutatus nam. Mel no elitr omnes graeci, id postea evertitur maiestatis cum. Quas fabulas percipit his eu, wisi minimum mea te, his ex pericula forensibus. His impedit ad hicd.", "9d35d8e7a50f20718abda597fbfb498f62e13b8aa1bf6e7d08e3b1bb1c8456c4"},
        /* residual<56 test  */ {"The quick brown fox jumps over the lazy dog",  "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"},
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
		std::cout << "Testing '" << reqs[n].second.data << "'" << std::endl;
		engine.add(reqs[n].first, reqs[n].second);
		engine.add(reqs[n].first, engine_t::block()); // terminate
		auto hash=o[n]->hash_value.get();
		auto hashstring=hash->asHexString();
		BOOST_CHECK(hashstring==tests[n][1]);
	}
	reqs.clear();

	// Test 4-hash
	std::cout << "\n4-SHA256 testing begins!" << std::endl;
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
		auto hashstring=hash->asHexString();
		BOOST_CHECK(hashstring==tests[n][1]);
	}
	reqs.clear();

	// Overhead testing of engine. Reference SHA-256 ought to push 14.89 cycles/byte on modern Intel
	std::cout << "\nSHA256 async engine overhead testing begins!" << std::endl;
	const std::string shouldbe("c622abd5eedbedcc9b661a28d7d56d599ad2dc1bebc9546500b72e3b3b667bc8");
	o=engine.begin(LOAD_HASHES/16);
	reqs.reserve(LOAD_HASHES);
	BOOST_FOREACH(auto &i, o)
	{
		reqs.push_back(std::make_pair(i, engine_t::block(random_, sizeof(random_))));
	}
	auto begin=std::chrono::high_resolution_clock::now();
	BOOST_FOREACH(auto &i, reqs)
	{
		engine.add(i.first, i.second);
		engine.add(i.first, engine_t::block()); // terminate
		auto hash=i.first->hash_value.get();
		auto hashstring=hash->asHexString();
		BOOST_CHECK(hashstring==shouldbe);
	}
	auto end=std::chrono::high_resolution_clock::now();
	auto diff=std::chrono::duration_cast<secs_type>(end-begin);
	std::cout << "SHA-256 hash engine does single streams at " << (CPU_CYCLES_PER_SEC*diff.count())/(LOAD_HASHES/16*sizeof(random_)) << " cycles/byte" << std::endl;
	reqs.clear();

	// Test engine under much load
	std::cout << "\n4-SHA256 load testing begins!" << std::endl;
	o=engine.begin(LOAD_HASHES);
	BOOST_FOREACH(auto &i, o)
	{
		reqs.push_back(std::make_pair(i, engine_t::block(random_, sizeof(random_))));
		reqs.push_back(std::make_pair(i, engine_t::block())); // terminate
	}
	engine.add(reqs);
	begin=std::chrono::high_resolution_clock::now();
	BOOST_FOREACH(auto &i, o)
	{
		auto hash=i->hash_value.get();
		auto hashstring=hash->asHexString();
		BOOST_CHECK(hashstring==shouldbe);
	}
	end=std::chrono::high_resolution_clock::now();
	diff=std::chrono::duration_cast<secs_type>(end-begin);
	std::cout << "SHA-256 hash engine does a maximum of " << (CPU_CYCLES_PER_SEC*diff.count())/(LOAD_HASHES*sizeof(random_)) << " cycles/byte" << std::endl;
	reqs.clear();

}
