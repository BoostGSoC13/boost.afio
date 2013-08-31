namespace boost { 
	namespace afio {

typedef void* thread_handle;


struct monitor : public recursive_mutex
{
	
#ifdef USE_INOTIFY
	int inotifyh;
#endif
#ifdef USE_KQUEUES
	int kqueueh;
#endif
	boost::ptr_list<Watcher> watchers;
	monitor();
	~monitor();
	void add(const std::filesystem::path &path, dir_monitor::ChangeHandler handler);
	bool remove(const std::filesystem::path &path, dir_monitor::ChangeHandler handler);
};

//static FXProcess_StaticInit<FXFSMon> fxfsmon("FXFSMonitor");// what do I do with this???
static monitor mon;

monitor::monitor() : watchers(true)
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
	/*Watcher *w;
	for(boost::ptr_list<Watcher>::iterator it(watchers); (w=it.current()); ++it)
	{
		w->requestTermination();
	}
	for(boost::ptr_list<Watcher>::iterator it(watchers); (w=it.current()); ++it)
	{
		w->wait();
	}
	watchers.clear();*/
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


struct Watcher : public thread
{
	
	std::unorded_map<std::filesystem::path, Path> paths;
#ifdef USE_WINAPI
	HANDLE latch;
	std::unorded_map<std::filesystem::path, Path> pathByHandle;
#else
	std::unorded_map<long, Path> pathByHandle;
#ifdef USE_KQUEUES
	struct kevent cancelWaiter;
#endif
#endif
	Watcher();
	~Watcher();
	void run();
	void *cleanup();
};


monitor::Watcher::Watcher() : thread(), paths(13)
#ifdef USE_WINAPI
	, latch(0)
#endif
{
	set_threadname("Filing system monitor")
#ifdef USE_WINAPI
	BOOST_AFIO_ERRHWIN(latch=CreateEvent(NULL, FALSE, FALSE, NULL));
#endif
}

monitor::Watcher::~Watcher()
{
	//requestTermination();
	//wait();
	join();// is this right???<----------------------------?????????
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
{/*
	HANDLE hlist[MAXIMUM_WAIT_OBJECTS];
	hlist[0]=0;//QThread::int_cancelWaiterHandle();
	hlist[1]=latch;
	for(;;)
	{
		BOOST_AFIO_LOCK_GUARD<monitor> h(mon);
		Path *p;
		int idx=2;
		for(QDict::iterator<Path> it(paths); (p=it.current()); ++it)
		{
			hlist[idx++]=p->h;
		}
		h.unlock();
		DWORD ret=WaitForMultipleObjects(idx, hlist, FALSE, INFINITE);
		if(ret<WAIT_OBJECT_0 || ret>=WAIT_OBJECT_0+idx) { BOOST_AFIO_ERRHWIN(ret); }
		checkForTerminate();
		if(WAIT_OBJECT_0+1==ret) continue;
		ret-=WAIT_OBJECT_0;
		FindNextChangeNotification(hlist[ret]);
		ret-=2;
		h.relock();
		for(QDict::iterator<Path> it(paths); (p=it.current()); ++it)
		{
			if(!ret) break;
			--ret;
		}
		if(p)
			p->callHandlers();
	}*/
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
					QMtxHold h(monitor);
					Path *p=pathByHandle.find(ev->wd);
					assert(p);
					if(p)
						p->callHandlers();
				}
			}
		}
		catch(std::exception &e)
		{
			warning("Error: %s\n", e.report().text());
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
					QMtxHold h(monitor);
					Path *p=pathByHandle.find(kev->ident);
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







struct Change
{
	dir_monitor change;
	const stat_t * /*FXRESTRICT*/ oldfi, * /*FXRESTRICT*/ newfi;
	unsigned int myoldfi : 1;
	unsigned int mynewfi : 1;
	
	Change(const stat_t * /*FXRESTRICT*/ _oldfi, const stat_t * /*FXRESTRICT*/ _newfi) : oldfi(_oldfi), newfi(_newfi), myoldfi(0), mynewfi(0) { }
	
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
			oldfi=new stat_t(*oldfi);
			myoldfi=true;
		}
		if(newfi)
		{
			newfi=new stat_t(*newfi);
			mynewfi=true;
		}
	}

	void reset_fis()
	{
		oldfi=0; myoldfi=false;
		newfi=0; mynewfi=false;
	}
};

struct Path
{
	Watcher *parent;
	std::shared_ptr<dir_apth> pathdir;

#ifdef USE_WINAPI
	HANDLE h;
#endif
#ifdef USE_INOTIFY
	int h;
#endif
#ifdef USE_KQUEUES
	struct kevent h;
#endif

	boost::ptr_vector<Handler> handlers;
	Path(Watcher *_parent, const FXString &_path)
		: parent(_parent), handlers(true)
#if defined(USE_WINAPI) || defined(USE_INOTIFY)
		, h(0)
#endif
	{
#if defined(USE_KQUEUES)
		memset(&h, 0, sizeof(h));
#endif
		pathdir=new QDir(_path, "*", QDir::Unsorted, QDir::All|QDir::Hidden);
		pathdir->entryInfoList();
	}
	~Path();
	void resetChanges(std::list<Change> *changes)
	{
		for(std::list<Change>::iterator it=changes->begin(); it!=changes->end(); ++it)
			it->reset_fis();
	}
	void callHandlers();
};

monitor::Watcher::Path::~Path()
{
#ifdef USE_WINAPI
	if(h)
	{
		parent->pathByHandle.remove(h);
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
	parent->pathByHandle.remove(h.ident);
	if(h.ident)
	{
		BOOST_AFIO_ERRHOS(::close(h.ident));
		h.ident=0;
	}
#endif

}



struct Handler
{
	Path *parent;
	dir_monitor::ChangeHandler handler;
	boost::ptr_list<void> callvs;
	Handler(Path *_parent, dir_monitor::ChangeHandler _handler) : parent(_parent), handler(std::move(_handler)) { }
	~Handler();
	
	void invoke(const std::list<Change> &changes, thread_handle callv);

private:
	Handler(const Handler &);
	Handler &operator=(const Handler &);
};


monitor::Watcher::Path::Handler::~Handler()
{
	QMtxHold h(monitor);
	while(!callvs.empty())
	{
		QThreadPool::CancelledState state;
		while(QThreadPool::WasRunning==(state=FXProcess::threadPool().cancel(callvs.front())));
		callvs.pop_front();
	}
}

void monitor::Watcher::Path::Handler::invoke(const std::list<Change> &changes, thread_handle callv)
{
	//fxmessage("dir_monitor dispatch %p\n", callv);
	for(std::list<Change>::const_iterator it=changes.begin(); it!=changes.end(); ++it)
	{
		const Change &ch=*it;
		const stat_t &oldfi=ch.oldfi ? *ch.oldfi : stat_t();
		const stat_t &newfi=ch.newfi ? *ch.newfi : stat_t();
#ifdef DEBUG
		{
			FXString file(oldfi.filePath()), chs;
			if(ch.change.modified) chs.append("modified ");
			if(ch.change.created)  { chs.append("created "); file=newfi.filePath(); }
			if(ch.change.deleted)  chs.append("deleted ");
			if(ch.change.renamed)  chs.append("renamed (to "+newfi.filePath()+") ");
			if(ch.change.attrib)   chs.append("attrib ");
			if(ch.change.security) chs.append("security ");
			fxmessage("dir_monitor: File %s had changes: %s at %s\n", file.text(), chs.text(), (ch.newfi ? *ch.newfi : *ch.oldfi).lastModified().asString().text());
		}
#endif
		QMtxHold h(monitor);
		callvs.remove(callv); //I think this will be OK instead of removeReffrom QptrList
		h.unlock();
		handler(ch.change, oldfi, newfi);
	}
}


static const stat_t *findFIByName(const std::list<stat_t> *list, const FXString &name)
{
	for(std::list<stat_t>::const_iterator it=list->begin(); it!=list->end(); ++it)
	{
		const stat_t &fi=*it;
		// Need a case sensitive compare
		if(fi.fileName()==name) return &fi;
	}
	return 0;
}
void monitor::Watcher::Path::callHandlers()
{	// Lock is held on entry
	FXAutoPtr<QDir> newpathdir;
	newpathdir=new QDir(pathdir->path(), "*", QDir::Unsorted, QDir::All|QDir::Hidden);
	newpathdir->entryInfoList();
	QStringList rawchanges=QDir::extractChanges(*pathdir, *newpathdir);
	std::list<Change> changes;
	for(QStringList::iterator it=rawchanges.begin(); it!=rawchanges.end(); )
	{
		const FXString &name=*it;
		Change ch(findFIByName(pathdir->entryInfoList(), name), findFIByName(newpathdir->entryInfoList(), name));
		// It's possible that between the directory enumeration and fetching metadata
		// entries the entry vanished. Delete any entries which no longer exist
		if((ch.oldfi && !ch.oldfi->exists()) || (ch.newfi && !ch.newfi->exists()))
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
			if(ch.oldfi->created()!=ch.newfi->created())
			{	// File was deleted and recreated, so split into two entries
				Change ch2(ch);
				ch.oldfi=0; ch2.newfi=0;
				changes.append(ch2);
			}
			else
			{
				ch.change.modified=(ch.oldfi->lastModified()!=ch.newfi->lastModified()
					|| ch.oldfi->size()			!=ch.newfi->size());
				ch.change.attrib=(ch.oldfi->isReadable()!=ch.newfi->isReadable()
					|| ch.oldfi->isWriteable()	!=ch.newfi->isWriteable()
					|| ch.oldfi->isExecutable()	!=ch.newfi->isExecutable()
					|| ch.oldfi->isHidden()		!=ch.newfi->isHidden());
				ch.change.security=(ch.oldfi->permissions()!=ch.newfi->permissions());
			}
		}
		changes.append(ch);
		++it;
	}
	// Try to detect renames
	bool noIncIter1;
	for(std::list<Change>::iterator it1=changes.begin(); it1!=changes.end(); !noIncIter1 ? (++it1, 0) : 0)
	{
		noIncIter1=false;
		Change &ch1=*it1;
		if(ch1.oldfi && ch1.newfi) continue;
		if(ch1.change.renamed) continue;
		bool disable=false;
		Change *candidate=0, *solution=0;
		for(std::list<Change>::iterator it2=changes.begin(); it2!=changes.end(); ++it2)
		{
			if(it1==it2) continue;
			Change &ch2=*it2;
			if(ch2.oldfi && ch2.newfi) continue;
			if(ch2.change.renamed) continue;
			const stat_t *a=0, *b=0;
			if(ch1.oldfi && ch2.newfi) { a=ch1.oldfi; b=ch2.newfi; }
			else if(ch1.newfi && ch2.oldfi) { a=ch2.oldfi; b=ch1.newfi; }
			else continue;
#ifdef DEBUG
			fxmessage("dir_monitor: Rename candidate %s, %s (%llu==%llu, %llu==%llu, %llu==%llu, %llu==%llu) candidate=%p\n",
					  a->fileName().text(), b->fileName().text(),
					  a->size(), b->size(),
					  a->created().value, b->created().value,
					  a->lastModified().value, b->lastModified().value,
					  a->lastRead().value, b->lastRead().value,
					  candidate);
#endif
			if(a->size()==b->size() && a->created()==b->created()
				&& a->lastModified()==b->lastModified()
				&& a->lastRead()==b->lastRead())
			{
				if(candidate) disable=true;
				else
				{
					candidate=&ch1; solution=&ch2;
				}
			}
		}
		if(candidate && !disable)
		{
			if(candidate->newfi && solution->oldfi)
			{
				Change *temp=candidate;
				candidate=solution;
				solution=temp;
			}
			solution->oldfi=candidate->oldfi;
			solution->change.renamed=true;
			if((noIncIter1=(candidate==&(*it1))))
				++it1;
			changes.remove(Change(*candidate));
		}
	}
	// Mark off created/deleted
	static FXulong eventcounter=0;
	for(std::list<Change>::iterator it=changes.begin(); it!=changes.end(); ++it)
	{
		Change &ch=*it;
		ch.change.eventNo=++eventcounter;
		if(ch.oldfi && ch.newfi) continue;
		if(ch.change.renamed) continue;
		ch.change.created=(!ch.oldfi && ch.newfi);
		ch.change.deleted=(ch.oldfi && !ch.newfi);
	}
	// Remove any which don't have something set
	for(std::list<Change>::iterator it=changes.begin(); it!=changes.end();)
	{
		Change &ch=*it;
		if(!ch.change)
			it=changes.erase(it);
		else
			++it;
	}
	FXRBOp resetchanges=FXRBObj(*this, &monitor::Watcher::Path::resetChanges, &changes);
	// Dispatch
	Watcher::Path::Handler *handler;
	for(boost::ptr_vector::iterator<Watcher::Path::Handler> it(handlers); (handler=it.current()); ++it)
	{
		typedef Generic::TL::create<void, std::list<Change>, thread_handle>::value Spec;
		Generic::BoundFunctor<Spec> *functor;
		// Detach changes per dispatch
		for(std::list<Change>::iterator it=changes.begin(); it!=changes.end(); ++it)
			it->make_fis();
		thread_handle callv=FXProcess::threadPool().dispatch((functor=new Generic::BoundFunctor<Spec>(Generic::Functor<Spec>(*handler, &Watcher::Path::Handler::invoke), changes, 0)));
		handler->callvs.push_back(callv);
		// Poke in the callv
		Generic::TL::instance<1>(functor->parameters()).value=callv;
	}
	pathdir=newpathdir;
}