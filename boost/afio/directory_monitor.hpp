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
			struct timespec st_birthtime;      /* time of file creation (birth) */
		} stat;

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
				//dir_path path; 				//!< Path to event/file

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
			typedef std::function<void(dir_event, stat_t, stat_t)> ChangeHandler;


			//! Adds a monitor of a path on the filing system
			static void add(const dir_path &path, ChangeHandler handler);
			//! Removes a monitor of a path. Cancels any pending handler invocations.
			static bool remove(const dir_path &path, ChangeHandler handler);
		};
	}// namespace afio
} // namespace boost
#endif