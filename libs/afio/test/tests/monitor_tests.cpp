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
	boost::afio::dir_monitor::Handler handler = [&testint](boost::afio::dir_event change){
		testint++;
		std::cout << "------->Event #: " << change.eventNo << " File: " << change.path << std::endl;
		if(change.flags.modified)
		{
			//++modified;
			std::cout <<("- The file was modified\n");
		}
		if(change.flags.created)
		{
			//++created;
			std::cout <<("- The file was created\n");
		}
		if(change.flags.deleted)
		{
			//++deleted;
			std::cout <<("- The file was deleted\n");
		}
		if(change.flags.renamed)
		{
			//++renamed;
			//auto str = sprintf("The file was renamed (from %s)\n\n", oldfi.name().c_str());
			std::cout <<"- The file was renamed\n";
		}
		if(change.flags.attrib)
		{
			std::cout <<("The file's attributes changed\n");
		}
		if(change.flags.security)
		{
			//++security;
			std::cout <<("- The file's security changed\n");
		}
		std::cout << std::endl;

	};
try{

	std::cout << "The handler address is: " << &handler << std::endl;
	auto dispatcher = boost::afio::make_async_file_io_dispatcher();
	auto mkdir(dispatcher->dir(boost::afio::async_path_op_req("testdir", boost::afio::file_flags::Create)));
	mkdir.h->get();

	std::chrono::milliseconds dur(100);
	//in case these files exist remove them
	std::remove("testdir/test.txt");
	std::remove("testdir/test2.txt");
	
	boost::afio::dir_monitor mm(dispatcher);
	//BOOST_CHECK(mm.hash.size() == 0);
	auto add_test(mm.add(mkdir, "testdir", &handler));
	//auto add_tools(mm.add(add_test.second, "tools", handler));
	size_t sum = 0;	
	//BOOST_CHECK(mm.hash.size() == 2);
	if(add_test.first.get())
		std::cout << "adding is complete<-----------------------\n";
	else 
		std::cout << "Something happend when adding diretories...!\n";

	std::ofstream file("testdir/test.txt");
	file <<  "testint = " << testint << std::endl;
	file.close();
	std::this_thread::sleep_for( dur);
	std::cout << "testint = " << testint << std::endl;
	//BOOST_CHECK(testint == 1);

	std::this_thread::sleep_for( dur);

	file.open("testdir/test2.txt");
	file <<  "testint = " << testint << std::endl;
	file.close();
	std::this_thread::sleep_for( dur);
	std::cout << "testint = " << testint << std::endl;
	//BOOST_CHECK(testint == 2);
	
	std::remove("testdir/test.txt");
	std::this_thread::sleep_for(dur);
	std::remove("testdir/test2.txt");
	std::this_thread::sleep_for(dur);
	
	std::cout << "testint = " << testint << std::endl;
	//BOOST_CHECK(testint == 4);

	auto removed(mm.remove(add_test.second, "testdir", &handler));
	std::cout << "Scheduled removal of testdir...\n";
	if(removed.first.get())
	{
		std::cout << "Removed a directory that was monitored" << std::endl;
	}

		std::cout << "exiting\n";
	}catch(std::exception &e){ std::cout << "Error in the test!!!!<------------\n"<< e.what()<< std::endl;}

}