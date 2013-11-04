#include "../test_functions.hpp"
#include "../../../../boost/afio/directory_monitor_v2.hpp"
#include <iostream>
#include <chrono>
#include <thread>
typedef boost::afio::directory_entry directory_entry;


void handler(boost::afio::dir_event change){ int x =1; int y = 2; x+y;}

BOOST_AFIO_AUTO_TEST_CASE(dir_monitor_constructor_destructor, "Tests that the directory_monitor constructor and destructors work", 180)
{

	auto dispatcher=boost::afio::make_async_file_io_dispatcher();
 	boost::afio::dir_monitor mon(dispatcher);
 	//BOOST_CHECK(mon.dispatcher == dispatcher);
 	//BOOST_CHECK(mon.eventcounter == 0);
	//BOOST_CHECK(mon.hash.empty());
	//BOOST_CHECK(mon.timer == nullptr);

 	// Add a directory to test destructor, use current directory
 	auto mkdir(dispatcher->dir(boost::afio::async_path_op_req("testdir", boost::afio::file_flags::Create)));
	boost::afio::dir_monitor::Handler h = handler;
 	auto mkmon(mon.add(mkdir, "testdir", &h));

 	//remove the monitor
 	//auto rmdir_mon(mon.remove(mkmon.second, "testdir", &h));
	auto rmdir(dispatcher->rmdir(boost::afio::async_path_op_req(mkmon.second, "testdir")));
 	rmdir.h.get();
}
