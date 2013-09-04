#include "../test_functions.hpp"
#include "../../../../boost/afio/directory_monitor.hpp"
#include <iostream>
typedef boost::afio::directory_entry directory_entry;

BOOST_AFIO_AUTO_TEST_CASE(Change, "Tests that the change class is fundamentally OK", 60)
{
	// Test the Change struct
	auto x = std::make_shared<boost::afio::monitor::Watcher::Path::Change>(new directory_entry(), new directory_entry());
	auto j = directory_entry();
	auto y = std::make_shared<boost::afio::monitor::Watcher::Path::Change>(&j, &j);
	BOOST_CHECK(x != y);
	//BOOST_CHECK(*y == *x);
	BOOST_CHECK(x->oldfi != x->newfi);
	BOOST_CHECK(y->oldfi == y->newfi);
	BOOST_CHECK(x->oldfi != y->oldfi);
	BOOST_CHECK(*x != *y);
	x->make_fis();
	y->reset_fis();
	BOOST_CHECK(*y != *x);



	//Test the Handler struct
	boost::afio::dir_monitor::ChangeHandler handler = [](boost::afio::dir_monitor::dir_event change,  directory_entry oldfi,  directory_entry newfi){
		return;
	};
	auto h = std::make_shared<boost::afio::monitor::Watcher::Path::Handler>(nullptr, handler);
	BOOST_CHECK(true);

	//test invoke later :(



// for a better test make the testdir then fill it with files, and then compare the names of the files

	//Test the Path struct
	auto path = std::make_shared<boost::afio::monitor::Watcher::Path>(nullptr, "testdir");
	for(auto it = path->pathdir->begin(); it != path->pathdir->begin(); ++it)
	{
		BOOST_MESSAGE(it->name().string());
		std::cout << it->name() << std::endl;
	}
	std::cout << path->path << std::endl;
// need to think of a test for resetChanges ...

	path->callHandlers();



	
}