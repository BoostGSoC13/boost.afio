#include "../test_functions.hpp"
#include "../../../../boost/afio/directory_monitor_v2.hpp"
#include <iostream>
#include <atomic>
#include <chrono>
#include <fstream>
#include <cstdio>
typedef boost::afio::directory_entry directory_entry;

BOOST_AFIO_AUTO_TEST_CASE(directory_monitor_testing, "Tests that directory monitoring basically works", 60)
{

	static std::atomic<int> testint(0);
	boost::afio::dir_monitor::Handler handler = [&testint](boost::afio::dir_monitor::dir_event change,  directory_entry oldfi,  directory_entry newfi){
		testint++;
	};

	auto dispatcher = boost::afio::make_async_file_io_dispatcher();
	auto mkdir(dispatcher->dir(boost::afio::async_path_op_req("testdir", boost::afio::file_flags::Create)));
	mkdir.h->get();

	std::chrono::seconds dur(1);
	//in case these files exist remove them
	std::remove("testdir/test.txt");
	std::remove("testdir/test2.txt");
	
	boost::afio::dir_monitor mm(dispatcher);
	BOOST_CHECK(mm.hash.size() == 0);
	auto add_test(mm.add(mkdir, "testdir", handler));
	auto add_tools(mm.add(add_test.second, "tools", handler));
	size_t sum = 0;	
	BOOST_CHECK(mm.hash.size() == 1);
	
	
	
	std::this_thread::sleep_for( dur);
	std::ofstream file("testdir/test.txt");
	file <<  "testint = " << testint << std::endl;
	file.close();
	std::this_thread::sleep_for( dur);
	std::cout << "testint = " << testint << std::endl;
	BOOST_CHECK(testint == 1);

	std::this_thread::sleep_for( dur);

	file.open("testdir/test2.txt");
	file <<  "testint = " << testint << std::endl;
	file.close();
	std::this_thread::sleep_for( dur);
	std::cout << "testint = " << testint << std::endl;
	BOOST_CHECK(testint == 2);
	
	std::remove("testdir/test.txt");
	std::this_thread::sleep_for(dur);
	std::remove("testdir/test2.txt");
	std::this_thread::sleep_for(dur);
	
	std::cout << "testint = " << testint << std::endl;
	BOOST_CHECK(testint == 4);
	if(mm.remove(add_tools.second, "testdir", handler))
	{
		std::cout << "Number of watchers after removing is now: " << mm.watchers.size() << std::endl;;
		mm.hash.size()
	}
}