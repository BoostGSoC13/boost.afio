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



monitor::monitor() : running(true), eventcounter(0)
{
	//std::cout << "monitor constructed" << std::endl;
	dispatcher = boost::afio::make_async_file_io_dispatcher();
	threadpool = process_threadpool();

	finished = threadpool->enqueue([this](){
	//	std::cout << "this lambda was called" << std::endl;
		while(this->is_running())
		{
			//std::cout << "running was true here" << std::endl;
			this->process_watchers(); 
		}
	//	std::cout << "this lambda is ending..." << std::endl;
	});
#ifdef USE_INOTIFY
	BOOST_AFIO_ERRHOS(inotifyh=inotify_init());
#endif
#ifdef USE_KQUEUES
	BOOST_AFIO_ERRHOS(kqueueh=kqueue());
#endif

}

monitor::monitor(std::shared_ptr<boost::afio::async_file_io_dispatcher_base> _dispatcher):running(true), eventcounter(0), dispatcher(_dispatcher)
{
	dispatcher = boost::afio::make_async_file_io_dispatcher();
	threadpool = process_threadpool();
	finished = threadpool->enqueue([this](){
		while(this->is_running())
		{
			this->process_watchers(); 
		}
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
	//std::cout << "Watchers have been stoped (i think)..." << std::endl;
	finished.get();
	//std::cout << "future has completed" << std::endl;
	/*if(my_thread && my_thread->joinable())
		my_thread->join();*/
	watchers.clear();
	//std::cout << "stopping the threadpool ..." <<std::endl;
	//threadpool.reset();
	//dispatcher.reset();
	//std::cout << "Threadpool stopped." <<std::endl;
	#ifdef USE_INOTIFY
		if(inotifyh)
		{
			//std::cout << "trying to close inotify ..." << std::endl;
			BOOST_AFIO_ERRHOS(::close(inotifyh));
			inotifyh=0;
			//std::cout << "inotify closed!" << std::endl;
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
	paths.clear();
	//std::cout << "watcher has been destroyed" << std::endl;
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
		//BOOST_AFIO_LOCK_GUARD<monitor> h((*this), boost::adopt_lock);
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
	BOOST_AFIO_LOCK_GUARD<boost::mutex> lg(mtx);
	int ret;
	char buffer[4096];
	//std::cout << "run() was called" << std::endl;
	//for(;;)
	//{
		try
		{
			if(0 <= (ret=read(parent->inotifyh, buffer, sizeof(buffer))))
			{
				//std::cout << "run() made it to the if statement" << std::endl;
				BOOST_AFIO_ERRHOS(ret);
				for(inotify_event *ev=(inotify_event *) buffer; ((char *)ev)-buffer<ret; ev+=ev->len+sizeof(inotify_event))
				{
					if(ev->mask & IN_IGNORED)
						continue;
#if 1
//#ifdef DEBUG
					printf("dir_monitor: inotify reports wd=%d, mask=%u, cookie=%u, name=%s\n", ev->wd, ev->mask, ev->cookie, ev->name);
#endif
					//if the event is the watch being removed, we don't care, so dont callHandler()
					
					auto it = pathByHandle.find(ev->wd); 
					Path* p = (it == pathByHandle.end()) ? NULL : it->second;
					//assert(p);
					if(p != NULL)
					{
						//std::cout << "making the call to callHandlers()" <<std::endl;
						try
						{
							p->callHandlers();
						}
						catch(std::exception &e)
						{
							std::cout << "An error occurred durring callHandlers() ...\n" << e.what() <<std::endl;	
							//std::abort();
							throw(e);
						}
						//std::cout << "done with call from callHandlers()" <<std::endl;
					}
					else
						std::cout << "Some thing is wrong with p in run()..." <<std::endl;	

				}
			}
		}
		catch(std::exception &e)
		{
			std::cout << "Error from boost::afio::monitor::Watcher::run(): " << e.what() << std::endl;
			throw(e);
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
					//BOOST_AFIO_LOCK_GUARD<monitor> h((*this));
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
	//BOOST_AFIO_LOCK_GUARD<recursive_mutex> h(parent->mtx);
	//std::cout << "invoke() was called" <<std::endl;
	//fxmessage("dir_monitor dispatch %p\n", callv);
	size_t i=1;

	std::vector<async_io_op> ops;
	ops.reserve(changes.size());
	std::vector<std::function<void()>> closures;
	closures.reserve(changes.size());
	for(auto it=changes.begin(); it!=changes.end(); ++it, ++i)
	{
		auto ch = *it;
		//printf("invoke() called the handler %d times so far\n", i);
		//std::cout << "The handler address is " << handler <<std::endl;
		auto func = this->handler;
		ops.push_back(async_io_op());
		closures.push_back([func, ch](){(*func)(ch.change, ch.oldfi, ch.newfi);});
	}
	parent->parent->parent->dispatcher->call(ops, closures);
	//std::cout << "invoke() has completed" <<std::endl;
}

#if 1
std::pair<monitor::Watcher::Path::Change*, directory_entry*> monitor::Watcher::Path::compare_entries(directory_entry& entry, std::shared_ptr< async_io_handle > dirh)
{
	Change* ret = nullptr;
	directory_entry* ptr = nullptr;
	//entry.full_lstat(dirh);// this does happen elsewhere, but just in case while testing...
	//std::cout << "stated file:" << entry.name() <<std::endl;
	try
	{
		
		// try to find the directory_entry
		// if it exists, then determine if anything has changed
		// if the hash is successful then it has the same inode and ctim
		ptr = &entry_dict.at(entry);
		auto temp = *ptr;
		bool changed = false, renamed = false, modified = false, security = false;
		if(temp.name() != entry.name())
		{
			changed = true;
			renamed = true;
		}
		if(temp.st_mtim(dirh)!= entry.st_mtim(dirh)
				|| temp.st_size(dirh) != entry.st_size(dirh) 
				|| temp.st_allocated(dirh) != entry.st_allocated(dirh))
		{
			modified = true;
			changed = true;
		}
		if(temp.st_type(dirh) != entry.st_type(dirh))// mode is linux only...
		{
			changed = true;
			security = true;
		}
		//BOOST_AFIO_LOCK_GUARD<boost::mutex> lk(mtx);
		if(changed)
		{
			ret = new Change(temp, entry);
			ret->change.eventNo=++(parent->parent->eventcounter);
			ret->change.setRenamed(renamed);
			ret->change.setModified(modified);
			ret->change.setSecurity(security);
			//BOOST_AFIO_LOCK_GUARD<monitor> lk(*parent->parent);
			//changes.push_back(ch);
			//return *ret;
		}

		// we found this entry, so remove it to later 
		// determine what has been deleted
		// this shouldn't invalidate any iterators, but maybe its still not good
		//entry_dict.erase(temp); 
	}
	catch(std::out_of_range &e)
	{
		//std::cout << "we have a new file: "  << entry.name() <<std::endl;
		//We've never seen this before
		ret = new Change(directory_entry(), entry);
		ret->change.eventNo=++(parent->parent->eventcounter);
		ret->change.setCreated();
		//BOOST_AFIO_LOCK_GUARD<boost::mutex> lk(mtx);
		//changes.push_back(ch);
		//return *ret;
	}
	catch(std::exception &e)
	{
		std::cout << "another type of error occured that is ruining this\n" << e.what() <<std::endl;;
		throw(e);
	}
	return std::make_pair(ret, ptr);	
}
#endif
void monitor::Watcher::Path::callHandlers()
{	
	//std::cout << pathdir->size();
	//std::cout << std::filesystem::absolute(this->path) << std::endl;
	BOOST_AFIO_LOCK_GUARD<recursive_mutex> lk(mtx);
	//std::cout << "callHandlers() was called" <<std::endl;
	

	//enumeratate the directory
	auto ab_path = std::filesystem::absolute(path);//maybe add FASTDIRECTRORY ENUMERATION flag???
	
	// enumerate directory
	auto rootdir(parent->parent->dispatcher->dir(boost::afio::async_path_op_req(ab_path)));
	boost::afio::async_io_op my_op;
	std::pair<std::vector<boost::afio::directory_entry>, bool> list;
	// This is used to reset the enumeration to the start
	bool restart=true;
	do
	{
	    // Schedule an enumeration of an open directory handle
	    std::pair<
	        boost::afio::future<std::pair<std::vector<boost::afio::directory_entry>, bool>>,
	        boost::afio::async_io_op
	    >  enumeration(
	       parent->parent->dispatcher->enumerate(boost::afio::async_enumerate_op_req(
	        	rootdir,boost::afio::directory_entry::compatibility_maximum(), restart)));
	    restart=false;

	    list=enumeration.first.get();
	   	my_op = enumeration.second;
	} while(list.second);
	std::shared_ptr<std::vector<directory_entry>> newpathdir = std::make_shared<std::vector<directory_entry>>(std::move(list.first));
	
	//std::cout <<"enumeration completed" <<std::endl;
	std::list<Change> changes;
	auto handle_ptr = my_op.h->get();

#define ASYNC_	
#ifdef ASYNC_
	
	std::vector<async_io_op> ops;
	ops.reserve(newpathdir->size());
	std::vector<std::function<std::pair<Change*, directory_entry*>()>> closures;
	closures.reserve(newpathdir->size());
	std::vector<std::function<void()>> full_stat;
	full_stat.reserve(newpathdir->size());
#endif
	for(auto it = newpathdir->begin(); it != newpathdir->end(); ++it)
	{
#ifdef ASYNC_
		directory_entry* my_ptr = &(*it);
		full_stat.push_back([my_ptr, &handle_ptr](){my_ptr->full_lstat(handle_ptr);});
		ops.push_back(my_op);
#else
		try// this all worked better when I could hash w/ birthtim...
		{
			it->full_lstat(my_op.h->get());//think this is wrong...

			// try to find the directory_entry
			// if it exists, then determine if anything has changed
			// if the hash is successful then it has the same inode and ctim
			auto temp = entry_dict.at(*it);

			bool changed = false, renamed = false, modified = false, security = false;
			// determine if any changes
			if(temp.name() != it->name())
			{
				changed = true;
				renamed = true;
			}
			if(temp.st_mtim(handle_ptr)!= it->st_mtim(handle_ptr)
					|| temp.st_size(handle_ptr) != it->st_size(handle_ptr) 
					|| temp.st_allocated(handle_ptr) != it->st_allocated(handle_ptr))
			{
				modified = true;
				changed = true;
			}
			if(temp.st_type(handle_ptr) != it->st_type(handle_ptr))// mode is linux only...
			{
				changed = true;
				security = true;
			}
			if(changed)
			{
				Change ch(temp, (*it));
				ch.change.eventNo=++(parent->parent->eventcounter);
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
			//std::cout << "we have a new file\n";
			//We've never seen this before
			Change ch(directory_entry(), (*it));
			ch.change.eventNo=++(parent->parent->eventcounter);
			ch.change.setCreated();
			changes.push_back(ch);
		}
		catch(std::exception &e)
		{
			std::cout << "another type of error occured that is ruining this\n" << e.what() <<std::endl;;
			throw;
		}
#endif
	}

#ifdef ASYNC_
	
	//std::cout <<"prepare for async stating" <<std::endl;
	// execute stats asynchonously
	auto stat_execute(parent->parent->dispatcher->call(ops, full_stat));
	//std::cout <<"stating scheduled..." <<std::endl;
	when_all(stat_execute.second.begin(), stat_execute.second.end()).wait();
	//std::cout <<"stating complete" <<std::endl;
	for(auto it = newpathdir->begin(); it != newpathdir->end(); ++it)
	{
		closures.push_back(std::bind(&boost::afio::monitor::Watcher::Path::compare_entries, this, *it, handle_ptr));
	}
	//std::cout <<"closures created" <<std::endl;
	auto called_ops(parent->parent->dispatcher->call(stat_execute.second, closures));
	//std::cout <<"closures scheduled" <<std::endl;
	when_all(called_ops.second.begin(), called_ops.second.end()).wait();
	//std::cout <<"closures completed successfully" <<std::endl;

	BOOST_FOREACH(auto &i, called_ops.first)// after this works make it async
	{
		try
		{	std::pair<Change*, directory_entry*> result;
			try{result = i.get();}catch(...){std::cout << "trouble getting results of comps\n"; throw;}
			if(result.first != nullptr)
				changes.push_back(*result.first);
			if(result.second != nullptr)
			{
				try{
					//result.second->full_lstat(handle_ptr);
					entry_dict.erase(*result.second);
				}catch(...){std::cout << "bad data from comps!!!!!!\n"; throw;}
			}
		}
		catch(std::exception &e){
			std::cout << "error moveing changes\n" << e.what() << std::endl;
			throw(e);
		}
		
	}
#endif

	//std::cout << "list of changes created" << std::endl;
	// anything left in entry_dict has been deleted
	for(auto it = entry_dict.begin(); it != entry_dict.end(); ++it)
	{
		//std::cout << "File was deleted: "  << it->second.name() <<std::endl;
		Change ch((it->second), directory_entry());
		ch.change.eventNo=++(parent->parent->eventcounter);
		ch.change.setDeleted();
		changes.push_back(std::move(ch));
	}
	entry_dict.clear();
	
	//std::cout << "Number of changes: "<< changes.size() << std::endl; 
	//this seems wrong...
	auto resetchanges = boost::afio::detail::Undoer([this, &changes](){ this->resetChanges(&changes); } );

	// Dispatch
	Watcher::Path::Handler *handler;
	for(auto it = handlers.begin(); it != handlers.end(); ++it)
	{	
		handler = &(*it);	
		//std::cout << "handlers size is: " << handlers.size() <<std::endl;
		//parent->future_queue.bounded_push(parent->parent->threadpool->enqueue([handler, &changes](){ handler->invoke(changes); }));
		parent->parent->threadpool->enqueue([handler, &changes](){ handler->invoke(changes); });
	}
	resetchanges.dismiss();
	// update pathdir and entry_dict
	pathdir.swap(newpathdir);
	for(auto it = pathdir->begin(); it != pathdir->end(); ++it)
	{
		//it->full_lstat(handle_ptr);
		entry_dict.insert(std::make_pair(*it, *it));
	}
	//std::cout <<"Exiting callHandlers()" << std::endl;

}

void monitor::add(const std::filesystem::path &path, dir_monitor::ChangeHandler& handler)
{
	auto ab_path = std::filesystem::absolute(path);

	std::cout << "adding a directory to monitor: " << ab_path.string() << std::endl;
	BOOST_AFIO_LOCK_GUARD<monitor> lh((*this));
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
		//w->can_run = true;
		watchers.push_back(w);
		unnew.dismiss();
	}

	Watcher::Path *p;
	auto temp = w->paths.find(ab_path);
	if (temp != w->paths.end())
		p = &(temp->second);
	else
	{

		auto unnew = boost::afio::detail::Undoer([&p]{delete p; p = nullptr;});
		w->paths.insert(std::make_pair(ab_path, std::move(Watcher::Path(w, ab_path))));
		
		// fix this to be safe and compliant!!!!!!!!!!!!

		temp = w->paths.find(ab_path);
		if (temp != w->paths.end())
			p = &(temp->second);
		else
			std::cout << "horrible error here !!!!!!!!!!!!!" << std::endl; //<----------------------------------Fix this!!!
		

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
		//std::cout <<"C string path: " << full_path.c_str() << std::endl;
		BOOST_AFIO_ERRHOS(h=inotify_add_watch(this->inotifyh, ab_path.c_str(), IN_ALL_EVENTS&~(IN_ACCESS|IN_CLOSE_NOWRITE|IN_OPEN)));
		//std::cout << "inotifyh value is: " << this->inotifyh <<  " h value is: " << h << std::endl;
		p->h=h;
		w->pathByHandle.insert(std::make_pair(h, p));
#endif
#ifdef USE_KQUEUES
		int h;
		BOOST_AFIO_ERRHOS(h=::open(ab_path.c_str(),
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
		//w->paths.insert(std::make_pair(ab_path, *p));
		//p = w->paths[path];
		//std::cout << "made it here 2" << std::endl;
		unnew.dismiss();

	}
	
	Watcher::Path::Handler *h;
	auto unh = boost::afio::detail::Undoer([&h]{delete h; h = nullptr;});
	h=new Watcher::Path::Handler(p, handler);
	p->handlers.push_back(h);
	unh.dismiss();
	if(w)
		w->can_run = true;
	//std::cout << "Successfuly added " << ab_path.string() << std::endl;
}

bool monitor::remove(const std::filesystem::path &path, dir_monitor::ChangeHandler &handler)
{
	//std::cout << "Starting to remove..." <<std::endl;
	BOOST_AFIO_LOCK_GUARD<monitor> hl(*this);
	//std::cout << "Lock guard aquired" <<std::endl;
	auto ab_path = std::filesystem::absolute(path);
	Watcher *w;
	for(auto it = watchers.begin(); it != watchers.end(); ++it)
	{
		//std::cout << "Going through watchers ..." <<std::endl;
		w = &(*it);
		Watcher::Path *p;
		auto iter = w->paths.find(ab_path);
		if(iter != w->paths.end())
			p = &(iter->second);
		else
			continue;
		Watcher::Path::Handler *h;
		for(auto it2 = p->handlers.begin(); it2 != p->handlers.end(); ++it2)
		{
			//std::cout << "looking for a handler to remove ..." <<std::endl;
			h = &(*it2);
			if(h->handler == &handler)
			{
				//std::cout << "Found the handler to remove!!!" <<std::endl;
				p->handlers.erase(it2);
				h = NULL;
				if(p->handlers.empty())
				{
#ifdef USE_WINAPI
					BOOST_AFIO_ERRHWIN(SetEvent(w->latch));
#endif
					//std::cout << "Removing a Path" <<std::endl;	
					w->paths.erase(ab_path);
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
	//BOOST_AFIO_LOCK_GUARD<monitor> h(*this);
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
	//std::cout << "process_watchers called" << std::endl;
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