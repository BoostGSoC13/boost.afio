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
#include "boost/thread.hpp"		// May undefine USE_WINAPI and USE_POSIX
#ifndef USE_POSIX
 #define USE_WINAPI
#else
 #if defined(__linux__)
  #include <sys/inotify.h>
  #include <unistd.h>
  #define USE_INOTIFY
 
 #elif defined(__APPLE__) || defined(__FreeBSD__)
  #include <xincs.h>
  #include <sys/event.h>
  #define USE_KQUEUES
  #warning Using BSD kqueues - NOTE THAT THIS PROVIDES REDUCED FUNCTIONALITY!
 #else
  #error FAM is not available and no alternative found!
 #endif
#endif

#include "afio.hpp"
#include "dir_monitor.hpp"
#include "detail/ErrorHandling.hpp"
#include <boost/ptr_container/ptr_list.hpp>


namespace boost { 
	namespace afio {

typedef std::future<void> thread_handle;

struct monitor : public recursive_mutex
{
	struct Watcher : public thread
	{
		struct Path
		{
			Watcher *parent;
			std::unique_ptr<std::vector<directory_entry>> pathdir;
			std::unordered_map<directory_entry, bool> entry_dict;
			std::unordered_map<directory_entry::stat_t, bool> stat_dict; 

#ifdef USE_WINAPI
			HANDLE h;
#endif
#ifdef USE_INOTIFY
			int h;
#endif
#ifdef USE_KQUEUES
			struct kevent h;
#endif

			struct Change
			{
				dir_event change;
				const directory_entry * /*FXRESTRICT*/ oldfi, * /*FXRESTRICT*/ newfi;
				unsigned int myoldfi : 1;
				unsigned int mynewfi : 1;
				Change(const directory_entry * /*FXRESTRICT*/ _oldfi, const directory_entry * /*FXRESTRICT*/ _newfi) : oldfi(_oldfi), newfi(_newfi), myoldfi(0), mynewfi(0) { }
				~Change()
				{
					if(myoldfi) { delete oldfi; oldfi = NULL; }
					if(mynewfi) { delete newfi; newfi = NULL; }
				}
				bool operator==(const Change &o) const { return oldfi==o.oldfi && newfi==o.newfi; }
				void make_fis()
				{
					if(oldfi)
					{
						oldfi=new directory_entry(*oldfi);
						myoldfi=true;
					}
					if(newfi)
					{
						newfi=new directory_entry(*newfi);
						mynewfi=true;
					}
				}
				void reset_fis()
				{
					oldfi=0; myoldfi=false;
					newfi=0; mynewfi=false;
				}
			};

			struct Handler
			{
				Path *parent;
				dir_monitor::ChangeHandler handler;
				std::list<thread_handle> callvs;
				Handler(Path *_parent, dir_monitor::ChangeHandler _handler) : parent(_parent), handler(std::move(_handler)) { }
				~Handler();
				void invoke(const std::vector<Change> &changes, thread_handle callv);
			private:
				Handler(const Handler &);
				Handler &operator=(const Handler &);
			};

			boost::ptr_vector<Handler> handlers;
			
			Path(Watcher *_parent, const std::filesystem::path &_path)
				: parent(_parent), handlers(true)
#if defined(USE_WINAPI) || defined(USE_INOTIFY)
				, h(0)
#endif
			{
#if defined(USE_KQUEUES)
				memset(&h, 0, sizeof(h));
#endif
				void *addr=begin_enumerate_directory(_path);
				std::unique_ptr<std::vector<directory_entry>> chunk;
				while((chunk=enumerate_directory(addr, NUMBER_OF_FILES)))
					if(!pathdir)
						pathdir=std::move(chunk);
					else
						pathdir->insert(pathdir->end(), std::make_move_iterator(chunk->begin()), std::make_move_iterator(chunk->end()));
				end_enumerate_directory(addr);
				entry_dict.clear();
				stat_dict.clear();
				for(auto it = pathdir.begin(); it != pathdir.end(); ++it)
				{
					entry_dict.insert(std::make_pair(*it, true));
					stat_dict.insert(std::make_pair(it->stat, true));
				}
				
			}

			~Path();
			void resetChanges(std::list<Change> *changes)
			{
				for(auto it=changes->begin(); it!=changes->end(); ++it)
					it->reset_fis();
			}

			void callHandlers();
		};

		std::unordered_map< std::filesystem::path, Path> paths;
#ifdef USE_WINAPI
		HANDLE latch;
		std::unordered_map<std::filesystem::path, Path*> pathByHandle;
#else
		std::unordered_map<int, Path> pathByHandle;
#ifdef USE_KQUEUES
		struct kevent cancelWaiter;
#endif
#endif
		Watcher();
		~Watcher();
		void run();
		void *cleanup();
	};

#ifdef USE_INOTIFY
	int inotifyh;
#endif
#ifdef USE_KQUEUES
	int kqueueh;
#endif
	boost::ptr_list<Watcher> watchers;
	std::shared_ptr<std_thread_pool>  threadpool;
	monitor();
	~monitor();
	void add(const std::filesystem::path &path, dir_monitor::ChangeHandler handler);
	bool remove(const std::filesystem::path &path, dir_monitor::ChangeHandler handler);
};
static monitor mon();

monitor::monitor() : watchers()
{
#ifdef USE_INOTIFY
	BOOST_AFIO_ERRHOS(inotifyh=inotify_init());
#endif
#ifdef USE_KQUEUES
	BOOST_AFIO_ERRHOS(kqueueh=kqueue());
#endif

}

monitor::~monitor()
{ 
	threadpool = process_threadpool();
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

monitor::Watcher::Watcher() : thread(), paths(13)
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
	join();
#ifdef USE_WINAPI
	if(latch)
	{
		BOOST_AFIO_ERRHWIN(CloseHandle(latch));
		latch=0;
	}
#endif
	paths.clear();
}

#ifdef USE_WINAPI
void monitor::Watcher::run()
{
	HANDLE hlist[MAXIMUM_WAIT_OBJECTS];
	hlist[0]=QThread::int_cancelWaiterHandle();
	hlist[1]=latch;
	for(;;)
	{
		//QMtxHold h(f);
		Path *p;
		int idx=2;
		for(QDict::iterator<Path> it(paths); (p=it.current()); ++it)
		{
			hlist[idx++]=p->h;
		}
		//h.unlock();
		DWORD ret=WaitForMultipleObjects(idx, hlist, FALSE, INFINITE);
		if(ret<WAIT_OBJECT_0 || ret>=WAIT_OBJECT_0+idx) { BOOST_AFIO_ERRHWIN(ret); }
		checkForTerminate();
		if(WAIT_OBJECT_0+1==ret) continue;
		ret-=WAIT_OBJECT_0;
		FindNextChangeNotification(hlist[ret]);
		ret-=2;
		//h.relock();
		for(QDict::iterator<Path> it(paths); (p=it.current()); ++it)
		{
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
			if((ret=read(monitor->inotifyh, buffer, sizeof(buffer))))
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
			warning("Error: %s\n", e.what());
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
					BOOST_AFIO_LOCK_GUARD<monitor> h(monitor);
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
		BOOST_AFIO_ERRHOS(inotify_rm_watch(monitor->inotifyh, h));
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

void monitor::Watcher::Path::Handler::invoke(const std::vector<Change> &changes, thread_handle callv)
{
	//fxmessage("dir_monitor dispatch %p\n", callv);
	for(std::vector<Change>::const_iterator it=changes.begin(); it!=changes.end(); ++it)
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
			BOOST_AFIO_LOCK_GUARD<monitor> h(mon);
			callvs.remove(callv); //I think this will be OK instead of removeReffrom QptrList
			//h.unlock();
		}
		handler(ch.change, oldfi, newfi);
	}
}


static const directory_entry& findFIByName(const std::unique_ptr<std::vector<directory_entry>>  list, const std::filesystem::path &name)
{
	for(auto it=list->begin(); it!=list->end(); ++it)
	{
		// Need a case sensitive compare
		if((*it).name()==name) return *it;
	}
	return 0;
}
void monitor::Watcher::Path::callHandlers()
{	// Lock is held on entry
	void *addr=begin_enumerate_directory(pathdir->front().name().root_path());//maybe need a better way to get directory name???
	std::unique_ptr<std::vector<directory_entry>> newpathdir, chunk;
	while((chunk=enumerate_directory(addr, NUMBER_OF_FILES)))
		if(!newpathdir)
			newpathdir=std::move(chunk);
		else
			newpathdir->insert(newpathdir->end(), std::make_move_iterator(chunk->begin()), std::make_move_iterator(chunk->end()));
	end_enumerate_directory(addr);


// consider using an std::vector w/ a binary search instead of 2 hashes

	std::list<directory_entry> rawchanges;
	std::list<Change> changes;/
	for(auto it = newpathdir.begin(); it != newpathdir.end(); ++it)
	{
		try
		{
			// try to find the directory_entry
			// if it exists, then nothing has changed
			// if it doesn't exist, then it has changed
			entry_dict.at(*it);
		}
		catch(std::out_of_range &e)
		{
			try
			{
				auto temp = stat_dict.at(it->stat);
				Change ch(temp, *it);
				ch.renamed = true;
				changes.push_back(ch); //maybe std::move is more appropriate???

			}
			catch(std::out_of_range &e)
			{
				rawchanges.push_back(*it);
			}
		}
	}

	
	
	for(auto it = rawchanges.begin(); it!=rawchanges.end(); )
	{
		auto name = (*it).name();
		Change ch(findFIByName(pathdir, name), findFIByName(newpathdir, name));
		
		// It's possible that between the directory enumeration and fetching metadata
		// entries the entry vanished. Delete any entries which no longer exist
		if((ch.oldfi && !ch.oldfi->name().exists()) || (ch.newfi && !ch.newfi->name().exists()))
		{
			it=rawchanges.erase(it);
			continue;
		}
		if(!ch.oldfi && !ch.newfi)
		{	// Change vanished
			++it;
			continue;
		}
		else if(ch.oldfi && ch.newfi)
		{	// Same file name
			if(ch.oldfi->st_birthtim()!= ch.newfi->st_birthtim())
			{	// File was deleted and recreated, so split into two entries
				Change ch2(ch);
				ch.oldfi=0; ch2.newfi=0;
				changes.push_back(ch2);
			}
			else
			{
				ch.change.modified=(ch.oldfi->st_mtim()!=ch.newfi->st_mtim()
					|| ch.oldfi->st_size()	!= ch.newfi->st_size() 
					|| ch.oldfi_>st_allocated() != ch.newfi->st_allocated());
				/*ch.change.attrib=(ch.oldfi->st_mode()!=ch.newfi->st_mode()
					|| ch.oldfi->isWriteable()	!=ch.newfi->isWriteable()
					|| ch.oldfi->isExecutable()	!=ch.newfi->isExecutable()
					|| ch.oldfi->isHidden()		!=ch.newfi->isHidden());*/
				ch.change.security=(ch.oldfi->st_mode()!=ch.newfi->st_mode());
			}
		}
		changes.push_back(ch);
		++it;
	}

	// Mark off created/deleted
	static unsigned long eventcounter=0;
	for(auto it=changes.begin(); it!=changes.end(); ++it)
	{
		Change &ch=*it;
		ch.change.eventNo=++eventcounter;
		if(ch.oldfi && ch.newfi) continue;
		if(ch.change.renamed) continue;
		ch.change.created=(!ch.oldfi && ch.newfi);
		ch.change.deleted=(ch.oldfi && !ch.newfi);
	}
	// Remove any which don't have something set
	for(auto it=changes.begin(); it!=changes.end();)
	{
		Change &ch=*it;
		if(!ch.change)
			it=changes.erase(it);
		else
			++it;
	}

	//this seems wrong...
	auto resetchanges=Undoer([*this, &changes]{ this->resetChanges(changes));

		//What should I use in place of the fxprocess::threadpool???

	// Dispatch
	Watcher::Path::Handler *handler;
	for(boost::ptr_vector::iterator<Watcher::Path::Handler> it(handlers); (handler=it.current()); ++it)
	{
		auto functor = std::bind(&Watcher::Path::Handler::invoke, handler, changes, 0);// would it be better to just use a lambda??
		// Detach changes per dispatch
		for(auto it=changes.begin(); it!=changes.end(); ++it)
			it->make_fis();
		
		handler->callvs.push_back(threadpool->enqueue(functor));
		// Poke in the callv
		Generic::TL::instance<1>(functor->parameters()).value=callv;
	}
	pathdir=newpathdir;
}

void monitor::add(const FXString &path, dir_monitor::ChangeHandler handler)
{
	QMtxHold lh(monitor);
	Watcher *w;
	for(boost::ptr_list::iterator<Watcher> it(watchers); (w=it.current()); ++it)
	{
#ifdef USE_WINAPI
		if(w->paths.count()<MAXIMUM_WAIT_OBJECTS-2) break;
#endif
#ifdef USE_POSIX
		break;
#endif
	}
	if(!w)
	{
		w=new Watcher;
		FXRBOp unnew=FXRBNew(w);
		w->start();
		watchers.append(w);
		unnew.dismiss();
	}
	Watcher::Path *p=w->paths.find(path);
	if(!p)
	{
		p=new Watcher::Path(w, path);
		FXRBOp unnew=FXRBNew(p);
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
		BOOST_AFIO_ERRHOS(h=inotify_add_watch(monitor->inotifyh, path.text(), IN_ALL_EVENTS&~(IN_ACCESS|IN_CLOSE_NOWRITE|IN_OPEN)));
		p->h=h;
		w->pathByHandle.insert(h, p);
#endif
#ifdef USE_KQUEUES
		int h;
		BOOST_AFIO_ERRHOS(h=::open(path.text(),
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

		w->paths.insert(path, p);
		unnew.dismiss();
	}
	Watcher::Path::Handler *h;
	h=new Watcher::Path::Handler(p, std::move(handler));
	FXRBOp unh=FXRBNew(h);
	p->handlers.append(h);
	unh.dismiss();
}

bool monitor::remove(const FXString &path, dir_monitor::ChangeHandler handler)
{
	QMtxHold hl(monitor);
	Watcher *w;
	for(boost::ptr_list::iterator<Watcher> it(watchers); (w=it.current()); ++it)
	{
		Watcher::Path *p=w->paths.find(path);
		if(!p) continue;
		Watcher::Path::Handler *h;
		for(boost::ptr_vector::iterator<Watcher::Path::Handler> it2(p->handlers); (h=it2.current()); ++it2)
		{
			if(h->handler==handler)
			{
				p->handlers.takeByIter(it2);
				hl.unlock();
				delete h;
				h = NULL;
				hl.relock();
				h=0;
				if(p->handlers.isEmpty())
				{
#ifdef USE_WINAPI
					BOOST_AFIO_ERRHWIN(SetEvent(w->latch));
#endif
					w->paths.remove(path);
					p=0;
					if(w->paths.isEmpty() && watchers.count()>1)
					{
						watchers.removeByIter(it);
						w=0;
					}
				}
				return true;
			}
		}
	}
	return false;
}

void dir_monitor::add(const FXString &_path, dir_monitor::ChangeHandler handler)
{
	FXString path=FXPath::absolute(_path);

	monitor->add(path, std::move(handler));
}

bool dir_monitor::remove(const FXString &path, dir_monitor::ChangeHandler handler)
{
	return monitor->remove(path, std::move(handler));
}


	}// namespace afio
}//namespace boost