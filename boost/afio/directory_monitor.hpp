#ifndef BOOST_AFIO_DIRECTORY_MONITOR_HPP
#define BOOST_AFIO_DIRECTORY_MONITOR_HPP

#include "config.hpp"

/*maybe we dont need this crazyness ...
#ifdef BOOST_WINDOWS
#include "win_dir_monitor.hpp"
#elif defined(__linux__)
#include "linux_dir_monitor.hpp"
#elif defined(__apple__) || defined(__freeBSD__)
#include "kqueue_dir_monitor.hpp"
#else
#include "generic_dir_monitor.hpp"
#endif
*/
#include "afio.hpp"
#include "../../../boost/afio/detail/ErrorHandling.hpp"
#include <boost/filesystem.hpp>
#include <boost/functional/hash.hpp>
#include "boost/thread.hpp"		// May undefine USE_WINAPI and USE_POSIX
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/ptr_container/ptr_list.hpp>
#include <vector>
#include <algorithm>
#include <future>

#define USE_POSIX
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

#define NUMBER_OF_FILES 100000
#define BOOST_AFIO_LOCK_GUARD boost::lock_guard

namespace boost{
	namespace afio{
		
		//maybe adding operators for this is a mistake??? but I want to compare so ...
		struct timespec
		{
			time_t tv_sec;
			long tv_nsec;

			bool operator == (const timespec& b) const BOOST_NOEXCEPT_OR_NOTHROW { return ((tv_nsec == b.tv_nsec) && (tv_sec == b.tv_sec)); }
			bool operator != (const timespec& b) const BOOST_NOEXCEPT_OR_NOTHROW { return ((tv_nsec != b.tv_nsec) && (tv_sec != b.tv_sec)); 	}
			bool operator < (const timespec& b) const BOOST_NOEXCEPT_OR_NOTHROW { return ((tv_nsec <  b.tv_nsec) && (tv_sec <  b.tv_sec)); }
			bool operator > ( const timespec& b) const BOOST_NOEXCEPT_OR_NOTHROW { return ((tv_nsec > b.tv_nsec) && (tv_sec > b.tv_sec)); }
			bool operator <= (const timespec& b) const BOOST_NOEXCEPT_OR_NOTHROW { return ((tv_nsec <= b.tv_nsec) && (tv_sec <= b.tv_sec)); }
			bool operator >= (const timespec& b) const BOOST_NOEXCEPT_OR_NOTHROW { return ((tv_nsec >= b.tv_nsec) && (tv_sec >= b.tv_sec)); }
			friend  std::size_t hash_value(timespec const& t)
			{
				size_t seed = 0;
				boost::hash_combine(seed, t.tv_sec);
				boost::hash_combine(seed, t.tv_nsec);
				return seed;
			}

		};
		typedef unsigned long long off_t;

		/*! \brief Bitflags for availability of metadata

		These replicate the `stat` structure. `st_birthtime` is a BSD extension, but
		also is available on Windows.
		*/
		union have_metadata_flags
		{
			struct
			{
				unsigned int have_dev:1, have_ino:1, have_type:1, have_mode:1, have_nlink:1, have_uid:1, have_gid:1,
					have_rdev:1, have_atim:1, have_mtim:1, have_ctim:1, have_size:1,
					have_allocated:1, have_blocks:1, have_blksize:1, have_flags:1, have_gen:1, have_birthtim:1;
			};
			unsigned int value;
		};
		//! An entry in a directory
		class BOOST_AFIO_DECL directory_entry
		{
			friend std::unique_ptr<std::vector<directory_entry>> enumerate_directory(void *h, size_t maxitems, std::filesystem::path glob=std::filesystem::path(), bool namesonly=false);
			//friend size_t std::hash<boost::afio::directory_entry>(const boost::afio::directory_entry& p) const;
public://cheating here for now, need to fix this if it works
			std::filesystem::path leafname;
			have_metadata_flags have_metadata;
			struct stat_t // Derived from BSD
			{
				uint64_t        st_dev;           /* inode of device containing file */
				uint64_t        st_ino;           /* inode of file */
				uint16_t        st_type;          /* type of file */
				uint16_t        st_mode;          /* type and perms of file */
				int16_t         st_nlink;         /* number of hard links */
				int16_t         st_uid;           /* user ID of the file */
				int16_t         st_gid;           /* group ID of the file */
				dev_t           st_rdev;          /* id of file if special */
				struct timespec st_atim;          /* time of last access */
				struct timespec st_mtim;          /* time of last data modification */
				struct timespec st_ctim;          /* time of last status change */
				off_t           st_size;          /* file size, in bytes */
				off_t           st_allocated;     /* bytes allocated for file */
				off_t           st_blocks;        /* number of blocks allocated */
				uint16_t        st_blksize;       /* block size used by this device */
				uint32_t        st_flags;         /* user defined flags for file */
				uint32_t        st_gen;           /* file generation number */
				int32_t         st_lspare;
				struct timespec st_birthtim;      /* time of file creation (birth) */

				friend  std::size_t hash_value(stat_t const& s)
				{
					size_t seed = 0;
					boost::hash_combine(seed, s.st_ino);
					boost::hash_combine(seed, s.st_birthtim);
					return seed;
				}

			} stat;
			void _int_fetch(have_metadata_flags wanted, std::filesystem::path prefix=std::filesystem::path());
		//public:
			//! Constructs an instance
			directory_entry()
			{
				have_metadata.value=0;
				memset(&stat, 0, sizeof(stat));
			}
			
			// copy constructor
			//directory_entry(const directory_entry& other): stat(other.stat), leafname(other.leafname), have_metadata(other.have_metadata) {}

			bool operator==(const directory_entry& rhs) const BOOST_NOEXCEPT_OR_NOTHROW { return leafname == rhs.leafname; }
			bool operator!=(const directory_entry& rhs) const BOOST_NOEXCEPT_OR_NOTHROW { return leafname != rhs.leafname; }
			bool operator< (const directory_entry& rhs) const BOOST_NOEXCEPT_OR_NOTHROW { return leafname < rhs.leafname; }
			bool operator<=(const directory_entry& rhs) const BOOST_NOEXCEPT_OR_NOTHROW { return leafname <= rhs.leafname; }
			bool operator> (const directory_entry& rhs) const BOOST_NOEXCEPT_OR_NOTHROW { return leafname > rhs.leafname; }
			bool operator>=(const directory_entry& rhs) const BOOST_NOEXCEPT_OR_NOTHROW { return leafname >= rhs.leafname; }
			//! The name of the directory entry
			std::filesystem::path name() const BOOST_NOEXCEPT_OR_NOTHROW { return leafname; }
			//! A bitfield of what metadata is ready right now
			have_metadata_flags metadata_ready() const BOOST_NOEXCEPT_OR_NOTHROW { return have_metadata; }
			//! Fetches the specified metadata, returning that newly available. This is a blocking call.
			have_metadata_flags fetch_metadata(std::filesystem::path prefix, have_metadata_flags wanted)
			{
				have_metadata_flags tofetch;
				wanted.value&=metadata_supported().value;
				tofetch.value=wanted.value&~have_metadata.value;
				if(tofetch.value) _int_fetch(tofetch, prefix);
				return have_metadata;
			}
			//! Returns st_dev
			uint64_t st_dev() { if(!have_metadata.have_dev) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_dev=1; _int_fetch(tofetch); } return stat.st_dev; }
			//! Returns st_ino
			uint64_t st_ino() { if(!have_metadata.have_ino) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_ino=1; _int_fetch(tofetch); } return stat.st_ino; }
			//! Returns st_type
			uint16_t st_type() { if(!have_metadata.have_type) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_type=1; _int_fetch(tofetch); } return stat.st_type; }
			//! Returns st_mode
			uint16_t st_mode() { if(!have_metadata.have_mode) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_mode=1; _int_fetch(tofetch); } return stat.st_mode; }
			//! Returns st_nlink
			int16_t st_nlink() { if(!have_metadata.have_nlink) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_nlink=1; _int_fetch(tofetch); } return stat.st_nlink; }
			//! Returns st_uid
			int16_t st_uid() { if(!have_metadata.have_uid) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_uid=1; _int_fetch(tofetch); } return stat.st_uid; }
			//! Returns st_gid
			int16_t st_gid() { if(!have_metadata.have_gid) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_gid=1; _int_fetch(tofetch); } return stat.st_gid; }
			//! Returns st_rdev
			dev_t st_rdev() { if(!have_metadata.have_rdev) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_rdev=1; _int_fetch(tofetch); } return stat.st_rdev; }
			//! Returns st_atim
			struct timespec st_atim() { if(!have_metadata.have_atim) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_atim=1; _int_fetch(tofetch); } return stat.st_atim; }
			//! Returns st_mtim
			struct timespec st_mtim() { if(!have_metadata.have_mtim) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_mtim=1; _int_fetch(tofetch); } return stat.st_mtim; }
			//! Returns st_ctim
			struct timespec st_ctim() { if(!have_metadata.have_ctim) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_ctim=1; _int_fetch(tofetch); } return stat.st_ctim; }
			//! Returns st_size
			off_t st_size() { if(!have_metadata.have_size) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_size=1; _int_fetch(tofetch); } return stat.st_size; }
			//! Returns st_allocated
			off_t st_allocated() { if(!have_metadata.have_allocated) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_allocated=1; _int_fetch(tofetch); } return stat.st_allocated; }
			//! Returns st_blocks
			off_t st_blocks() { if(!have_metadata.have_blocks) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_blocks=1; _int_fetch(tofetch); } return stat.st_blocks; }
			//! Returns st_blksize
			uint16_t st_blksize() { if(!have_metadata.have_blksize) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_blksize=1; _int_fetch(tofetch); } return stat.st_blksize; }
			//! Returns st_flags
			uint32_t st_flags() { if(!have_metadata.have_flags) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_flags=1; _int_fetch(tofetch); } return stat.st_flags; }
			//! Returns st_gen
			uint32_t st_gen() { if(!have_metadata.have_gen) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_gen=1; _int_fetch(tofetch); } return stat.st_gen; }
			//! Returns st_birthtim
			struct timespec st_birthtim() { if(!have_metadata.have_birthtim) { have_metadata_flags tofetch; tofetch.value=0; tofetch.have_birthtim=1; _int_fetch(tofetch); } return stat.st_birthtim; }

			//! A bitfield of what metadata is available on this platform. This doesn't mean all is available for every filing system.
			static have_metadata_flags metadata_supported() BOOST_NOEXCEPT_OR_NOTHROW;
		};// end directory_entry

		class BOOST_AFIO_DECL dir_monitor
		{
			typedef unsigned long event_no;
			typedef unsigned int event_flag;
			typedef boost::filesystem::path dir_path;
			struct event_flags
			{
				event_flag modified	: 1;		//!< When an entry is modified
				event_flag created	: 1;		//!< When an entry is created
				event_flag deleted	: 1;		//!< When an entry is deleted
				event_flag renamed	: 1;		//!< When an entry is renamed
				//event_flag attrib	: 1;		//!< When the attributes of an entry are changed 
				event_flag security	: 1;		//!< When the security of an entry is changed
			};

		public:
			struct dir_event
			{
				event_no eventNo;				//!< Non zero event number index
				event_flags flags;				//!< bit-field of director events
				dir_path path; 					//!< Path to event/file

				dir_event() : eventNo(0) { flags.modified = false; flags.created = false; flags.deleted = false; flags.security = false; }
				dir_event(int) : eventNo(0) { flags.modified = true; flags.created = true; flags.deleted = true; flags.security = true; }
				operator event_flags() const throw()
				{
					return this->flags;
				}
				//! Sets the modified bit
				dir_event &setModified(bool v=true) throw()		{ flags.modified=v; return *this; }
				//! Sets the created bit
				dir_event &setCreated(bool v=true) throw()		{ flags.created=v; return *this; }
				//! Sets the deleted bit
				dir_event &setDeleted(bool v=true) throw()		{ flags.deleted=v; return *this; }
				//! Sets the renamed bit
				dir_event &setRenamed(bool v=true) throw()		{ flags.renamed=v; return *this; }
				//! Sets the attrib bit
				//dir_event &setAttrib(bool v=true) throw()		{ flags.attrib=v; return *this; }
				//! Sets the security bit
				dir_event &setSecurity(bool v=true) throw()		{ flags.security=v; return *this; }
			};//end dir_event


			//! Defines the type of closure change handlers are
			typedef std::function<void(dir_event, directory_entry, directory_entry)> ChangeHandler;


			//! Adds a monitor of a path on the filing system
			static void add(const dir_path &path, ChangeHandler handler);
			//! Removes a monitor of a path. Cancels any pending handler invocations.
			static bool remove(const dir_path &path, ChangeHandler handler);
		};// end dir_monitor

		//! Starts the enumeration of a directory. This actually simply opens the directory and returns the fd or `HANDLE`.
	extern BOOST_AFIO_DECL void *begin_enumerate_directory(std::filesystem::path path);
	//! Ends the enumeration of a directory. This simply closes the fd or `HANDLE`.
	extern BOOST_AFIO_DECL void end_enumerate_directory(void *h);
	/*! \brief Enumerates a directory as quickly as possible, retrieving all zero-cost metadata.

	Note that maxitems items may not be retreived for various reasons, including that glob filtered them out.
	A zero item vector return is entirely possible, but this does not mean end of enumeration: only a null
	shared_ptr means that.

	Windows returns the common stat items, Linux and FreeBSD returns `st_ino` and usually `st_rdev`, other POSIX just `st_ino`.
	Setting namesonly to true returns as little information as possible.

	Suggested code for merging chunks of enumeration into a single vector:
	\code
	void *h=begin_enumerate_directory(_L("testdir"));
	std::unique_ptr<std::vector<directory_entry>> enumeration, chunk;
	while((chunk=enumerate_directory(h, NUMBER_OF_FILES)))
		if(!enumeration)
			enumeration=std::move(chunk);
		else
			enumeration->insert(enumeration->end(), std::make_move_iterator(chunk->begin()), std::make_move_iterator(chunk->end()));
	end_enumerate_directory(h);
	\endcode
	*/
	extern BOOST_AFIO_DECL std::unique_ptr<std::vector<directory_entry>> enumerate_directory(void *h, size_t maxitems, std::filesystem::path glob, bool namesonly);

	}//namespace afio
}// namspace boost




namespace std
{
	template<> struct hash<boost::afio::directory_entry>
	{
	public:
		size_t operator()(const boost::afio::directory_entry& p) const
		{
			size_t seed = 0;
			//boost::hash_combine(seed, p.leafname);
			//boost::hash_combine(seed, p.have_metadata);
			boost::hash_combine(seed, p.stat);
			return seed;
		}
	};
	
	/*//Probably need to change this dependent on platform right now it should hash safely
	template<> struct hash<boost::afio::directory_entry::stat_t>
	{
	public:
		size_t operator()(const boost::afio::directory_entry::stat_t& s) const
		{
			size_t seed = 0;
			//boost::hash_combine(seed, s.st_dev);
			boost::hash_combine(seed, s.st_ino);
			//boost::hash_combine(seed, s.st_type);
			//boost::hash_combine(seed, s.st_mode);
			//boost::hash_combine(seed, s.st_nlink);
			//boost::hash_combine(seed, s.st_uid);
			//boost::hash_combine(seed, s.st_gid);
			//boost::hash_combine(seed, s.st_rdev);
			//boost::hash_combine(seed, s.st_atim);
			//boost::hash_combine(seed, s.st_mtim);
			//boost::hash_combine(seed, s.st_ctim);
			//boost::hash_combine(seed, s.st_size);
			//boost::hash_combine(seed, s.st_allocated);
			//boost::hash_combine(seed, s.st_blocks);
			//boost::hash_combine(seed, s.st_blksize);
			//boost::hash_combine(seed, s.st_flags);
			//boost::hash_combine(seed, s.st_gen);
			//boost::hash_combine(seed, s.st_lspare);
			boost::hash_combine(seed, s.st_birthtim);
			return seed;
		}
	};
	*/

	template<> struct hash<std::filesystem::path>
	{
	public:
		size_t operator()(const std::filesystem::path& p) const
		{
			return boost::filesystem::hash_value(p);
		}
	};
}//namesapce std
#include <unordered_map>


namespace boost{
	namespace afio{
	typedef std::future<void> future_handle;
	typedef boost::afio::dir_monitor dir_monitor;
	typedef dir_monitor::dir_event dir_event;

	struct BOOST_AFIO_DECL monitor : public recursive_mutex
	{
		struct BOOST_AFIO_DECL Watcher //: public thread
		{
			struct BOOST_AFIO_DECL Path
			{
	#ifdef USE_WINAPI
				HANDLE h;
	#endif
	#ifdef USE_INOTIFY
				int h;
	#endif
	#ifdef USE_KQUEUES
				struct kevent h;
	#endif

				struct BOOST_AFIO_DECL Change
				{
					dir_event change;
					std::shared_ptr<directory_entry> oldfi, newfi;
					unsigned int myoldfi : 1;
					unsigned int mynewfi : 1;
					Change( std::shared_ptr<directory_entry>& _oldfi, std::shared_ptr<directory_entry>& _newfi) : oldfi(_oldfi), newfi(_newfi), myoldfi(0), mynewfi(0) { }
					Change( directory_entry* _oldfi, directory_entry* _newfi) : oldfi(_oldfi), newfi(_newfi), myoldfi(0), mynewfi(0) { }
					//Change(const directory_entry&& _oldfi, const directory_entry&& _newfi) : oldfi(_oldfi), newfi(_newfi), myoldfi(0), mynewfi(0) { }
					~Change()
					{
						
					}
					bool operator==(const Change &o) const { return oldfi==o.oldfi && newfi==o.newfi; }
					bool operator!=(const Change &o) const { return oldfi!=o.oldfi && newfi!=o.newfi; }
					void make_fis()
					{
						if(oldfi)
						{
							//oldfi.reset(new directory_entry(*oldfi));
							myoldfi=true;
						}
						if(newfi)
						{
							//newfi.reset(new directory_entry(*newfi));
							mynewfi=true;
						}
					}
					void reset_fis()
					{
						oldfi.reset(); myoldfi=false;
						newfi.reset(); mynewfi=false;
					}
				};

				struct BOOST_AFIO_DECL Handler
				{
					Path *parent;
					dir_monitor::ChangeHandler* handler;
					//std::list<future_handle> callvs;
					Handler(Path *_parent, dir_monitor::ChangeHandler& _handler) : parent(_parent), handler(&_handler) { }
					~Handler();
					void invoke(const std::list<Change> &changes/*, future_handle &callv*/);
				//private:
					Handler(const Handler & other): parent(other.parent), handler(other.handler) {}
					Handler &operator=(const Handler & other) { parent = other.parent; handler = other.handler; return *this; }
				};
				
				Watcher *parent;
				std::shared_ptr<std::vector<directory_entry>> pathdir;
				std::unordered_map<directory_entry, directory_entry> entry_dict;// each entry is also the hash of itself
				boost::ptr_vector<Handler> handlers;
				const std::filesystem::path path;
				Path(Watcher *_parent, const std::filesystem::path &_path)
					: parent(_parent), path(_path), pathdir(nullptr)
	#if defined(USE_WINAPI) || defined(USE_INOTIFY)
					, h(0)
	#endif
				{
	#if defined(USE_KQUEUES)
					memset(&h, 0, sizeof(h));
	#endif
					void *addr = begin_enumerate_directory(path);
					//std::cout << std::filesystem::absolute(path) << std::endl;
					std::unique_ptr<std::vector<directory_entry>> chunk;
					while((chunk = enumerate_directory(addr, NUMBER_OF_FILES)))
						if(!pathdir)
							pathdir=std::move(chunk);
						else
							pathdir->insert(pathdir->end(), std::make_move_iterator(chunk->begin()), std::make_move_iterator(chunk->end()));
					end_enumerate_directory(addr);
					//entry_dict.clear();
					//if(pathdir)
					//std::sort(pathdir->begin(), pathdir->end(), [](directory_entry a, directory_entry b){return a.name() < b.name();});
					
					//BOOST_FOREACH(auto &i, *pathdir)
					for(auto it = pathdir->begin(); it != pathdir->end(); ++it)
					{	
						entry_dict.insert(std::make_pair(*it, *it));
						//std::cout << it->name() << std::endl;
					}

					//std::cout << pathdir->size() << std::endl;
				}
				
				~Path();
				void resetChanges(std::list<Change> *changes)
				{
					for(auto it=changes->begin(); it!=changes->end(); ++it)
						it->reset_fis();
				}

				void callHandlers();
			};

			monitor* parent;
			std::unordered_map< std::filesystem::path, Path> paths;
	#ifdef USE_WINAPI
			HANDLE latch;
			std::unordered_map<std::filesystem::path, Path*> pathByHandle;
	#else
			std::unordered_map<int, Path*> pathByHandle;
	#ifdef USE_KQUEUES
			struct kevent cancelWaiter;
	#endif
	#endif
			Watcher(monitor* _parent);
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
		std::shared_ptr<std_thread_pool> threadpool;
		std::atomic<bool> running;
		bool is_running(){return running.load(); }
		future<void> finished;
		std::shared_ptr<thread> my_thread;
		monitor();
		~monitor();
		void add(const std::filesystem::path &path, dir_monitor::ChangeHandler& handler);
		bool remove(const std::filesystem::path &path, dir_monitor::ChangeHandler& handler);
		void process_watchers();
	};
	//static monitor mon;//do something better, like make a controller class that has this as a member

	}// namespace afio
} // namespace boost


#endif