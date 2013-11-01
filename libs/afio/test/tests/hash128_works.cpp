TEST_CASE("Hash128/works", "Tests that niallsnasty128hash works")
{
	using namespace std;
	const string shouldbe("609f3fd85acc3bb4f8833ac53ab33458");
	auto scratch=unique_ptr<char>(new char[sizeof(random_)]);
	typedef std::chrono::duration<double, ratio<1>> secs_type;
	for(int n=0; n<100; n++)
	{
		memcpy(scratch.get(), random_, sizeof(random_));
	}
	{
		auto begin=chrono::high_resolution_clock::now();
		auto p=scratch.get();
		for(int n=0; n<1000; n++)
		{
			memcpy(p, random_, sizeof(random_));
		}
		auto end=chrono::high_resolution_clock::now();
		auto diff=chrono::duration_cast<secs_type>(end-begin);
		cout << "memcpy does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000ULL*sizeof(random_)) << " cycles/byte" << endl;
	}
	Hash128 hash;
	{
		auto begin=chrono::high_resolution_clock::now();
		for(int n=0; n<1000; n++)
		{
			hash.AddFastHashTo(random_, sizeof(random_));
		}
		auto end=chrono::high_resolution_clock::now();
		auto diff=chrono::duration_cast<secs_type>(end-begin);
		cout << "Niall's nasty 128 bit hash does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000ULL*sizeof(random_)) << " cycles/byte" << endl;
	}
	cout << "Hash is " << hash.asHexString() << endl;
	CHECK(shouldbe==hash.asHexString());
}
