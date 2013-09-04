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



monitor::monitor()
{
	threadpool = process_threadpool();
#ifdef USE_INOTIFY
	BOOST_AFIO_ERRHOS(inotifyh=inotify_init());
#endif
#ifdef USE_KQUEUES
	BOOST_AFIO_ERRHOS(kqueueh=kqueue());
#endif

}

monitor::~monitor()
{ 
	#ifdef USE_INOTIFY
		if(inotifyh)
		{
			BOOST_AFIO_ERRHOS(::close(inotifyh));
			inotifyh=0;
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

monitor::Watcher::Watcher() : thread()
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
	join();// make sure the thread joins before destruction
#ifdef USE_WINAPI
	if(latch)
	{
		BOOST_AFIO_ERRHWIN(CloseHandle(latch));
		latch=0;
	}
#endif
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
		//mon.lock();
		BOOST_AFIO_LOCK_GUARD<monitor> h(mon, boost::adopt_lock);
		Path *p;
		int idx=1;
		for(auto it = paths.begin(); it != paths.end(); ++it)
		{
			hlist[idx++]=it->h;
		}
		mon.unlock();
		
		DWORD ret=WaitForMultipleObjects(idx, hlist, FALSE, INFINITE);
		if(ret<WAIT_OBJECT_0 || ret>=WAIT_OBJECT_0+idx) { BOOST_AFIO_ERRHWIN(ret); }
		checkForTerminate();
		if(WAIT_OBJECT_0==ret) continue;
		ret-=WAIT_OBJECT_0;
		FindNextChangeNotification(hlist[ret]);
		ret-=1;
		mon.lock();
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
	int ret;
	char buffer[4096];
	for(;;)
	{
		try
		{
			if((ret=read(mon.inotifyh, buffer, sizeof(buffer))))
			{
				BOOST_AFIO_ERRHOS(ret);
				for(inotify_event *ev=(inotify_event *) buffer; ((char *)ev)-buffer<ret; ev+=ev->len+sizeof(inotify_event))
				{
/*#ifdef DEBUG
					fxmessage("dir_monitor: inotify reports wd=%d, mask=%u, cookie=%u, name=%s\n", ev->wd, ev->mask, ev->cookie, ev->name);
#endif*/
					BOOST_AFIO_LOCK_GUARD<monitor> h(mon);
					Path *p=pathByHandle[(ev->wd)];
					assert(p);
					if(p)
						p->callHandlers();
				}
			}
		}
		catch(std::exception &e)
		{
			std::cout << "Error: " << e.what() << std::endl;
			return;
		}
		
	}
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
					BOOST_AFIO_LOCK_GUARD<monitor> h(mon);
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
		BOOST_AFIO_ERRHOS(inotify_rm_watch(mon.inotifyh, h));
		h=0;
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
	BOOST_AFIO_LOCK_GUARD<monitor> h(mon);
	while(!callvs.empty())
	{
		//QThreadPool::CancelledState state;
		//while(QThreadPool::WasRunning==(state=FXProcess::threadPool().cancel(callvs.front())));
		callvs.pop_front();// this should be insufficient I think. need to fix the above lines to work without qt of fx
	}*/


}

void monitor::Watcher::Path::Handler::invoke(const std::list<Change> &changes/*, future_handle callv*/)
{
	//fxmessage("dir_monitor dispatch %p\n", callv);
	for(auto it=changes.begin(); it!=changes.end(); ++it)
	{
		const Change &ch=*it;
		const directory_entry &oldfi=ch.oldfi ? *ch.oldfi : directory_entry();
		const directory_entry &newfi=ch.newfi ? *ch.newfi : directory_entry();
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
			//BOOST_AFIO_LOCK_GUARD<monitor> h(mon);
			//callv.get();
			//callvs.remove(callv); //I think this will be OK instead of removeReffrom QptrList
			//h.unlock();
		}
		handler(ch.change, oldfi, newfi);
	}
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
{	// Lock is held on entry
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
				Change ch(&temp, &(*it));
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
			//We've never seen this before
			Change ch(new directory_entry(), &(*it));
			ch.change.eventNo=++eventcounter;
			ch.change.setCreated();
			changes.push_back(ch);
		}
	}

	// anything left in entry_dict has been deleted
	for(auto it = entry_dict.begin(); it != entry_dict.end(); ++it)
	{
		//We've never seen this before
		Change ch(&(*it).second, new directory_entry());
		ch.change.eventNo=++eventcounter;
		ch.change.setDeleted();
		changes.push_back(ch);
	}
	entry_dict.clear();

	
	//this seems wrong...
	auto resetchanges = boost::afio::detail::Undoer([this, &changes](){ this->resetChanges(&changes); } );

	// Dispatch
	Watcher::Path::Handler *handler;
	for(auto it = handlers.begin(); it != handlers.end(); ++it)
	{		
		// Detach changes per dispatch
		for(auto it2 = changes.begin(); it2 != changes.end(); ++it2)
			it2->make_fis(); 

		mon.threadpool->enqueue([it, &changes](){ it->invoke(changes/*, callv*/); });
		//do we need to keep these futures???
		//handler->callvs.push_back();
	}

	// update pathdir and entry_dict
	pathdir=std::move(newpathdir);
	for(auto it = pathdir->begin(); it != pathdir->end(); ++it)
	{
		entry_dict.insert(std::make_pair(*it, *it));
	}
}

void monitor::add(const std::filesystem::path &path, dir_monitor::ChangeHandler handler)
{
	BOOST_AFIO_LOCK_GUARD<monitor> lh(mon);
	Watcher *w = nullptr;
	for(auto it = watchers.begin(); it != watchers.end() && !w; ++it)
	{
#ifdef USE_WINAPI
		if(w->paths.size() >= MAXIMUM_WAIT_OBJECTS-2) continue;
#endif
		w = &(*it);
	}
	if(!w)
	{
		auto unnew = boost::afio::detail::Undoer([&w]{delete w; w = nullptr;});
		w=new Watcher;
		//w->start();
		watchers.push_back(w);
		unnew.dismiss();
	}
	Watcher::Path *p = &w->paths.find(path)->second;
	if(!p)
	{
		auto unnew = boost::afio::detail::Undoer([&p]{delete p; p = nullptr;});
		p=new Watcher::Path(w, path);
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
		int h;
		BOOST_AFIO_ERRHOS(h=inotify_add_watch(mon.inotifyh, path.c_str(), IN_ALL_EVENTS&~(IN_ACCESS|IN_CLOSE_NOWRITE|IN_OPEN)));
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

		w->paths.insert(std::make_pair(path, *p));
		unnew.dismiss();
	}
	Watcher::Path::Handler *h;
	auto unh = boost::afio::detail::Undoer([&h]{delete h; h = nullptr;});
	h=new Watcher::Path::Handler(p, std::move(handler));
	p->handlers.push_back(h);
	unh.dismiss();
}

bool monitor::remove(const std::filesystem::path &path, dir_monitor::ChangeHandler handler)
{
	BOOST_AFIO_LOCK_GUARD<monitor> hl(mon, boost::adopt_lock);
	Watcher *w;
	for(auto it = watchers.begin(); it != watchers.end(); ++it)
	{
		w = &(*it);
		Watcher::Path *p = &w->paths.find(path)->second;
		if(!p) continue;
		Watcher::Path::Handler *h;
		for(auto it2 = p->handlers.begin(); it2 != p->handlers.end(); ++it2)
		{
			h = &(*it2);
			if(&h->handler == &handler)
			{
				p->handlers.erase(it2);
				mon.unlock();
				delete h;
				h = NULL;
				mon.lock();
				h=0;
				if(p->handlers.empty())
				{
#ifdef USE_WINAPI
					BOOST_AFIO_ERRHWIN(SetEvent(w->latch));
#endif
					w->paths.erase(path);
					p=0;
					if(w->paths.empty() && watchers.size()>1)
					{
						watchers.erase(it);
						w=0;
					}
				}
				return true;
			}
		}
	}
	return false;
}

void dir_monitor::add(const std::filesystem::path &_path, dir_monitor::ChangeHandler handler)
{
	mon.add(_path, std::move(handler));
}

bool dir_monitor::remove(const std::filesystem::path &path, dir_monitor::ChangeHandler handler)
{
	return mon.remove(path, std::move(handler));
}


	}// namespace afio
}//namespace boost