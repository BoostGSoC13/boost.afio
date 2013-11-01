TEST_CASE("Int128/works", "Tests that Int128 works")
{
	char _hash1[16], _hash2[16];
	memset(_hash1, 0, sizeof(_hash1));
	memset(_hash2, 0, sizeof(_hash2));
	_hash1[5]=78;
	_hash2[15]=1;
	Int128 hash1(_hash1), hash2(_hash2), null;
	cout << "hash1=0x" << hash1.asHexString() << endl;
	cout << "hash2=0x" << hash2.asHexString() << endl;
	CHECK(hash1==hash1);
	CHECK(hash2==hash2);
	CHECK(null==null);
	CHECK(hash1!=null);
	CHECK(hash2!=null);
	CHECK(hash1!=hash2);

	CHECK(hash1>hash2);
	CHECK_FALSE(hash1<hash2);
	CHECK(hash2<hash1);
	CHECK_FALSE(hash2>hash1);

	CHECK(hash1>=hash2);
	CHECK_FALSE(hash1<=hash2);
	CHECK(hash1<=hash1);
	CHECK_FALSE(hash1<hash1);
	CHECK(hash2<=hash2);
	CHECK_FALSE(hash2<hash2);

	CHECK(alignment_of<Int128>::value==16);
	vector<Int128> hashes(4096);
	CHECK(vector<Int128>::allocator_type::alignment==16);

	{
		typedef std::chrono::duration<double, ratio<1>> secs_type;
		auto begin=chrono::high_resolution_clock::now();
		for(int m=0; m<10000; m++)
			Int128::FillFastRandom(hashes);
		auto end=chrono::high_resolution_clock::now();
		auto diff=chrono::duration_cast<secs_type>(end-begin);
		cout << "FillFastRandom 128-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(10000*hashes.size()*sizeof(Int128)) << " cycles/byte" << endl;
	}
	{
		typedef std::chrono::duration<double, ratio<1>> secs_type;
		auto begin=chrono::high_resolution_clock::now();
		for(int m=0; m<10000; m++)
			Int128::FillQualityRandom(hashes);
		auto end=chrono::high_resolution_clock::now();
		auto diff=chrono::duration_cast<secs_type>(end-begin);
		cout << "FillQualityRandom 128-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(10000*hashes.size()*sizeof(Int128)) << " cycles/byte" << endl;
	}
	vector<char> comparisons1(hashes.size());
	{
		typedef std::chrono::duration<double, ratio<1>> secs_type;
		auto begin=chrono::high_resolution_clock::now();
		for(int m=0; m<1000; m++)
			for(size_t n=0; n<hashes.size()-1; n++)
				comparisons1[n]=hashes[n]>hashes[n+1];
		auto end=chrono::high_resolution_clock::now();
		auto diff=chrono::duration_cast<secs_type>(end-begin);
		cout << "Comparisons 128-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000*(hashes.size()-1)) << " cycles/op" << endl;
	}
	vector<char> comparisons2(hashes.size());
	{
		typedef std::chrono::duration<double, ratio<1>> secs_type;
		auto begin=chrono::high_resolution_clock::now();
		for(int m=0; m<1000; m++)
			for(size_t n=0; n<hashes.size()-1; n++)
				comparisons2[n]=memcmp(&hashes[n], &hashes[n+1], sizeof(hashes[n]))>0;
		auto end=chrono::high_resolution_clock::now();
		auto diff=chrono::duration_cast<secs_type>(end-begin);
		cout << "Comparisons memcmp does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000*(hashes.size()-1)) << " cycles/op" << endl;
	}
	CHECK((comparisons1==comparisons2));
}

TEST_CASE("Int256/works", "Tests that Int256 works")
{
	char _hash1[32], _hash2[32];
	memset(_hash1, 0, sizeof(_hash1));
	memset(_hash2, 0, sizeof(_hash2));
	_hash1[5]=78;
	_hash2[31]=1;
	Int256 hash1(_hash1), hash2(_hash2), null;
	cout << "hash1=0x" << hash1.asHexString() << endl;
	cout << "hash2=0x" << hash2.asHexString() << endl;
	CHECK(hash1==hash1);
	CHECK(hash2==hash2);
	CHECK(null==null);
	CHECK(hash1!=null);
	CHECK(hash2!=null);
	CHECK(hash1!=hash2);

	CHECK(hash1>hash2);
	CHECK_FALSE(hash1<hash2);
	CHECK(hash2<hash1);
	CHECK_FALSE(hash2>hash1);

	CHECK(hash1>=hash2);
	CHECK_FALSE(hash1<=hash2);
	CHECK(hash1<=hash1);
	CHECK_FALSE(hash1<hash1);
	CHECK(hash2<=hash2);
	CHECK_FALSE(hash2<hash2);

	CHECK(alignment_of<Int256>::value==32);
	vector<Int256> hashes(4096);
	CHECK(vector<Int256>::allocator_type::alignment==32);

	{
		typedef std::chrono::duration<double, ratio<1>> secs_type;
		auto begin=chrono::high_resolution_clock::now();
		for(int m=0; m<10000; m++)
			Int256::FillFastRandom(hashes);
		auto end=chrono::high_resolution_clock::now();
		auto diff=chrono::duration_cast<secs_type>(end-begin);
		cout << "FillFastRandom 256-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(10000*hashes.size()*sizeof(Int256)) << " cycles/byte" << endl;
	}
	{
		typedef std::chrono::duration<double, ratio<1>> secs_type;
		auto begin=chrono::high_resolution_clock::now();
		for(int m=0; m<10000; m++)
			Int256::FillQualityRandom(hashes);
		auto end=chrono::high_resolution_clock::now();
		auto diff=chrono::duration_cast<secs_type>(end-begin);
		cout << "FillQualityRandom 256-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(10000*hashes.size()*sizeof(Int256)) << " cycles/byte" << endl;
	}
	vector<char> comparisons1(hashes.size());
	{
		typedef std::chrono::duration<double, ratio<1>> secs_type;
		auto begin=chrono::high_resolution_clock::now();
		for(int m=0; m<1000; m++)
			for(size_t n=0; n<hashes.size()-1; n++)
				comparisons1[n]=hashes[n]>hashes[n+1];
		auto end=chrono::high_resolution_clock::now();
		auto diff=chrono::duration_cast<secs_type>(end-begin);
		cout << "Comparisons 256-bit does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000*(hashes.size()-1)) << " cycles/op" << endl;
	}
	vector<char> comparisons2(hashes.size());
	{
		typedef std::chrono::duration<double, ratio<1>> secs_type;
		auto begin=chrono::high_resolution_clock::now();
		for(int m=0; m<1000; m++)
			for(size_t n=0; n<hashes.size()-1; n++)
				comparisons2[n]=memcmp(&hashes[n], &hashes[n+1], sizeof(hashes[n]))>0;
		auto end=chrono::high_resolution_clock::now();
		auto diff=chrono::duration_cast<secs_type>(end-begin);
		cout << "Comparisons memcmp does " << (CPU_CYCLES_PER_SEC*diff.count())/(1000*(hashes.size()-1)) << " cycles/op" << endl;
	}
	CHECK((comparisons1==comparisons2));
}
