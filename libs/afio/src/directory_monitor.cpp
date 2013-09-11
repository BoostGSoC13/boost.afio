/********************************************************************************
*                                                                               *
*                            Filing system monitor                              *
*                                                                               *
*********************************************************************************
*        Copyright (C) 2003-2008 by Niall Douglas.   All Rights Reserved.       *
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
// set up for various OS

#include "../../../boost/afio/directory_monitor.hpp"
namespace boost { 
	namespace afio {



monitor::monitor() : running(true)
{
	std::cout << "monitor constructed" << std::endl;
	threadpool = process_threadpool();
	finished = threadpool->enqueue([this](){
		std::cout << "this lambda was called" << std::endl;
		while(this->is_running())
		{
			std::cout << "running was true here" << std::endl;
			this->process_watchers(); 
		}
		std::cout << "this lambda is ending..." << std::endl;
	});
#ifdef USE_INOTIFY
	BOOST_AFIO_ERRHOS(inotifyh=inotify_init());
#endif
#ifdef USE_KQUEUES
	BOOST_AFIO_ERRHOS(kqueueh=kqueue());
#endif

}

monitor::~monitor()
{ 
	std::cout << "Monitor is being destroyed" << std::endl;
	running = false;
	std::cout << "running is false" << std::endl;

	//there must be a better way to do this...
	BOOST_FOREACH(auto &i, watchers)
	{
		BOOST_FOREACH(auto &j, i.paths)
		{
			BOOST_FOREACH(auto &k, j.second.handlers)
				remove(j.second.path, *k.handler);
		}
	}
	std::cout << "Watchers have been stoped (i think)..." << std::endl;
	finished.get();
	std::cout << "future has completed" << std::endl;
	/*if(my_thread && my_thread->joinable())
		my_thread->join();*/
	watchers.clear();
	std::cout << "stopping the threadpool ..." <<std::endl;
	threadpool.reset();
	std::cout << "Threadpool stopped." <<std::endl;
	#ifdef USE_INOTIFY
		if(inotifyh)
		{
			std::cout << "trying to close inotify ..." << std::endl;
			BOOST_AFIO_ERRHOS(::close(inotifyh));
			inotifyh=0;
			std::cout << "inotify closed!" << std::endl;
		}
	#endif
	#ifdef USE_KQUEUES
		if(kqueueh)
		{
			BOOST_AFIO_ERRHOS(::close(kqueueh));
			kqueueh=0;
		}
	#endif
}

monitor::Watcher::Watcher(monitor* _parent) : parent(_parent), can_run(false)
#ifdef USE_WINAPI
	, latch(0)
#endif
{
	paths.clear();
#ifdef USE_WINAPI
	BOOST_AFIO_ERRHWIN(latch=CreateEvent(NULL, FALSE, FALSE, NULL));
#endif
}

monitor::Watcher::~Watcher()
{
	// attempt to get all outstanding futures
	/*while(!future_queue.empty())
	{
		std::future<void> *temp;
		if(future_queue.pop(temp))
			temp->get();
	}*/
#ifdef USE_WINAPI
	if(latch)
	{
		BOOST_AFIO_ERRHWIN(CloseHandle(latch));
		latch=0;
	}
#endif
	std::cout << "watcher has been destroyed" << std::endl;
}

#ifdef USE_WINAPI
void monitor::Watcher::run()
{
	HANDLE hlist[MAXIMUM_WAIT_OBJECTS];
	//hlist[0]=QThread::int_cancelWaiterHandle();
	hlist[0]=latch;
	for(;;)
	{
		//QMtxHold h(f);
		//(*this).lock();
		BOOST_AFIO_LOCK_GUARD<monitor> h((*this), boost::adopt_lock);
		Path *p;
		int idx=1;
		for(auto it = paths.begin(); it != paths.end(); ++it)
		{
			hlist[idx++]=it->h;
		}
		(*this).unlock();
		
		DWORD ret=WaitForMultipleObjects(idx, hlist, FALSE, INFINITE);
		if(ret<WAIT_OBJECT_0 || ret>=WAIT_OBJECT_0+idx) { BOOST_AFIO_ERRHWIN(ret); }
		checkForTerminate();
		if(WAIT_OBJECT_0==ret) continue;
		ret-=WAIT_OBJECT_0;
		FindNextChangeNotification(hlist[ret]);
		ret-=1;
		(*this).lock();
		for(auto it = paths.begin(); it != paths.end(); ++it)
		{
			p = it;
			if(!ret) break;
			--ret;
		}
		if(p)
			p->callHandlers();
	}
}
#endif



#ifdef USE_INOTIFY
void monitor::Watcher::run()
{
	BOOST_AFIO_LOCK_GUARD<monitor> lg(*this->parent);
	int ret;
	char buffer[4096];
	std::cout << "run() was called" << std::endl;
	//for(;;)
	//{
		try
		{

			if(0 <= (ret=read(parent->inotifyh, buffer, sizeof(buffer))))
			{
				std::cout << "run() made it to the if statement" << std::endl;
				BOOST_AFIO_ERRHOS(ret);
				for(inotify_event *ev=(inotify_event *) buffer; ((char *)ev)-buffer<ret; ev+=ev->len+sizeof(inotify_event))
				{
#if 1
//#ifdef DEBUG
					printf("dir_monitor: inotify reports wd=%d, mask=%u, cookie=%u, name=%s\n", ev->wd, ev->mask, ev->cookie, ev->name);
#endif
					//BOOST_AFIO_LOCK_GUARD<monitor> h(*parent);
					auto it = pathByHandle.find(ev->wd); 
					Path* p = (it == pathByHandle.end()) ? NULL : it->second;
					//assert(p);
					if(p != NULL)
					{
						std::cout << "making the call to callHandlers()" <<std::endl;
						try
						{
							p->callHandlers();
						}
						catch(...)
						{
							std::cout << "An error occurred durring callHandlers() ..." <<std::endl;	
							//std::abort();
							throw;
						}
						std::cout << "done with call from callHandlers()" <<std::endl;
					}
					else
						std::cout << "Some thing is wrong with p in run()..." <<std::endl;	

				}
			}
		}
		catch(std::exception &e)
		{
			std::cout << "Error from boost::afio::monitor::Watcher::run(): " << e.what() << std::endl;
			return;
		}
		
	//}
}
#endif
#ifdef USE_KQUEUES
void monitor::Watcher::run()
{
	int ret;
	struct kevent kevs[16];
#if defined(__APPLE__) && defined(_APPLE_C_SOURCE)
	// Register magic thread termination waiter handle with kqueue
	int cancelWaiterHandle=(int)(FXuval) QThread::int_cancelWaiterHandle();
	EV_SET(&cancelWaiter, cancelWaiterHandle, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);
	BOOST_AFIO_ERRHOS(kevent(monitor->kqueueh, &cancelWaiter, 1, NULL, 0, NULL));
#endif
	for(;;)
	{
#if !(defined(__APPLE__) && defined(_APPLE_C_SOURCE))
		QThread::current()->checkForTerminate();
#endif
		FXERRH_TRY
		{	// Have it kick out once a second as it's not a cancellation point on FreeBSD
			struct timespec timeout={1, 0};
			if((ret=kevent(monitor->kqueueh, NULL, 0, kevs, sizeof(kevs)/sizeof(struct kevent),
#if defined(__APPLE__) && defined(_APPLE_C_SOURCE)
				NULL)))
#else
				&timeout)))
#endif
			{
				BOOST_AFIO_ERRHOS(ret);
				for(int n=0; n<ret; n++)
				{
					struct kevent *kev=&kevs[n];
#if defined(__APPLE__) && defined(_APPLE_C_SOURCE)
					if(cancelWaiterHandle==kev->ident)
						QThread::current()->checkForTerminate();
#endif
					BOOST_AFIO_LOCK_GUARD<monitor> h((*this));
					Path *p=pathByHandle.[(kev->ident)];
					assert(p);
					if(p)
						p->callHandlers();
				}
			}
		}
		FXERRH_CATCH(FXException &e)
		{
			fxwarning("dir_monitor Error: %s\n", e.report().text());
			return;
		}
		FXERRH_ENDTRY
	}
}
#endif

void *monitor::Watcher::cleanup()
{
#if defined(__APPLE__) && defined(_APPLE_C_SOURCE)
	// Deregister magic thread termination waiter handle with kqueue
	cancelWaiter.flags=EV_DELETE;
	BOOST_AFIO_ERRHOS(kevent(monitor->kqueueh, &cancelWaiter, 1, NULL, 0, NULL));
#endif
	return 0;
}

monitor::Watcher::Path::~Path()
{
#ifdef USE_WINAPI
	if(h)
	{
		parent->pathByHandle.erase(h);
		BOOST_AFIO_ERRHWIN(FindCloseChangeNotification(h));
		h=0;
	}
#endif
#ifdef USE_INOTIFY
	if(h)
	{
		int temp; 
		parent->pathByHandle.erase(h);
		//std::cout << parent->parent->inotifyh << " " << h << std::endl;
		BOOST_AFIO_ERRHOS(temp = inotify_rm_watch(parent->parent->inotifyh, h));
		//temp = inotify_rm_watch(parent->parent->inotifyh, h);
		//std::cout << "temp is: " << temp << std::endl;
		h=0;
		//std::cout << "finished destructing this path" << std::endl;
	}
#endif
#ifdef USE_KQUEUES
	h.flags=EV_DELETE;
	kevent(monitor->kqueueh, &h, 1, NULL, 0, NULL);
	parent->pathByHandle.erase(h.ident);
	if(h.ident)
	{
		BOOST_AFIO_ERRHOS(::close(h.ident));
		h.ident=0;
	}
#endif

}

monitor::Watcher::Path::Handler::~Handler()
{
	//I think that the destruction from std_thread_pool takes care of all of this
/*
	BOOST_AFIO_LOCK_GUARD<monitor> h((*this));
	while(!callvs.empty())
	{
		//QThreadPool::CancelledState state;
		//while(QThreadPool::WasRunning==(state=FXProcess::threadPool().cancel(callvs.front())));
		callvs.pop_front();// this should be insufficient I think. need to fix the above lines to work without qt of fx
	}*/


}

void monitor::Watcher::Path::Handler::invoke(const std::list<Change> &changes/*, future_handle callv*/)
{
	std::cout << "invoke() was called" <<std::endl;
	//fxmessage("dir_monitor dispatch %p\n", callv);
	for(auto it=changes.begin(); it!=changes.end(); ++it)
	{
		//const Change &ch=*it;
		//const directory_entry &oldfi = it->oldfi ? *it->oldfi : directory_entry();
		//const directory_entry &newfi = it->newfi ? *it->newfi : directory_entry();
#ifdef DEBUG
		{
			/*FXString file(oldfi.filePath()), chs;
			if(ch.change.modified) chs.append("modified ");
			if(ch.change.created)  { chs.append("created "); file=newfi.filePath(); }
			if(ch.change.deleted)  chs.append("deleted ");
			if(ch.change.renamed)  chs.append("renamed (to "+newfi.filePath()+") ");
			if(ch.change.attrib)   chs.append("attrib ");
			if(ch.change.security) chs.append("security ");
			message("dir_monitor: File %s had changes: %s at %s\n", file.text(), chs.text(), (ch.newfi ? *ch.newfi : *ch.oldfi).lastModified().asString().text());
		*/}
#endif
		{
			//BOOST_AFIO_LOCK_GUARD<monitor> h((*this));
			//callv.get();
			//callvs.remove(callv); //I think this will be OK instead of removeReffrom QptrList
			//h.unlock();
		}
		std::cout << "invoke() called the handler" <<std::endl;
		std::cout << "The handler address is " << handler <<std::endl;
		(*handler)(it->change, it->oldfi, it->newfi);
	}
	std::cout << "invoke() has completed" <<std::endl;
}


/*static const directory_entry& findFIByName(const std::unique_ptr<std::vector<directory_entry>>  list, const std::filesystem::path &name)
{
	for(auto it=list->begin(); it!=list->end(); ++it)
	{
		// Need a case sensitive compare
		if((*it).name()==name) return *it;
	}
	return 0;
}*/
void monitor::Watcher::Path::callHandlers()
{	


	//std::cout << pathdir->size();
	//std::cout << std::filesystem::absolute(this->path) << std::endl;
	BOOST_AFIO_LOCK_GUARD<monitor> lk(*this->parent->parent);
	std::cout << "callHandlers was called" <<std::endl;
	void *addr=begin_enumerate_directory(path);
	std::unique_ptr<std::vector<directory_entry>> newpathdir, chunk;

	while((chunk=enumerate_directory(addr, NUMBER_OF_FILES)))
		if(!newpathdir)
			newpathdir=std::move(chunk);
		else
			newpathdir->insert(newpathdir->end(), std::make_move_iterator(chunk->begin()), std::make_move_iterator(chunk->end()));
	end_enumerate_directory(addr);

	std::list<Change> changes;
	static unsigned long eventcounter=0;
#if 1
	for(auto it = newpathdir->begin(); it != newpathdir->end(); ++it)
	{
		try
		{
			// try to find the directory_entry
			// if it exists, then determine if anything has changed
			auto temp = entry_dict.at(*it);

			bool changed = false, renamed = false, modified = false, security = false;
			// determine if any changes
			if(temp.name() != it->name())
			{
				changed = true;
				renamed = true;
			}
			if(temp.st_mtim()!= it->st_mtim()
					|| temp.st_size()	!= it->st_size() 
					|| temp.st_allocated() != it->st_allocated())
			{
				modified = true;
				changed = true;
			}
			if(temp.st_mode() != it->st_mode())
			{
				changed = true;
				security = true;
			}
			if(changed)
			{
				Change ch(temp, (*it));
				ch.change.eventNo=++eventcounter;
				ch.change.setRenamed(renamed);
				ch.change.setModified(modified);
				ch.change.setSecurity(security);
				changes.push_back(ch);
			}

			// we found this entry, so remove it to later 
			// determine what has been deleted
			entry_dict.erase(temp); 
		}
		catch(std::out_of_range &e)
		{
			std::cout << "we have a new file\n";
			//We've never seen this before
			Change ch(directory_entry(), (*it));
			ch.change.eventNo=++eventcounter;
			ch.change.setCreated();
			changes.push_back(ch);
		}
		catch(...)
		{
			std::cout << "another type of error occured that is ruining this\n";
		}
	}
#endif
	// anything left in entry_dict has been deleted
	for(auto it = entry_dict.begin(); it != entry_dict.end(); ++it)
	{
		
		Change ch((it->second), directory_entry());
		ch.change.eventNo=++eventcounter;
		ch.change.setDeleted();
		changes.push_back(std::move(ch));
	}
	entry_dict.clear();
	
	//this seems wrong...
	auto resetchanges = boost::afio::detail::Undoer([this, &changes](){ this->resetChanges(&changes); } );

	// Dispatch
	Watcher::Path::Handler *handler;
	for(auto it = handlers.begin(); it != handlers.end(); ++it)
	{	
		handler = &(*it);	
		std::cout << "handlers size is: " << handlers.size() <<std::endl;
		//parent->future_queue.bounded_push(parent->parent->threadpool->enqueue([handler, &changes](){ handler->invoke(changes); }));
		parent->parent->threadpool->enqueue([handler, &changes](){ handler->invoke(changes); });
	}

	// update pathdir and entry_dict
	pathdir=std::move(newpathdir);
	for(auto it = pathdir->begin(); it != pathdir->end(); ++it)
	{
		entry_dict.insert(std::make_pair(*it, *it));
	}
	std::cout <<"Exiting callHandlers()" << std::endl;

}

void monitor::add(const std::filesystem::path &path, dir_monitor::ChangeHandler& handler)
{

	std::cout << "adding a directory to monitor " << path.string() << std::endl;
	//BOOST_AFIO_LOCK_GUARD<monitor> lh((*this));
	Watcher *w = nullptr;
	for(auto it = watchers.begin(); it != watchers.end() && !w; ++it)
	{
#ifdef USE_WINAPI
		if(it == watchers.end() || it->paths.size() >= MAXIMUM_WAIT_OBJECTS-2) continue;
#endif
		w = &(*it);
		//break;
	}
	if(!w)
	{
		auto unnew = boost::afio::detail::Undoer([&w]{delete w; w = nullptr;});
		w=new Watcher(this);
		//w->start();
		w->can_run = true;
		watchers.push_back(w);
		unnew.dismiss();
	}
	Watcher::Path *p;
	auto temp = w->paths.find(path);
	if (temp != w->paths.end())
		p = &(temp->second);
	else
	{
		auto unnew = boost::afio::detail::Undoer([&p]{delete p; p = nullptr;});
		w->paths.insert(std::make_pair(path, Watcher::Path(w, path)));
		
		// fix this to be safe and compliant!!!!!!!!!!!!
		temp = w->paths.find(path);
		if (temp != w->paths.end())
			p = &(temp->second);
		else
			std::cout << "horrible error here !!!!!!!!!!!!!" << std::endl;
		

#ifdef USE_WINAPI
		HANDLE h;
		BOOST_AFIO_ERRHWIN(INVALID_HANDLE_VALUE!=(h=FindFirstChangeNotification(FXUnicodify<>(path, true).buffer(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME
			|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_ATTRIBUTES|FILE_NOTIFY_CHANGE_SIZE
			/*|FILE_NOTIFY_CHANGE_LAST_WRITE*/|FILE_NOTIFY_CHANGE_SECURITY)));
		p->h=h;
		BOOST_AFIO_ERRHWIN(SetEvent(w->latch));
		w->pathByHandle.insert(h, p);
#endif
#ifdef USE_INOTIFY
		int h = 0;
		auto full_path = std::filesystem::absolute(path);
		//std::cout <<"C string path: " << full_path.c_str() << std::endl;
		BOOST_AFIO_ERRHOS(h=inotify_add_watch(this->inotifyh, full_path.c_str(), IN_ALL_EVENTS&~(IN_ACCESS|IN_CLOSE_NOWRITE|IN_OPEN)));
		//std::cout << "inotifyh value is: " << this->inotifyh <<  " h value is: " << h << std::endl;
		p->h=h;
		w->pathByHandle.insert(std::make_pair(h, p));
#endif
#ifdef USE_KQUEUES
		int h;
		BOOST_AFIO_ERRHOS(h=::open(path.c_str(),
#ifdef __APPLE__
			O_RDONLY|O_EVTONLY));		// Stop unmounts being prevented by open handle
#else
			O_RDONLY));
#endif
		BOOST_AFIO_ERRHOS(::fcntl(h, F_SETFD, ::fcntl(h, F_GETFD, 0)|FD_CLOEXEC));
		EV_SET(&p->h, h, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE, 0, 0);
		BOOST_AFIO_ERRHOS(kevent(monitor->kqueueh, &p->h, 1, NULL, 0, NULL));
		w->pathByHandle.insert(h, p);
#endif
		//std::cout << "made it here 1" << std::endl;
		//w->paths.insert(std::make_pair(path, *p));
		//p = w->paths[path];
		//std::cout << "made it here 2" << std::endl;
		unnew.dismiss();

	}
	
	Watcher::Path::Handler *h;
	auto unh = boost::afio::detail::Undoer([&h]{delete h; h = nullptr;});
	h=new Watcher::Path::Handler(p, handler);
	p->handlers.push_back(h);
	unh.dismiss();
	std::cout << "Successfuly added " << path.string() << std::endl;
}

bool monitor::remove(const std::filesystem::path &path, dir_monitor::ChangeHandler &handler)
{
	std::cout << "Starting to remove..." <<std::endl;
	//BOOST_AFIO_LOCK_GUARD<monitor> hl(*this);
	//std::cout << "Lock guard aquired" <<std::endl;
	Watcher *w;
	for(auto it = watchers.begin(); it != watchers.end(); ++it)
	{
		std::cout << "Going through watchers ..." <<std::endl;
		w = &(*it);
		Watcher::Path *p;
		auto iter = w->paths.find(path);
		if(iter != w->paths.end())
			p = &(iter->second);
		else
			continue;
		Watcher::Path::Handler *h;
		for(auto it2 = p->handlers.begin(); it2 != p->handlers.end(); ++it2)
		{
			std::cout << "looking for a handler to remove ..." <<std::endl;
			h = &(*it2);
			if(h->handler == &handler)
			{
				std::cout << "Found the handler to remove!!!" <<std::endl;
				p->handlers.erase(it2);
				h = NULL;
				if(p->handlers.empty())
				{
#ifdef USE_WINAPI
					BOOST_AFIO_ERRHWIN(SetEvent(w->latch));
#endif
					std::cout << "Removing a Path" <<std::endl;	
					w->paths.erase(path);
					w->pathByHandle.erase(p->h);
					p=0;
					if(w->paths.empty())
					{
						if(watchers.size() > 1)
							watchers.erase(it);
						w->can_run = false;
						w=0;
					}
				}
				std::cout << "Removing handler was successfull!!!" <<std::endl;
				return true;
			}
		}
	}
	return false;
}


void monitor::process_watchers()
{
	BOOST_AFIO_LOCK_GUARD<monitor> h(*this);
	BOOST_FOREACH( auto &i, this->watchers)
	{
		try
		{
			if(i.can_run.load())
				i.run();
		}
		catch(...)
		{
			std::cout << "An error was detected in process_watchers!!!! \n";
			continue;
		}
	}
	std::cout << "process_watchers called" << std::endl;
}


void dir_monitor::add(const std::filesystem::path &_path, dir_monitor::ChangeHandler handler)
{
	//mon.add(_path, std::move(handler));
}

bool dir_monitor::remove(const std::filesystem::path &path, dir_monitor::ChangeHandler handler)
{
	return true;
	//return mon.remove(path, std::move(handler));
}


	}// namespace afio
}//namespace boost