#include "../test_functions.hpp"
#include "../../../../boost/afio/directory_monitor.hpp"
#include <iostream>
#include <atomic>
#include <chrono>
#include <fstream>
#include <cstdio>
typedef boost::afio::directory_entry directory_entry;

BOOST_AFIO_AUTO_TEST_CASE(directory_monitor_testing, "Tests that the change class is fundamentally OK", 60)
{
	/*// Test the Change struct
	auto x = std::make_shared<boost::afio::monitor::Watcher::Path::Change>(directory_entry(), directory_entry());
	auto j = std::make_shared<directory_entry>();
	auto y = std::make_shared<boost::afio::monitor::Watcher::Path::Change>(j, j);
	BOOST_CHECK(x != y);
	//BOOST_CHECK(*y == *x);
	BOOST_CHECK(x->oldfi != x->newfi);
	BOOST_CHECK(y->oldfi == y->newfi);
	BOOST_CHECK(x->oldfi != y->oldfi);
	BOOST_CHECK(*x != *y);
	x->reset_fis();
	y->reset_fis();
	BOOST_CHECK(*y != *x);
*/
// Test Watcher

	//boost::afio::monitor::Watcher w;
	//w.run();




// for a better test make the testdir then fill it with files, and then compare the names of the files

	//Test the Path struct
	//std::filesystem::path p("testdir");

	//boost::afio::monitor::Watcher::Path P_obj(nullptr, "testdir");


	/*auto path = std::make_shared<boost::afio::monitor::Watcher::Path>(nullptr, "testdir");
	std::cout << path->pathdir->size() << std::endl;
	for(auto it = path->pathdir->begin(); it != path->pathdir->end(); ++it)
	{
		BOOST_MESSAGE(it->name());
		//std::cout << it->name() << std::endl;
	}
	std::cout << path->path << std::endl;*/
// need to think of a test for resetChanges ...


//if(path)
	//path->callHandlers();



	//Test the Handler struct
	//std::cout <<"size of entry is: " << sizeof(directory_entry) << std::endl;

	static std::atomic<int> testint(0);
	boost::afio::dir_monitor::ChangeHandler handler = [&testint](boost::afio::dir_monitor::dir_event change,  directory_entry oldfi,  directory_entry newfi){
		std::cout << "We made it here !!!" << std::endl;
		testint++;
		std::cout << "testint is " << testint << " inside the lambda" << std::endl;
	};
	std::cout << "The handler address is " << &handler <<std::endl;
	/*auto h = std::make_shared<boost::afio::monitor::Watcher::Path::Handler>(path.get(), handler);
	BOOST_CHECK(true);

	//test invoke()
	std::list<boost::afio::monitor::Watcher::Path::Change> changes;
	boost::afio::monitor::Watcher::Path::Change b(j, j);
	changes.push_back(*x);
	changes.emplace_back(new directory_entry(), new directory_entry());
	changes.push_back(*y);

	h->invoke(changes);
	std::cout << "testint = " << testint << std::endl;
	BOOST_CHECK(testint == 3);
	testint = 0;
*/





//test how ptr_vector works for myself
	//boost::ptr_vector<boost::afio::monitor::Watcher::Path::Handler> handlers;
	//boost::afio::monitor::Watcher::Path::Handler* ptr_h = new boost::afio::monitor::Watcher::Path::Handler(path.get(), handler);
	//handlers.push_back(ptr_h);

	std::chrono::seconds dur(5);
	std::remove("testdir/test.txt");
	//test monitor
	boost::afio::monitor mm;
	std::cout << "Number of watchers is: " << mm.watchers.size() << std::endl;
	mm.add("testdir", handler);
	std::this_thread::sleep_for( dur);
	std::cout << "Number of watchers after adding is: " << mm.watchers.size() << std::endl;
	std::ofstream file("testdir/test.txt");
	file <<  "testint = " << testint << std::endl;
	file.close();
	std::this_thread::sleep_for( dur);
	

	std::cout << "testint = " << testint << std::endl;


	if(mm.remove("testdir", handler))
	{
		std::cout << "Number of watchers after removing is now: " << mm.watchers.size() << std::endl;;
	}

/*
	auto w = new boost::afio::monitor::Watcher(&mm);
	auto path_ptr = std::make_shared<boost::afio::monitor::Watcher::Path>(w, "testdir");
	mm.watchers.push_back(w);
	w->paths.emplace(path_ptr->path, *path_ptr);
	auto h_ptr = new boost::afio::monitor::Watcher::Path::Handler(path_ptr.get(), handler);
	path_ptr->handlers.push_back(h_ptr);
	std::cout << "Number of watchers after adding is: " << mm.watchers.size() << std::endl;;
	path_ptr->callHandlers();
	std::ofstream file("testdir/test.txt");
	file <<  "testint = " << testint << std::endl;
	file.close();
	path_ptr->callHandlers();
	std::cout << "testint = " << testint << std::endl;
	*/




}