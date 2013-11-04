#include "../test_functions.hpp"
#include "../../../../boost/afio/directory_monitor_v2.hpp"
#include <iostream>
#include <chrono>
#include <thread>
typedef boost::afio::directory_entry directory_entry;


void handler(boost::afio::dir_event change){ int x =1; int y = 2; x+y;}

BOOST_AFIO_AUTO_TEST_CASE(dir_monitor_add, "Tests that the directory_monitor add()  works", 20)
{

	auto dispatcher=boost::afio::make_async_file_io_dispatcher();
 	auto mon = boost::afio::make_monitor(dispatcher);
 	BOOST_CHECK(mon->get_hash().empty());

 	// Add a directory to test destructor, use current directory
 	auto mkdir(dispatcher->dir(boost::afio::async_path_op_req("testdir", boost::afio::file_flags::Create)));
	boost::afio::dir_monitor::Handler h = handler;
	
	mkdir.h.get();
 	//auto mkmon(mon.add(mkdir, "testdir", &h));
 	//std::cout << "OK here before mkmon.first.get()" << std::endl;
 	//mkmon.first.wait();
 	if(!mon->add_p("testdir", &h))
 		std::cout << "Super huge error here! Couldn't add_path()!!!" << std::endl;
 	else
 		std::cout << "Path successfully added!!!" << std::endl;
 	//auto mkmon2(mon.add(mkmon.second, "testdir/..", &h));
 	//boost::afio::dir_monitor* ptr= &mon;
 	//sleep(10);
 	std::cout << "Monitor hash size = " << mon->get_hash().size() <<std::endl;

 	if(mon->remove_p("testdir", &h))
 		std::cout << "The remove operation completed successfully!!! :)" << std::endl;
sleep(2);
 	/*auto check(dispatcher->call(mkmon2.second, [ptr](){
 		std::cout << "Monitor hash size = " << ptr->get_hash().size() <<std::endl;
 		BOOST_CHECK(ptr->get_hash().size() == 2);

 	}));*/

 	//remove the monitor
 	//auto rmdir_mon(mon.remove(mkmon.second, "testdir", &h));
	//auto rmdir(dispatcher->rmdir(boost::afio::async_path_op_req(check.second, "testdir")));
 	//rmdir.h.get();
}
