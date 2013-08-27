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
 //#include "WindowsGubbins.h"//fix this <--------------------!!!!!!!!!!!!!!!!!
#else
 #if defined(__linux__)
  #include <sys/inotify.h>
  #include <unistd.h>
  #define USE_INOTIFY
 #elif defined(HAVE_FAM) // will we use this??????????????????????????????????
  #include <fam.h>
  #include <sys/select.h>
  #include "tnfxselect.h"
  #define USE_FAM
  #define FXERRHFAM(ret) if(ret<0) { FXERRG(FXString("FAM Error: %1 (code %2)").arg(FXString(FamErrlist[FAMErrno])).arg(FAMErrno), FXEXCEPTION_OSSPECIFIC, 0); }
 #elif defined(__APPLE__) || defined(__FreeBSD__)
  #include <xincs.h>
  #include <sys/event.h>
  #define USE_KQUEUES
  #warning Using BSD kqueues as FAM is not available - NOTE THAT THIS PROVIDES REDUCED FUNCTIONALITY!
 #else
  #error FAM is not available and no alternative found!
 #endif
#endif

#include "dir_monitor.hpp"
/*
#include "FXString.h"
#include "FXProcess.h"	//
#include "FXRollback.h" // I think htis is undoer
#include "FXPath.h"
#include "FXStat.h"
#include "QDir.h"
#include "QFileInfo.h"
#include "QTrans.h"
#include "FXErrCodes.h"
#include <qptrlist.h>
#include <qdict.h>
#include <qptrdict.h>
#include <qintdict.h>
#include <qptrvector.h>
#include <assert.h>
#include "FXMemDbg.h"
*/
/*
#if defined(DEBUG) && defined(FXMEMDBG_H)
static const char *_fxmemdbg_current_file_ = __FILE__;
#endif




*/

namespace boost { 
	namespace afio {

struct monitor : public boost::mutex
{
	struct Watcher : public boost::thread
	{
		struct Path
		{
			Watcher *parent;
			FXAutoPtr<QDir> pathdir; //std::shared_ptr<dir_apth> ; QDir == boost::filesystem::path???
#ifdef USE_WINAPI
			HANDLE h;
#endif
#ifdef USE_INOTIFY
			int h;
#endif
#ifdef USE_KQUEUES
			struct kevent h;
#endif
#ifdef USE_FAM
			FAMRequest h;
#endif
			struct Change
			{
				dir_monitor change;
				const QFileInfo *FXRESTRICT oldfi, *FXRESTRICT newfi;
				FXuint myoldfi : 1;
				FXuint mynewfi : 1;
				Change(const QFileInfo *FXRESTRICT _oldfi, const QFileInfo *FXRESTRICT _newfi) : oldfi(_oldfi), newfi(_newfi), myoldfi(0), mynewfi(0) { }
				~Change()
				{
					if(myoldfi) { FXDELETE(oldfi); }
					if(mynewfi) { FXDELETE(newfi); }
				}
				bool operator==(const Change &o) const { return oldfi==o.oldfi && newfi==o.newfi; }
				void make_fis()
				{
					if(oldfi)
					{
						FXERRHM(oldfi=new QFileInfo(*oldfi));
						myoldfi=true;
					}
					if(newfi)
					{
						FXERRHM(newfi=new QFileInfo(*newfi));
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
				FXFSMonitor::ChangeHandler handler;
				QPtrList<void> callvs;
				Handler(Path *_parent, FXFSMonitor::ChangeHandler _handler) : parent(_parent), handler(std::move(_handler)) { }
				~Handler();
				void invoke(const QValueList<Change> &changes, QThreadPool::handle callv);
			private:
				Handler(const Handler &);
				Handler &operator=(const Handler &);
			};
			QPtrVector<Handler> handlers;
			Path(Watcher *_parent, const FXString &_path)
				: parent(_parent), handlers(true)
#if defined(USE_WINAPI) || defined(USE_INOTIFY)
				, h(0)
#endif
			{
#if defined(USE_KQUEUES) || defined(USE_FAM)
				memset(&h, 0, sizeof(h));
#endif
				FXERRHM(pathdir=new QDir(_path, "*", QDir::Unsorted, QDir::All|QDir::Hidden));
				pathdir->entryInfoList();
			}
			~Path();
			void resetChanges(QValueList<Change> *changes)
			{
				for(QValueList<Change>::iterator it=changes->begin(); it!=changes->end(); ++it)
					it->reset_fis();
			}
			void callHandlers();
		};
		QDict<Path> paths;
#ifdef USE_WINAPI
		HANDLE latch;
		QPtrDict<Path> pathByHandle;
#else
		QIntDict<Path> pathByHandle;
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
#ifdef USE_FAM
	bool nofam, fambroken;
	FAMConnection fc;
#endif
	QPtrList<Watcher> watchers;
	FXFSMon();
	~FXFSMon();
	void add(const FXString &path, FXFSMonitor::ChangeHandler handler);
	bool remove(const FXString &path, FXFSMonitor::ChangeHandler handler);
};
static FXProcess_StaticInit<FXFSMon> fxfsmon("FXFSMonitor");

FXFSMon::FXFSMon() : watchers(true)
{
#ifdef USE_INOTIFY
	FXERRHOS(inotifyh=inotify_init());
#endif
#ifdef USE_KQUEUES
	FXERRHOS(kqueueh=kqueue());
#endif
#ifdef USE_FAM
	nofam=true;
	fambroken=false;
	int ret=FAMOpen(&fc);
	if(ret)
	{
		fxwarning("Disabling FXFSMonitor due to FAMOpen() throwing error %d (%s) syserror: %s\nIs the famd daemon running?\n", FAMErrno, FamErrlist[FAMErrno], strerror(errno));
	}
	else nofam=false;
	//FXERRHOS(ret);
#endif
}

FXFSMon::~FXFSMon()
{ FXEXCEPTIONDESTRUCT1 {
	Watcher *w;
	for(QPtrListIterator<Watcher> it(watchers); (w=it.current()); ++it)
	{
		w->requestTermination();
	}
	for(QPtrListIterator<Watcher> it(watchers); (w=it.current()); ++it)
	{
		w->wait();
	}
	watchers.clear();
#ifdef USE_INOTIFY
	if(inotifyh)
	{
		FXERRHOS(::close(inotifyh));
		inotifyh=0;
	}
#endif
#ifdef USE_KQUEUES
	if(kqueueh)
	{
		FXERRHOS(::close(kqueueh));
		kqueueh=0;
	}
#endif
#ifdef USE_FAM
	if(!nofam)
	{
		FXERRHFAM(FAMClose(&fc));
	}
#endif
} FXEXCEPTIONDESTRUCT2; }

FXFSMon::Watcher::Watcher() : QThread("Filing system monitor", false, 128*1024, QThread::InProcess), paths(13, true, true)
#ifdef USE_WINAPI
	, latch(0)
#endif
{
#ifdef USE_WINAPI
	FXERRHWIN(latch=CreateEvent(NULL, FALSE, FALSE, NULL));
#endif
}

FXFSMon::Watcher::~Watcher()
{
	requestTermination();
	wait();
#ifdef USE_WINAPI
	if(latch)
	{
		FXERRHWIN(CloseHandle(latch));
		latch=0;
	}
#endif
	paths.clear();
}

#ifdef USE_WINAPI
void FXFSMon::Watcher::run()
{
	HANDLE hlist[MAXIMUM_WAIT_OBJECTS];
	hlist[0]=QThread::int_cancelWaiterHandle();
	hlist[1]=latch;
	for(;;)
	{
		QMtxHold h(fxfsmon);
		Path *p;
		int idx=2;
		for(QDictIterator<Path> it(paths); (p=it.current()); ++it)
		{
			hlist[idx++]=p->h;
		}
		h.unlock();
		DWORD ret=WaitForMultipleObjects(idx, hlist, FALSE, INFINITE);
		if(ret<WAIT_OBJECT_0 || ret>=WAIT_OBJECT_0+idx) { FXERRHWIN(ret); }
		checkForTerminate();
		if(WAIT_OBJECT_0+1==ret) continue;
		ret-=WAIT_OBJECT_0;
		FindNextChangeNotification(hlist[ret]);
		ret-=2;
		h.relock();
		for(QDictIterator<Path> it(paths); (p=it.current()); ++it)
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
void FXFSMon::Watcher::run()
{
	int ret;
	char buffer[4096];
	for(;;)
	{
		FXERRH_TRY
		{
			if((ret=read(fxfsmon->inotifyh, buffer, sizeof(buffer))))
			{
				FXERRHOS(ret);
				for(inotify_event *ev=(inotify_event *) buffer; ((char *)ev)-buffer<ret; ev+=ev->len+sizeof(inotify_event))
				{
#ifdef DEBUG
					fxmessage("FXFSMonitor: inotify reports wd=%d, mask=%u, cookie=%u, name=%s\n", ev->wd, ev->mask, ev->cookie, ev->name);
#endif
					QMtxHold h(fxfsmon);
					Path *p=pathByHandle.find(ev->wd);
					assert(p);
					if(p)
						p->callHandlers();
				}
			}
		}
		FXERRH_CATCH(FXException &e)
		{
			fxwarning("FXFSMonitor Error: %s\n", e.report().text());
			return;
		}
		FXERRH_ENDTRY
	}
}
#endif
#ifdef USE_KQUEUES
void FXFSMon::Watcher::run()
{
	int ret;
	struct kevent kevs[16];
#if defined(__APPLE__) && defined(_APPLE_C_SOURCE)
	// Register magic thread termination waiter handle with kqueue
	int cancelWaiterHandle=(int)(FXuval) QThread::int_cancelWaiterHandle();
	EV_SET(&cancelWaiter, cancelWaiterHandle, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);
	FXERRHOS(kevent(fxfsmon->kqueueh, &cancelWaiter, 1, NULL, 0, NULL));
#endif
	for(;;)
	{
#if !(defined(__APPLE__) && defined(_APPLE_C_SOURCE))
		QThread::current()->checkForTerminate();
#endif
		FXERRH_TRY
		{	// Have it kick out once a second as it's not a cancellation point on FreeBSD
			struct timespec timeout={1, 0};
			if((ret=kevent(fxfsmon->kqueueh, NULL, 0, kevs, sizeof(kevs)/sizeof(struct kevent),
#if defined(__APPLE__) && defined(_APPLE_C_SOURCE)
				NULL)))
#else
				&timeout)))
#endif
			{
				FXERRHOS(ret);
				for(int n=0; n<ret; n++)
				{
					struct kevent *kev=&kevs[n];
#if defined(__APPLE__) && defined(_APPLE_C_SOURCE)
					if(cancelWaiterHandle==kev->ident)
						QThread::current()->checkForTerminate();
#endif
					QMtxHold h(fxfsmon);
					Path *p=pathByHandle.find(kev->ident);
					assert(p);
					if(p)
						p->callHandlers();
				}
			}
		}
		FXERRH_CATCH(FXException &e)
		{
			fxwarning("FXFSMonitor Error: %s\n", e.report().text());
			return;
		}
		FXERRH_ENDTRY
	}
}
#endif
#ifdef USE_FAM
void FXFSMon::Watcher::run()
{
	int ret;
	FAMEvent fe;
	for(;;)
	{
		FXERRH_TRY
		{	/* Why is every implementation of FAM I've tried using crap?
			This is yet another workaround for thread cancellation hanging the process */
			fd_set fds;
			for(;;)
			{
				FD_ZERO(&fds);
				FD_SET(FAMCONNECTION_GETFD(&fxfsmon->fc), &fds);
				FXERRHOS(tnfxselect(FAMCONNECTION_GETFD(&fxfsmon->fc)+1, &fds, 0, 0, NULL));
				if(!(ret=FAMNextEvent(&fxfsmon->fc, &fe))) break;
				FXERRHFAM(ret);
				if(FAMStartExecuting==fe.code || FAMStopExecuting==fe.code
					|| FAMAcknowledge==fe.code) continue;
				QMtxHold h(fxfsmon);
				Path *p=pathByHandle.find(fe.fr.reqnum);
				assert(p);
				if(p)
					p->callHandlers();
			}
		}
		FXERRH_CATCH(FXException &)
		{
			if(3==FAMErrno)
			{	// libgamin on Fedora Core 3 seems totally broken so exit the thread :(
				fxfsmon->fambroken=true;
				fxwarning("WARNING: FAMNextEvent() returned Connection Failed, assuming broken FAM implementation and exiting thread - FXFSMonitor will no longer work for the remainder of this program's execution\n");
				return;
			}
		}
		FXERRH_ENDTRY
	}
}
#endif
void *FXFSMon::Watcher::cleanup()
{
#if defined(__APPLE__) && defined(_APPLE_C_SOURCE)
	// Deregister magic thread termination waiter handle with kqueue
	cancelWaiter.flags=EV_DELETE;
	FXERRHOS(kevent(fxfsmon->kqueueh, &cancelWaiter, 1, NULL, 0, NULL));
#endif
	return 0;
}

FXFSMon::Watcher::Path::~Path()
{
#ifdef USE_WINAPI
	if(h)
	{
		parent->pathByHandle.remove(h);
		FXERRHWIN(FindCloseChangeNotification(h));
		h=0;
	}
#endif
#ifdef USE_INOTIFY
	if(h)
	{
		FXERRHOS(inotify_rm_watch(fxfsmon->inotifyh, h));
		h=0;
	}
#endif
#ifdef USE_KQUEUES
	h.flags=EV_DELETE;
	kevent(fxfsmon->kqueueh, &h, 1, NULL, 0, NULL);
	parent->pathByHandle.remove(h.ident);
	if(h.ident)
	{
		FXERRHOS(::close(h.ident));
		h.ident=0;
	}
#endif
#ifdef USE_FAM
	if(!fxfsmon->fambroken)
	{
		parent->pathByHandle.remove(h.reqnum);
		FXERRHFAM(FAMCancelMonitor(&fxfsmon->fc, &h));
	}
#endif
}

FXFSMon::Watcher::Path::Handler::~Handler()
{
	QMtxHold h(fxfsmon);
	while(!callvs.isEmpty())
	{
		QThreadPool::CancelledState state;
		while(QThreadPool::WasRunning==(state=FXProcess::threadPool().cancel(callvs.getFirst())));
		callvs.removeFirst();
	}
}

void FXFSMon::Watcher::Path::Handler::invoke(const QValueList<Change> &changes, QThreadPool::handle callv)
{
	//fxmessage("FXFSMonitor dispatch %p\n", callv);
	for(QValueList<Change>::const_iterator it=changes.begin(); it!=changes.end(); ++it)
	{
		const Change &ch=*it;
		const QFileInfo &oldfi=ch.oldfi ? *ch.oldfi : QFileInfo();
		const QFileInfo &newfi=ch.newfi ? *ch.newfi : QFileInfo();
#ifdef DEBUG
		{
			FXString file(oldfi.filePath()), chs;
			if(ch.change.modified) chs.append("modified ");
			if(ch.change.created)  { chs.append("created "); file=newfi.filePath(); }
			if(ch.change.deleted)  chs.append("deleted ");
			if(ch.change.renamed)  chs.append("renamed (to "+newfi.filePath()+") ");
			if(ch.change.attrib)   chs.append("attrib ");
			if(ch.change.security) chs.append("security ");
			fxmessage("FXFSMonitor: File %s had changes: %s at %s\n", file.text(), chs.text(), (ch.newfi ? *ch.newfi : *ch.oldfi).lastModified().asString().text());
		}
#endif
		QMtxHold h(fxfsmon);
		callvs.removeRef(callv);
		h.unlock();
		handler(ch.change, oldfi, newfi);
	}
}


static const QFileInfo *findFIByName(const QFileInfoList *list, const FXString &name)
{
	for(QFileInfoList::const_iterator it=list->begin(); it!=list->end(); ++it)
	{
		const QFileInfo &fi=*it;
		// Need a case sensitive compare
		if(fi.fileName()==name) return &fi;
	}
	return 0;
}
void FXFSMon::Watcher::Path::callHandlers()
{	// Lock is held on entry
	FXAutoPtr<QDir> newpathdir;
	FXERRHM(newpathdir=new QDir(pathdir->path(), "*", QDir::Unsorted, QDir::All|QDir::Hidden));
	newpathdir->entryInfoList();
	QStringList rawchanges=QDir::extractChanges(*pathdir, *newpathdir);
	QValueList<Change> changes;
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
	for(QValueList<Change>::iterator it1=changes.begin(); it1!=changes.end(); !noIncIter1 ? (++it1, 0) : 0)
	{
		noIncIter1=false;
		Change &ch1=*it1;
		if(ch1.oldfi && ch1.newfi) continue;
		if(ch1.change.renamed) continue;
		bool disable=false;
		Change *candidate=0, *solution=0;
		for(QValueList<Change>::iterator it2=changes.begin(); it2!=changes.end(); ++it2)
		{
			if(it1==it2) continue;
			Change &ch2=*it2;
			if(ch2.oldfi && ch2.newfi) continue;
			if(ch2.change.renamed) continue;
			const QFileInfo *a=0, *b=0;
			if(ch1.oldfi && ch2.newfi) { a=ch1.oldfi; b=ch2.newfi; }
			else if(ch1.newfi && ch2.oldfi) { a=ch2.oldfi; b=ch1.newfi; }
			else continue;
#ifdef DEBUG
			fxmessage("FXFSMonitor: Rename candidate %s, %s (%llu==%llu, %llu==%llu, %llu==%llu, %llu==%llu) candidate=%p\n",
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
	for(QValueList<Change>::iterator it=changes.begin(); it!=changes.end(); ++it)
	{
		Change &ch=*it;
		ch.change.eventNo=++eventcounter;
		if(ch.oldfi && ch.newfi) continue;
		if(ch.change.renamed) continue;
		ch.change.created=(!ch.oldfi && ch.newfi);
		ch.change.deleted=(ch.oldfi && !ch.newfi);
	}
	// Remove any which don't have something set
	for(QValueList<Change>::iterator it=changes.begin(); it!=changes.end();)
	{
		Change &ch=*it;
		if(!ch.change)
			it=changes.erase(it);
		else
			++it;
	}
	FXRBOp resetchanges=FXRBObj(*this, &FXFSMon::Watcher::Path::resetChanges, &changes);
	// Dispatch
	Watcher::Path::Handler *handler;
	for(QPtrVectorIterator<Watcher::Path::Handler> it(handlers); (handler=it.current()); ++it)
	{
		typedef Generic::TL::create<void, QValueList<Change>, QThreadPool::handle>::value Spec;
		Generic::BoundFunctor<Spec> *functor;
		// Detach changes per dispatch
		for(QValueList<Change>::iterator it=changes.begin(); it!=changes.end(); ++it)
			it->make_fis();
		QThreadPool::handle callv=FXProcess::threadPool().dispatch((functor=new Generic::BoundFunctor<Spec>(Generic::Functor<Spec>(*handler, &Watcher::Path::Handler::invoke), changes, 0)));
		handler->callvs.append(callv);
		// Poke in the callv
		Generic::TL::instance<1>(functor->parameters()).value=callv;
	}
	pathdir=newpathdir;
}

void FXFSMon::add(const FXString &path, FXFSMonitor::ChangeHandler handler)
{
	QMtxHold lh(fxfsmon);
	Watcher *w;
	for(QPtrListIterator<Watcher> it(watchers); (w=it.current()); ++it)
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
		FXERRHM(w=new Watcher);
		FXRBOp unnew=FXRBNew(w);
		w->start();
		watchers.append(w);
		unnew.dismiss();
	}
	Watcher::Path *p=w->paths.find(path);
	if(!p)
	{
		FXERRHM(p=new Watcher::Path(w, path));
		FXRBOp unnew=FXRBNew(p);
#ifdef USE_WINAPI
		HANDLE h;
		FXERRHWIN(INVALID_HANDLE_VALUE!=(h=FindFirstChangeNotification(FXUnicodify<>(path, true).buffer(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME
			|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_ATTRIBUTES|FILE_NOTIFY_CHANGE_SIZE
			/*|FILE_NOTIFY_CHANGE_LAST_WRITE*/|FILE_NOTIFY_CHANGE_SECURITY)));
		p->h=h;
		FXERRHWIN(SetEvent(w->latch));
		w->pathByHandle.insert(h, p);
#endif
#ifdef USE_INOTIFY
		int h;
		FXERRHOS(h=inotify_add_watch(fxfsmon->inotifyh, path.text(), IN_ALL_EVENTS&~(IN_ACCESS|IN_CLOSE_NOWRITE|IN_OPEN)));
		p->h=h;
		w->pathByHandle.insert(h, p);
#endif
#ifdef USE_KQUEUES
		int h;
		FXERRHOS(h=::open(path.text(),
#ifdef __APPLE__
			O_RDONLY|O_EVTONLY));		// Stop unmounts being prevented by open handle
#else
			O_RDONLY));
#endif
		FXERRHOS(::fcntl(h, F_SETFD, ::fcntl(h, F_GETFD, 0)|FD_CLOEXEC));
		EV_SET(&p->h, h, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_WRITE, 0, 0);
		FXERRHOS(kevent(fxfsmon->kqueueh, &p->h, 1, NULL, 0, NULL));
		w->pathByHandle.insert(h, p);
#endif
#ifdef USE_FAM
		if(!fambroken)
		{
			FXERRHFAM(FAMMonitorDirectory(&fc, path.text(), &p->h, 0));
			w->pathByHandle.insert(p->h.reqnum, p);
		}
#endif
		w->paths.insert(path, p);
		unnew.dismiss();
	}
	Watcher::Path::Handler *h;
	FXERRHM(h=new Watcher::Path::Handler(p, std::move(handler)));
	FXRBOp unh=FXRBNew(h);
	p->handlers.append(h);
	unh.dismiss();
}

bool FXFSMon::remove(const FXString &path, FXFSMonitor::ChangeHandler handler)
{
	QMtxHold hl(fxfsmon);
	Watcher *w;
	for(QPtrListIterator<Watcher> it(watchers); (w=it.current()); ++it)
	{
		Watcher::Path *p=w->paths.find(path);
		if(!p) continue;
		Watcher::Path::Handler *h;
		for(QPtrVectorIterator<Watcher::Path::Handler> it2(p->handlers); (h=it2.current()); ++it2)
		{
			if(h->handler==handler)
			{
				p->handlers.takeByIter(it2);
				hl.unlock();
				FXDELETE(h);
				hl.relock();
				h=0;
				if(p->handlers.isEmpty())
				{
#ifdef USE_WINAPI
					FXERRHWIN(SetEvent(w->latch));
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

void FXFSMonitor::add(const FXString &_path, FXFSMonitor::ChangeHandler handler)
{
	FXString path=FXPath::absolute(_path);
#ifdef USE_FAM
	FXERRH(FXStat::exists(path), QTrans::tr("FXFSMonitor", "Path not found"), FXFSMONITOR_PATHNOTFOUND, 0);
	if(fxfsmon->nofam)
	{	// Try starting it again
		if(FAMOpen(&fxfsmon->fc)<0) return;
		fxfsmon->nofam=false;
	}
#endif
	fxfsmon->add(path, std::move(handler));
}

bool FXFSMonitor::remove(const FXString &path, FXFSMonitor::ChangeHandler handler)
{
#ifdef USE_FAM
	if(fxfsmon->nofam) return false;
#endif
	return fxfsmon->remove(path, std::move(handler));
}

	}// namespace afio
}//namespace boost