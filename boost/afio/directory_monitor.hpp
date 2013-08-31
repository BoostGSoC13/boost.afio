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

#include <boost/filesystem.hpp>


namespace boost{
	namespace afio{
		
		struct timespec
		{
			time_t tv_sec;
			long tv_nsec;
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
		class directory_entry
		{
			friend std::unique_ptr<std::vector<directory_entry>> enumerate_directory(void *h, size_t maxitems, std::filesystem::path glob=std::filesystem::path(), bool namesonly=false);

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
			} stat;
			void _int_fetch(have_metadata_flags wanted, std::filesystem::path prefix=std::filesystem::path());
		public:
			//! Constructs an instance
			directory_entry()
			{
				have_metadata.value=0;
				memset(&stat, 0, sizeof(stat));
			}
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
		};

		class dir_monitor
		{
			typedef unsigned long event_id;
			typedef unsigned int event_flag;
			typedef boost::filesystem::path dir_path;
			struct event_flags
			{
				event_flag modified	: 1;		//!< When an entry is modified
				event_flag created	: 1;		//!< When an entry is created
				event_flag deleted	: 1;		//!< When an entry is deleted
				event_flag renamed	: 1;		//!< When an entry is renamed
				event_flag attrib	: 1;		//!< When the attributes of an entry are changed 
				event_flag security	: 1;		//!< When the security of an entry is changed
			};

			struct dir_event
			{
				event_id id;					//!< Non zero event number index
				event_flags flags;				//!< bit-field of director events
				dir_path path; 					//!< Path to event/file

				dir_event() : eventNo(0), modified(false), created(false), deleted(false), renamed(false), attrib(false), security(false) { }
				dir_event(int) : eventNo(0), modified(true), created(true), deleted(true), renamed(true), attrib(true), security(true) { }
				operator event_flag() const throw()
				{
					return this->flags;
				}
				//! Sets the modified bit
				dir_event &setModified(bool v=true) throw()		{ modified=v; return *this; }
				//! Sets the created bit
				dir_event &setCreated(bool v=true) throw()		{ created=v; return *this; }
				//! Sets the deleted bit
				dir_event &setDeleted(bool v=true) throw()		{ deleted=v; return *this; }
				//! Sets the renamed bit
				dir_event &setRenamed(bool v=true) throw()		{ renamed=v; return *this; }
				//! Sets the attrib bit
				dir_event &setAttrib(bool v=true) throw()		{ attrib=v; return *this; }
				//! Sets the security bit
				dir_event &setSecurity(bool v=true) throw()		{ security=v; return *this; }
			};


			// replace QFileInfo with 
			//! Defines the type of closure change handlers are
			typedef std::function<void(dir_event, directory_entry, directory_entry)> ChangeHandler;


			//! Adds a monitor of a path on the filing system
			static void add(const dir_path &path, ChangeHandler handler);
			//! Removes a monitor of a path. Cancels any pending handler invocations.
			static bool remove(const dir_path &path, ChangeHandler handler);
		};
	}// namespace afio
} // namespace boost


namespace std
{
	template<> struct hash<FastDirectoryEnumerator::directory_entry>
	{
	public:
		size_t operator()(const FastDirectoryEnumerator::directory_entry& p) const
		{
			size_t seed = 0;
			boost::hash_combine(seed, p.leafname);
			boost::hash_combine(seed, p.have_metadata);
			boost::hash_combine(seed, p.stat);
			return seed;
		}
	};
	
	//Probably need to change this dependent on platform right now it should hash safely
	template<> struct hash<FastDirectoryEnumerator::directory_entry::stat_t>
	{
	public:
		size_t operator()(const FastDirectoryEnumerator::directory_entry::stat& s) const
		{
			size_t seed = 0;
			//boost::hash_combine(seed, s.st_dev);
			boost::hash_combine(seed, s.st_ino);
			boost::hash_combine(seed, s.st_type);
			//boost::hash_combine(seed, s.st_mode);
			//boost::hash_combine(seed, s.st_nlink);
			//boost::hash_combine(seed, s.st_uid);
			//boost::hash_combine(seed, s.st_gid);
			//boost::hash_combine(seed, s.st_rdev);
			boost::hash_combine(seed, s.st_atim);
			boost::hash_combine(seed, s.st_mtim);
			boost::hash_combine(seed, s.st_ctim);
			boost::hash_combine(seed, s.st_size);
			boost::hash_combine(seed, s.st_allocated);
			//boost::hash_combine(seed, s.st_blocks);
			//boost::hash_combine(seed, s.st_blksize);
			//boost::hash_combine(seed, s.st_flags);
			//boost::hash_combine(seed, s.st_gen);
			//boost::hash_combine(seed, s.st_lspare);
			boost::hash_combine(seed, s.st_birthtim);
			return seed;
		}
	};



}//namesapce std

#endif