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
	auto str = sprintf("%s (size %d, modified %d) %d", fi.leafname.c_str(), fi.st_size(), fi.st_mtim().tv_sec, fi.st_mode());
	std::cout <<(str) << std::endl;
}
static void printDir(const std::shared_ptr<std::vector<directory_entry>> list)
{
	for(auto it=list->begin(); it!=list->end(); ++it)
	{
		print(*it);
	}
}
static int called=0;
static void handler(boost::afio::dir_monitor::dir_event change,  directory_entry oldfi,  directory_entry newfi)
{
	called++;
	std::cout <<("Item changed to:\n");
	print(newfi);
	if(change.flags.modified)
	{
		std::cout <<("Code was modified\n\n");
	}
	if(change.flags.created)
	{
		std::cout <<("Code was created\n\n");
	}
	if(change.flags.deleted)
	{
		std::cout <<("Code was deleted\n\n");
	}
	if(change.flags.renamed)
	{
		auto str = sprintf("Code was renamed (from %s)\n\n", oldfi.leafname.c_str());
		std::cout <<(str);
	}
/*	if(change.flags.attrib)
	{
		std::cout <<("Code was attributes changed\n\n");
	}*/
	if(change.flags.security)
	{
		std::cout <<("Code was security changed\n\n");
	}
}

BOOST_AFIO_AUTO_TEST_CASE(dir_monitor, "Tests that the directory monitoring implementation works", 60)
{
	const size_t num = 1000;
 	auto dispatcher=boost::afio::make_async_file_io_dispatcher();

	//namespace chrono = boost::afio::chrono;
    //typedef chrono::duration<double, ratio<1>> secs_type;
    auto mkdir(dispatcher->dir(boost::afio::async_path_op_req("testdir", boost::afio::file_flags::Create)));
    std::vector<char, boost::afio::detail::aligned_allocator<char, 4096>> towrite(64, 'N');
    assert(!(((size_t) &towrite.front()) & 4095));

	//auto begin=chrono::high_resolution_clock::now();
	

	//auto after1 = chrono::high_resolution_clock::now();
	//auto res = after1-before;
	//FXuint after2=FXProcess::getMsCount();
	//BOOST_TEST_MESSAGE("  (took %dms for list, %dms for file info fetch)\n", after1-before, after2-after1);
	//
	//BOOST_TEST_MESSAGE("\nMonitoring for changes ...\n");

	boost::afio::dir_monitor::ChangeHandler h = handler;
	boost::afio::dir_monitor::add("testdir", h);
	//QThread::sleep(num);
	//BOOST_TEST_MESSAGE("Making changes ...\n");



	//auto begin=chrono::high_resolution_clock::now();
    std::vector<boost::afio::async_path_op_req> manyfilereqs;
    manyfilereqs.reserve(num);
    for(size_t n=0; n<num; n++)
            manyfilereqs.push_back(boost::afio::async_path_op_req(mkdir, "testdir/"+boost::to_string(n), boost::afio::file_flags::Create|boost::afio::file_flags::Write));
    auto manyopenfiles(dispatcher->file(manyfilereqs));

    // Write to each of those num files as they are opened
    std::vector<boost::afio::async_data_op_req<const char>> manyfilewrites;
    manyfilewrites.reserve(manyfilereqs.size());
    auto openit=manyopenfiles.begin();
    for(size_t n=0; n<manyfilereqs.size(); n++)
        manyfilewrites.push_back(boost::afio::async_data_op_req<const char>(*openit++, &towrite.front(), towrite.size(), 0));
    auto manywrittenfiles(dispatcher->write(manyfilewrites));

	auto it(manywrittenfiles.begin());
    BOOST_FOREACH(auto &i, manyfilereqs)
        i.precondition=*it++;
    std::vector<long long unsigned int> sizes(num);
    BOOST_FOREACH(auto &i, sizes)
    	i = 1024;
    auto manytruncatedfiles(dispatcher->truncate(manywrittenfiles, sizes));

    // Close each of those num files once one byte has been written
    auto manyclosedfiles(dispatcher->close(manywrittenfiles));

	std::shared_ptr<std::vector<directory_entry>> list;
	void *addr=boost::afio::begin_enumerate_directory("testdir");
	std::unique_ptr<std::vector<directory_entry>> chunk;
	while((chunk=boost::afio::enumerate_directory(addr, 10000)))
		if(!list)
			list=std::move(chunk);
		else
			list->insert(list->end(), std::make_move_iterator(chunk->begin()), std::make_move_iterator(chunk->end()));
	boost::afio::end_enumerate_directory(addr);

	printDir(list);

    // Delete each of those num files once they are closed
    it= manyclosedfiles.begin();
    BOOST_FOREACH(auto &i, manyfilereqs)
            i.precondition=*it++;
    auto manydeletedfiles(dispatcher->rmfile(manyfilereqs));

    
    // Wait for all files to delete
    when_all(manydeletedfiles.begin(), manydeletedfiles.end()).wait();
   
    auto rmdir(dispatcher->rmdir(boost::afio::async_path_op_req("testdir")));
    // Fetch any outstanding error
    rmdir.h->get();
	//BOOST_CHECK(called >= 3);
}