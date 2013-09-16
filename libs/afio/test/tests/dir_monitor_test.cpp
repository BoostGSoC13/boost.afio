#include "../test_functions.hpp"





/********************************************************************************
*                                                                               *
*                       Test of filing system monitor                           *
*                                                                               *
*********************************************************************************
*        Copyright (C) 2003-2007 by Niall Douglas.   All Rights Reserved.       *
*       NOTE THAT I DO NOT PERMIT ANY OF MY CODE TO BE PROMOTED TO THE GPL      *
*********************************************************************************
* This code is free software; you can redistribute it and/or modify it under    *
* the terms of the GNU Library General Public License v2.1 as published by the  *
* Free Software Foundation EXCEPT that clause 3 does not apply ie; you may not  *
* "upgrade" this code to the GPL without my prior written permission.           *
* Please consult the file "License_Addendum2.txt" accompanying this file.       *
*                                                                               *
* This code is distributed in the hope that it will be useful,                  *
* but WITHOUT ANY WARRANTY; without even the implied warranty of                *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                          *
*********************************************************************************
* $Id:                                                                          *
********************************************************************************/

#include "../../../../boost/afio/directory_monitor.hpp"
#include <iostream>
typedef boost::afio::directory_entry directory_entry;

static void print( directory_entry &fi)
{
	auto str = sprintf("%s (size %d, modified %d) %d", fi.name().c_str(), fi.st_size(), fi.st_mtim(), fi.st_mode());
	std::cout <<(str) << std::endl;
}
static void printDir(const std::shared_ptr<std::vector<directory_entry>> list)
{
	for(auto it=list->begin(); it!=list->end(); ++it)
	{
		print(*it);
	}
}
static std::atomic<size_t> called(0);
static std::atomic<size_t> created(0);
static std::atomic<size_t> modified(0);
static std::atomic<size_t> deleted(0);
static std::atomic<size_t> renamed(0);
static std::atomic<size_t> security(0);
static void handler(boost::afio::dir_monitor::dir_event change,  directory_entry oldfi,  directory_entry newfi)
{
	called++;
	//std::cout <<("Item changed to:\n");
	//print(newfi);
#if 1
	if(change.flags.modified)
	{
		++modified;
		//std::cout <<("Code was modified\n\n");
	}
	if(change.flags.created)
	{
		++created;
		//std::cout <<("Code was created\n\n");
	}
	if(change.flags.deleted)
	{
		++deleted;
		//std::cout <<("Code was deleted\n\n");
	}
	if(change.flags.renamed)
	{
		++renamed;
		//auto str = sprintf("Code was renamed (from %s)\n\n", oldfi.name().c_str());
		//std::cout <<(str);
	}
/*	if(change.flags.attrib)
	{
		std::cout <<("Code was attributes changed\n\n");
	}*/
	if(change.flags.security)
	{
		++security;
		//std::cout <<("Code was security changed\n\n");
	}
#endif
}

BOOST_AFIO_AUTO_TEST_CASE(dir_monitor, "Tests that the directory monitoring implementation works", 90)
{
	using boost::afio::ratio;
	const size_t num = 100;
	std::chrono::seconds dur(10);
 	auto dispatcher=boost::afio::make_async_file_io_dispatcher();
 	boost::afio::monitor mon(dispatcher);

	namespace chrono = boost::afio::chrono;
    typedef chrono::duration<double, ratio<1>> secs_type;
    auto mkdir(dispatcher->dir(boost::afio::async_path_op_req("testdir", boost::afio::file_flags::Create)));
    std::vector<char, boost::afio::detail::aligned_allocator<char, 4096>> towrite(64, 'N');
    assert(!(((size_t) &towrite.front()) & 4095));

	boost::afio::dir_monitor::ChangeHandler h = handler;
    auto mkmon(dispatcher->call(mkdir, [&mon, &h](){mon.add("testdir", h);}));

std::this_thread::sleep_for( dur);
	auto begin=chrono::high_resolution_clock::now();
    std::vector<boost::afio::async_path_op_req> manyfilereqs;
    manyfilereqs.reserve(num);
    for(size_t n=0; n<num; n++)
            manyfilereqs.push_back(boost::afio::async_path_op_req(mkmon.second, "testdir/"+boost::to_string(n), boost::afio::file_flags::Create|boost::afio::file_flags::Write));
    auto manyopenfiles(dispatcher->file(manyfilereqs));


    // Write to each of those num files as they are opened
    std::vector<boost::afio::async_data_op_req<const char>> manyfilewrites;
    manyfilewrites.reserve(manyfilereqs.size());
    auto openit=manyopenfiles.begin();
    for(size_t n=0; n<manyfilereqs.size(); n++)
        manyfilewrites.push_back(boost::afio::async_data_op_req<const char>(*openit++, &towrite.front(), towrite.size(), 0));
    auto manywrittenfiles(dispatcher->write(manyfilewrites));


 auto manyclosedfiles(dispatcher->close(manywrittenfiles));

	auto it(manyclosedfiles.begin());
    BOOST_FOREACH(auto &i, manyfilereqs)
        i.precondition=*it++;
    std::vector<long long unsigned int> sizes(num, 1024);
    //sizes.reserve(num);
    /*BOOST_FOREACH(auto &i, sizes)
    	i = 1024;*/
    auto manytruncatedfiles(dispatcher->truncate(manyclosedfiles, sizes));


    // Close each of those num files once one byte has been written
   
   // sleep(10);

	//printDir(list);
std::this_thread::sleep_for( dur);
    // Delete each of those num files once they are closed
    auto del_it= manytruncatedfiles.begin();
    BOOST_FOREACH(auto &i, manyfilereqs)
            i.precondition=*del_it++;
    auto manydeletedfiles(dispatcher->rmfile(manyfilereqs));

    
    // Wait for all files to delete
    when_all(manydeletedfiles.begin(), manydeletedfiles.end()).wait();
    std::this_thread::sleep_for( dur);
    auto end=chrono::high_resolution_clock::now(); 
    auto rmdir(dispatcher->rmdir(boost::afio::async_path_op_req("testdir")));
    // Fetch any outstanding error
    //mon.remove("testdir", h);
    auto diff=chrono::duration_cast<secs_type>(end-begin);
    std::cout << "It took " << diff.count() << " secs to do all operations" << std::endl;
    rmdir.h->get();
	//BOOST_CHECK(called >= 3);
	
	//sleep(20);
	mon.remove("testdir", h);

	BOOST_CHECK(called.load() >= 2*num);
	BOOST_CHECK(created.load() == num);
	BOOST_CHECK(deleted.load() == num);
	BOOST_CHECK(security.load() == 0);
	BOOST_CHECK(modified.load() == 0);
	BOOST_CHECK(renamed.load() == 0);
	printf("called =%d, created = %d, deleted = %d, security=%d, modified=%d, renamed=%d\n", called.load(), created.load(), deleted.load(), security.load(), modified.load(), renamed.load());
}