namespace boost { 
	namespace afio {

typedef void* thread_handle;


struct monitor : public boost::mutex
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
	Watcher *w;
	for(boost::ptr_list<Watcher>::iterator it(watchers); (w=it.current()); ++it)
	{
		w->requestTermination();
	}
	for(boost::ptr_list<Watcher>::iterator it(watchers); (w=it.current()); ++it)
	{
		w->wait();
	}
	watchers.clear();
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
	requestTermination();
	wait();
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
		QMtxHold h(monitor);
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
		FXERRH_TRY
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