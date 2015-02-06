/* async_file_io
Provides a threadpool and asynchronous file i/o infrastructure based on Boost.ASIO, Boost.Iostreams and filesystem
(C) 2013-2014 Niall Douglas http://www.nedprod.com/
File Created: Mar 2013
*/

//#define BOOST_AFIO_USE_CONCURRENT_UNORDERED_MAP
//#define BOOST_AFIO_MAX_NON_ASYNC_QUEUE_DEPTH 1

#ifndef BOOST_AFIO_MAX_NON_ASYNC_QUEUE_DEPTH
#define BOOST_AFIO_MAX_NON_ASYNC_QUEUE_DEPTH 8
#endif
//#define USE_POSIX_ON_WIN32 // Useful for testing

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4456) // declaration hides previous local declaration
#pragma warning(disable: 4458) // declaration hides class member
#pragma warning(disable: 4996) // This function or variable may be unsafe
#endif

// This always compiles in input validation for this file only (the header file
// disables at the point of instance validation in release builds)
#if !defined(BOOST_AFIO_NEVER_VALIDATE_INPUTS) && !defined(BOOST_AFIO_COMPILING_FOR_GCOV)
#define BOOST_AFIO_VALIDATE_INPUTS 1
#endif

// Define this to have every allocated op take a backtrace from where it was allocated
//#ifndef BOOST_AFIO_OP_STACKBACKTRACEDEPTH
//#ifndef NDEBUG
//#define BOOST_AFIO_OP_STACKBACKTRACEDEPTH 8
//#endif
//#endif

// Define this to have every part of AFIO print, in extremely terse text, what it is doing and why.
#if (defined(BOOST_AFIO_DEBUG_PRINTING) && BOOST_AFIO_DEBUG_PRINTING)
#ifndef BOOST_AFIO_DEBUG_PRINTING
# define BOOST_AFIO_DEBUG_PRINTING 1
#endif
#ifdef WIN32
#define BOOST_AFIO_DEBUG_PRINT(...) \
    { \
    char buffer[16384]; \
    sprintf(buffer, __VA_ARGS__); \
    fprintf(stderr, buffer); \
    OutputDebugStringA(buffer); \
    }
#else
#define BOOST_AFIO_DEBUG_PRINT(...) \
    { \
    fprintf(stderr, __VA_ARGS__); \
    }
#endif
#else
#define BOOST_AFIO_DEBUG_PRINT(...)
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef BOOST_AFIO_OP_STACKBACKTRACEDEPTH
#include <dlfcn.h>
// Set to 1 to use libunwind instead of glibc's stack backtracer
#if 0
#define UNW_LOCAL_ONLY
#include <libunwind.h>
#else
#ifndef __linux__
#undef BOOST_AFIO_OP_STACKBACKTRACEDEPTH
#endif
#endif
#endif
#if defined(__linux__) && !defined(__ANDROID__)
#include <execinfo.h>
#endif

#include "../../afio.hpp"
#include "../valgrind/memcheck.h"
#include "ErrorHandling.ipp"

#include <limits>
#include <unordered_map>
#ifdef BOOST_AFIO_USE_CONCURRENT_UNORDERED_MAP
BOOST_AFIO_V1_NAMESPACE_BEGIN
  template<class T> using default_engine_unordered_map_spinlock=spinlock<T,
    spins_to_loop<100>::policy,
    spins_to_yield<500>::policy,
    spins_to_sleep::policy>;
  template<class Key,
           class T,
           class Hash=BOOST_SPINLOCK_V1_NAMESPACE::fnv1a_hash<Key>,
           class Pred=std::equal_to<Key>,
           class Alloc=std::allocator<std::pair<const Key, T>>
           > using engine_unordered_map_t=BOOST_SPINLOCK_V1_NAMESPACE::concurrent_unordered_map<Key,
                                                                   T,
                                                                   Hash,
                                                                   Pred,
                                                                   Alloc,
                                                                   default_engine_unordered_map_spinlock
                                                                   >;
  struct null_lock { void lock() { } bool try_lock() { return true; } void unlock() { } };
BOOST_AFIO_V1_NAMESPACE_END
#else
BOOST_AFIO_V1_NAMESPACE_BEGIN
  template<class Key, class T, class Hash=std::hash<Key>, class Pred=std::equal_to<Key>, class Alloc=std::allocator<std::pair<const Key, T>>> using engine_unordered_map_t=std::unordered_map<Key, T, Hash, Pred, Alloc>;
BOOST_AFIO_V1_NAMESPACE_END
#endif

#include <sys/types.h>
#ifdef __MINGW32__
// Workaround bad headers as usual in mingw
typedef __int64 off64_t;
#endif
#include <fcntl.h>
#include <sys/stat.h>
#ifdef WIN32
#ifndef S_IFSOCK
#define S_IFSOCK 0xC000
#endif
#ifndef S_IFBLK
#define S_IFBLK 0x6000
#endif
#ifndef S_IFIFO
#define S_IFIFO 0x1000
#endif
#ifndef S_IFLNK
#define S_IFLNK 0xA000
#endif
#endif
BOOST_AFIO_V1_NAMESPACE_BEGIN
static inline filesystem::file_type to_st_type(uint16_t mode)
{
    switch(mode & S_IFMT)
    {
#ifdef BOOST_AFIO_USE_LEGACY_FILESYSTEM_SEMANTICS
    case S_IFBLK:
        return filesystem::file_type::block_file;
    case S_IFCHR:
        return filesystem::file_type::character_file;
    case S_IFDIR:
        return filesystem::file_type::directory_file;
    case S_IFIFO:
        return filesystem::file_type::fifo_file;
    case S_IFLNK:
        return filesystem::file_type::symlink_file;
    case S_IFREG:
        return filesystem::file_type::regular_file;
    case S_IFSOCK:
        return filesystem::file_type::socket_file;
    default:
        return filesystem::file_type::type_unknown;
#else
    case S_IFBLK:
        return filesystem::file_type::block;
    case S_IFCHR:
        return filesystem::file_type::character;
    case S_IFDIR:
        return filesystem::file_type::directory;
    case S_IFIFO:
        return filesystem::file_type::fifo;
    case S_IFLNK:
        return filesystem::file_type::symlink;
    case S_IFREG:
        return filesystem::file_type::regular;
    case S_IFSOCK:
        return filesystem::file_type::socket;
    default:
        return filesystem::file_type::unknown;
#endif
    }
}
BOOST_AFIO_V1_NAMESPACE_END
#ifdef WIN32
// We also compile the posix compat layer for catching silly compile errors for POSIX
#include <io.h>
#include <direct.h>
#define BOOST_AFIO_POSIX_MKDIR(path, mode) _wmkdir(path)
#define BOOST_AFIO_POSIX_RMDIR _wrmdir
#define BOOST_AFIO_POSIX_STAT_STRUCT struct __stat64
#define BOOST_AFIO_POSIX_STAT _wstat64
#define BOOST_AFIO_POSIX_LSTAT _wstat64
#define BOOST_AFIO_POSIX_FSTAT _fstat64
#define BOOST_AFIO_POSIX_OPEN _wopen
#define BOOST_AFIO_POSIX_CLOSE _close
#define BOOST_AFIO_POSIX_UNLINK _wunlink
#define BOOST_AFIO_POSIX_FSYNC _commit
#define BOOST_AFIO_POSIX_FTRUNCATE winftruncate
#define BOOST_AFIO_POSIX_MMAP(addr, size, prot, flags, fd, offset) (-1)
#define BOOST_AFIO_POSIX_MUNMAP(addr, size) (-1)
#include "nt_kernel_stuff.hpp"
#else
#include <dirent.h>     /* Defines DT_* constants */
#include <sys/syscall.h>
#include <fnmatch.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/mount.h>
#ifdef __linux__
# include <sys/statfs.h>
# include <mntent.h>
#endif
#include <limits.h>
#define BOOST_AFIO_POSIX_MKDIR mkdir
#define BOOST_AFIO_POSIX_RMDIR ::rmdir
#define BOOST_AFIO_POSIX_STAT_STRUCT struct stat 
#define BOOST_AFIO_POSIX_STAT stat
#define BOOST_AFIO_POSIX_LSTAT ::lstat
#define BOOST_AFIO_POSIX_FSTAT ::fstat
#define BOOST_AFIO_POSIX_OPEN open
#define BOOST_AFIO_POSIX_SYMLINK ::symlink
#define BOOST_AFIO_POSIX_CLOSE ::close
#define BOOST_AFIO_POSIX_UNLINK unlink
#define BOOST_AFIO_POSIX_FSYNC fsync
#define BOOST_AFIO_POSIX_FTRUNCATE ftruncate
#define BOOST_AFIO_POSIX_MMAP mmap
#define BOOST_AFIO_POSIX_MUNMAP munmap

BOOST_AFIO_V1_NAMESPACE_BEGIN
static inline chrono::system_clock::time_point to_timepoint(struct timespec ts)
{
    // Need to have this self-adapt to the STL being used
    static BOOST_CONSTEXPR_OR_CONST unsigned long long STL_TICKS_PER_SEC=(unsigned long long) chrono::system_clock::period::den/chrono::system_clock::period::num;
    static BOOST_CONSTEXPR_OR_CONST unsigned long long multiplier=STL_TICKS_PER_SEC>=1000000000ULL ? STL_TICKS_PER_SEC/1000000000ULL : 1;
    static BOOST_CONSTEXPR_OR_CONST unsigned long long divider=STL_TICKS_PER_SEC>=1000000000ULL ? 1 : 1000000000ULL/STL_TICKS_PER_SEC;
    // We make the big assumption that the STL's system_clock is based on the time_t epoch 1st Jan 1970.
    chrono::system_clock::duration duration(ts.tv_sec*STL_TICKS_PER_SEC+ts.tv_nsec*multiplier/divider);
    return chrono::system_clock::time_point(duration);
}
static inline void fill_stat_t(stat_t &stat, BOOST_AFIO_POSIX_STAT_STRUCT s, metadata_flags wanted)
{
    using namespace boost::afio;
    if(!!(wanted&metadata_flags::dev)) { stat.st_dev=s.st_dev; }
    if(!!(wanted&metadata_flags::ino)) { stat.st_ino=s.st_ino; }
    if(!!(wanted&metadata_flags::type)) { stat.st_type=to_st_type(s.st_mode); }
    if(!!(wanted&metadata_flags::perms)) { stat.st_perms=s.st_mode & 0xfff; }
    if(!!(wanted&metadata_flags::nlink)) { stat.st_nlink=s.st_nlink; }
    if(!!(wanted&metadata_flags::uid)) { stat.st_uid=s.st_uid; }
    if(!!(wanted&metadata_flags::gid)) { stat.st_gid=s.st_gid; }
    if(!!(wanted&metadata_flags::rdev)) { stat.st_rdev=s.st_rdev; }
#ifdef __ANDROID__
    if(!!(wanted&metadata_flags::atim)) { stat.st_atim=to_timepoint(*((struct timespec *)&s.st_atime)); }
    if(!!(wanted&metadata_flags::mtim)) { stat.st_mtim=to_timepoint(*((struct timespec *)&s.st_mtime)); }
    if(!!(wanted&metadata_flags::ctim)) { stat.st_ctim=to_timepoint(*((struct timespec *)&s.st_ctime)); }
#else
    if(!!(wanted&metadata_flags::atim)) { stat.st_atim=to_timepoint(s.st_atim); }
    if(!!(wanted&metadata_flags::mtim)) { stat.st_mtim=to_timepoint(s.st_mtim); }
    if(!!(wanted&metadata_flags::ctim)) { stat.st_ctim=to_timepoint(s.st_ctim); }
#endif
    if(!!(wanted&metadata_flags::size)) { stat.st_size=s.st_size; }
    if(!!(wanted&metadata_flags::allocated)) { stat.st_allocated=(off_t) s.st_blocks*512; }
    if(!!(wanted&metadata_flags::blocks)) { stat.st_blocks=s.st_blocks; }
    if(!!(wanted&metadata_flags::blksize)) { stat.st_blksize=s.st_blksize; }
#ifdef HAVE_STAT_FLAGS
    if(!!(wanted&metadata_flags::flags)) { stat.st_flags=s.st_flags; }
#endif
#ifdef HAVE_STAT_GEN
    if(!!(wanted&metadata_flags::gen)) { stat.st_gen=s.st_gen; }
#endif
#ifdef HAVE_BIRTHTIMESPEC
    if(!!(wanted&metadata_flags::birthtim)) { stat.st_birthtim=to_timepoint(s.st_birthtim); }
#endif
    if(!!(wanted&metadata_flags::sparse)) { stat.st_sparse=((off_t) s.st_blocks*512)<(off_t) s.st_size; }
}
BOOST_AFIO_V1_NAMESPACE_END
#endif

#ifdef WIN32
#ifndef IOV_MAX
#define IOV_MAX 1024
#endif
BOOST_AFIO_V1_NAMESPACE_BEGIN
struct iovec {
    void  *iov_base;    /* Starting address */
    size_t iov_len;     /* Number of bytes to transfer */
};
typedef ptrdiff_t ssize_t;
static spinlock<bool> preadwritelock;
inline ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
    off_t at=offset;
    ssize_t transferred;
    lock_guard<decltype(preadwritelock)> lockh(preadwritelock);
    if(-1==_lseeki64(fd, offset, SEEK_SET)) return -1;
    for(; iovcnt; iov++, iovcnt--, at+=(off_t) transferred)
        if(-1==(transferred=_read(fd, iov->iov_base, (unsigned) iov->iov_len))) return -1;
    return (ssize_t)(at-offset);
}
inline ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
    off_t at=offset;
    ssize_t transferred;
    lock_guard<decltype(preadwritelock)> lockh(preadwritelock);
    if(-1==_lseeki64(fd, offset, SEEK_SET)) return -1;
    for(; iovcnt; iov++, iovcnt--, at+=(off_t) transferred)
        if(-1==(transferred=_write(fd, iov->iov_base, (unsigned) iov->iov_len))) return -1;
    return (ssize_t)(at-offset);
}
BOOST_AFIO_V1_NAMESPACE_END

#elif !defined(IOV_MAX)  // Android lacks preadv() and pwritev()
#define IOV_MAX 1024
BOOST_AFIO_V1_NAMESPACE_BEGIN
struct iovec {
    void  *iov_base;    /* Starting address */
    size_t iov_len;     /* Number of bytes to transfer */
};
typedef ptrdiff_t ssize_t;
inline ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
    off_t at=offset;
    ssize_t transferred;
    for(; iovcnt; iov++, iovcnt--, at+=(off_t) transferred)
        if(-1==(transferred=pread(fd, iov->iov_base, (unsigned) iov->iov_len, at))) return -1;
    return (ssize_t)(at-offset);
}
inline ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
    off_t at=offset;
    ssize_t transferred;
    for(; iovcnt; iov++, iovcnt--, at+=(off_t) transferred)
        if(-1==(transferred=pwrite(fd, iov->iov_base, (unsigned) iov->iov_len, at))) return -1;
    return (ssize_t)(at-offset);
}
BOOST_AFIO_V1_NAMESPACE_END

#endif


BOOST_AFIO_V1_NAMESPACE_BEGIN

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC size_t async_file_io_dispatcher_base::page_size() BOOST_NOEXCEPT_OR_NOTHROW
{
    static size_t pagesize;
    if(!pagesize)
    {
#ifdef WIN32
        SYSTEM_INFO si={ 0 };
        GetSystemInfo(&si);
        pagesize=si.dwPageSize;
#else
        pagesize=getpagesize();
#endif
    }
    return pagesize;
}

std::shared_ptr<std_thread_pool> process_threadpool()
{
    // This is basically how many file i/o operations can occur at once
    // Obviously the kernel also has a limit
    static std::weak_ptr<std_thread_pool> shared;
    static spinlock<bool> lock;
    std::shared_ptr<std_thread_pool> ret(shared.lock());
    if(!ret)
    {
        lock_guard<decltype(lock)> lockh(lock);
        ret=shared.lock();
        if(!ret)
        {
            shared=ret=std::make_shared<std_thread_pool>(BOOST_AFIO_MAX_NON_ASYNC_QUEUE_DEPTH);
        }
    }
    return ret;
}

#ifndef BOOST_AFIO_COMPILING_FOR_GCOV
// Experimental file region locking
namespace detail {
  struct process_lockfile_registry;
  struct actual_lock_file;
  template<class T> struct lock_file;
  typedef spinlock<bool> process_lockfile_registry_lock_t;
  static process_lockfile_registry_lock_t process_lockfile_registry_lock;
  static std::unique_ptr<process_lockfile_registry> process_lockfile_registry_ptr;

  struct process_lockfile_registry
  {
    std::unordered_map<async_io_handle *, lock_file<actual_lock_file> *> handle_to_lockfile;
    std::unordered_map<filesystem::path, std::weak_ptr<actual_lock_file>, filesystem_hash> path_to_lockfile;
    template<class T> static std::unique_ptr<T> open(async_io_handle *h)
    {
      lock_guard<process_lockfile_registry_lock_t> g(process_lockfile_registry_lock);
      if(!process_lockfile_registry_ptr)
        process_lockfile_registry_ptr=make_unique<process_lockfile_registry>();
      auto p=detail::make_unique<T>(h);
      process_lockfile_registry_ptr->handle_to_lockfile.insert(std::make_pair(h, (lock_file<actual_lock_file> *) p.get()));
      return p;
    }
  };
  struct actual_lock_file : std::enable_shared_from_this<actual_lock_file>
  {
    filesystem::path path, lockfilepath;
  protected:
    actual_lock_file(filesystem::path p) : path(p), lockfilepath(p)
    {
      lockfilepath+=".afiolockfile";
    }
  public:
    ~actual_lock_file()
    {
      lock_guard<process_lockfile_registry_lock_t> g(process_lockfile_registry_lock);
      process_lockfile_registry_ptr->path_to_lockfile.erase(path);
    }
    virtual async_file_io_dispatcher_base::completion_returntype lock(size_t id, async_io_op op, async_lock_op_req req, void *)=0;
  };
  struct posix_actual_lock_file : public actual_lock_file
  {
    int h;
#ifndef WIN32
    std::vector<struct flock> local_locks;
#endif
    std::vector<async_lock_op_req> locks;
    posix_actual_lock_file(filesystem::path p) : actual_lock_file(std::move(p)), h(0)
    {
#ifndef WIN32
      bool done=false;
      do
      {
        struct stat before, after;
        BOOST_AFIO_ERRHOS(lstat(path.c_str(), &before));
        BOOST_AFIO_ERRHOS(h=open(lockfilepath.c_str(), O_CREAT|O_RDWR, before.st_mode));
        // Place a read lock on the final byte as a way of detecting when this lock file no longer in use
        struct flock l;
        l.l_type=F_RDLCK;
        l.l_whence=SEEK_SET;
        l.l_start=std::numeric_limits<decltype(l.l_start)>::max()-1;
        l.l_len=1;
        int retcode;
        while(-1==(retcode=fcntl(h, F_SETLKW, &l)) && EINTR==errno);
        // Between the time of opening the lock file and indicating we are using it did someone else delete it
        // or even delete and recreate it?
        if(-1!=fstat(h, &before) && -1!=lstat(lockfilepath.c_str(), &after) && before.st_ino==after.st_ino)
          done=true;
        else
          close(h);
      } while(!done);
#endif
    }
    ~posix_actual_lock_file()
    {
#ifndef WIN32
      // Place a write lock on the final byte as a way of detecting when this lock file no longer in use
      struct flock l;
      l.l_type=F_WRLCK;
      l.l_whence=SEEK_SET;
      l.l_start=std::numeric_limits<decltype(l.l_start)>::max()-1;
      l.l_len=1;
      int retcode;
      while(-1==(retcode=fcntl(h, F_SETLK, &l)) && EINTR==errno);
      if(-1!=retcode)
        BOOST_AFIO_ERRHOS(unlink(lockfilepath.c_str()));
      BOOST_AFIO_ERRHOS(close(h));
      lock_guard<process_lockfile_registry_lock_t> g(process_lockfile_registry_lock);
      process_lockfile_registry_ptr->path_to_lockfile.erase(path);
#endif
    }
    virtual async_file_io_dispatcher_base::completion_returntype lock(size_t id, async_io_op op, async_lock_op_req req, void *) override final
    {
#ifndef WIN32
      struct flock l;
      switch(req.type)
      {
        case async_lock_op_req::Type::read_lock:
          l.l_type=F_RDLCK;
          break;
        case async_lock_op_req::Type::write_lock:
          l.l_type=F_WRLCK;
          break;
        case async_lock_op_req::Type::unlock:
          l.l_type=F_UNLCK;
          break;
        default:
          BOOST_AFIO_THROW(std::invalid_argument("Lock type cannot be unknown"));
      }
      l.l_whence=SEEK_SET;
      // We use the last byte for deleting the lock file on last use, so clamp to second last byte
      if(req.offset==(off_t)(std::numeric_limits<decltype(l.l_start)>::max()-1))
        BOOST_AFIO_THROW(std::invalid_argument("offset cannot be (1<<62)-1"));
      l.l_start=(::off_t) req.offset;
      ::off_t end=(::off_t) std::min(req.offset+req.length, (off_t)(std::numeric_limits<decltype(l.l_len)>::max()-1));
      l.l_len=end-l.l_start;
      // TODO FIXME: Run through local_locks with some async algorithm before dropping onto fcntl().
      int retcode;
      while(-1==(retcode=fcntl(h, F_SETLKW, &l)) && EINTR==errno);
      BOOST_AFIO_ERRHOS(retcode);
#endif
      return std::make_pair(true, op.get());
    }
  };
  template<class T> struct lock_file
  {
    friend struct process_lockfile_registry;
    async_io_handle *h;
    std::vector<async_lock_op_req> locks;
    std::shared_ptr<actual_lock_file> actual;
    lock_file(async_io_handle *_h=nullptr) : h(_h)
    {
      if(h)
      {
        auto mypath=h->path();
        auto it=process_lockfile_registry_ptr->path_to_lockfile.find(mypath);
        if(process_lockfile_registry_ptr->path_to_lockfile.end()!=it)
          actual=it->second.lock();
        if(!actual)
        {
          if(process_lockfile_registry_ptr->path_to_lockfile.end()!=it)
            process_lockfile_registry_ptr->path_to_lockfile.erase(it);
          actual=std::make_shared<T>(mypath);
          process_lockfile_registry_ptr->path_to_lockfile.insert(std::make_pair(mypath, actual));
        }
      }
    }
    async_file_io_dispatcher_base::completion_returntype lock(size_t id, async_io_op op, async_lock_op_req req)
    {
      return actual->lock(id, std::move(op), std::move(req), this);
    }
    ~lock_file()
    {
      lock_guard<process_lockfile_registry_lock_t> g(process_lockfile_registry_lock);
      process_lockfile_registry_ptr->handle_to_lockfile.erase(h);
    }
  };
  typedef lock_file<posix_actual_lock_file> posix_lock_file;
}
#endif


namespace detail {
    BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC void print_fatal_exception_message_to_stderr(const char *msg)
    {
        std::cerr << "FATAL EXCEPTION: " << msg << std::endl;
#if !defined(BOOST_AFIO_COMPILING_FOR_GCOV) && defined(__linux__) && !defined(__ANDROID__)
        void *array[20];
        size_t size=backtrace(array, 20);
        char **strings=backtrace_symbols(array, size);
        for(size_t i=0; i<size; i++)
            std::cerr << "   " << strings[i] << std::endl;
        free(strings);
#endif
    }
    
    struct async_io_handle_posix : public async_io_handle
    {
        int fd;  // -999 is closed handle
        bool has_been_added, DeleteOnClose, SyncOnClose, has_ever_been_fsynced;
        void *mapaddr; size_t mapsize;
#ifndef BOOST_AFIO_COMPILING_FOR_GCOV
        std::unique_ptr<posix_lock_file> lockfile;
#endif

        async_io_handle_posix(async_file_io_dispatcher_base *_parent, std::shared_ptr<async_io_handle> _dirh, const filesystem::path &path, file_flags flags, bool _DeleteOnClose, bool _SyncOnClose, int _fd) : async_io_handle(_parent, std::move(_dirh), path, flags), fd(_fd), has_been_added(false), DeleteOnClose(_DeleteOnClose), SyncOnClose(_SyncOnClose), has_ever_been_fsynced(false), mapaddr(nullptr), mapsize(0)
        {
            if(fd!=-999)
                BOOST_AFIO_ERRHOSFN(fd, path);
#ifndef BOOST_AFIO_COMPILING_FOR_GCOV
            if(!!(flags & file_flags::OSLockable))
                lockfile=process_lockfile_registry::open<posix_lock_file>(this);
#endif
        }
        void int_close()
        {
            BOOST_AFIO_DEBUG_PRINT("D %p\n", this);
            if(mapaddr)
            {
                BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_MUNMAP(mapaddr, mapsize), path());
                mapaddr=nullptr;
            }
            int _fd=fd;
            if(fd>=0)
            {
                if(SyncOnClose && write_count_since_fsync())
                    BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_FSYNC(fd), path());
                BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_CLOSE(fd), path());
                fd=-1;
                if(DeleteOnClose)
                  BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_UNLINK(path().c_str()), path());
            }
            // Deregister AFTER close of file handle
            if(has_been_added)
            {
                parent()->int_del_io_handle((void *) (size_t) _fd);
                has_been_added=false;
            }
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC void close() override final
        {
            int_close();
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC void *native_handle() const override final { return (void *)(size_t)fd; }

        // You can't use shared_from_this() in a constructor so ...
        void do_add_io_handle_to_parent()
        {
            if(fd!=-999)
            {
                parent()->int_add_io_handle((void *) (size_t) fd, shared_from_this());
                has_been_added=true;
            }
        }
        ~async_io_handle_posix()
        {
            int_close();
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC directory_entry direntry(metadata_flags wanted) const override final
        {
            stat_t stat(nullptr);
            BOOST_AFIO_POSIX_STAT_STRUCT s={0};
            if(opened_as_symlink())
            {
              BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_LSTAT(path().c_str(), &s), path());
              fill_stat_t(stat, s, wanted);
              return directory_entry(path()
#ifdef BOOST_AFIO_USE_LEGACY_FILESYSTEM_SEMANTICS
                .leaf()
#else
                .filename()
#endif
              , stat, wanted);
            }
            else
            {
              BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_FSTAT(fd, &s), path());
              fill_stat_t(stat, s, wanted);
              return directory_entry((-1==BOOST_AFIO_POSIX_LSTAT(path().c_str(), &s)) ? filesystem::path() : path()
#ifdef BOOST_AFIO_USE_LEGACY_FILESYSTEM_SEMANTICS
                .leaf()
#else
                .filename()
#endif
              , stat, wanted);
            }
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC filesystem::path target() const override final
        {
#ifdef WIN32
            return filesystem::path();
#else
            if(!opened_as_symlink())
                return filesystem::path();
            char buffer[PATH_MAX+1];
            ssize_t len;
            if((len = readlink(path().c_str(), buffer, sizeof(buffer)-1)) == -1)
                BOOST_AFIO_ERRGOS(-1);
            return filesystem::path::string_type(buffer, len);
#endif
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC void *try_mapfile() override final
        {
#ifndef WIN32
            if(!mapaddr)
            {
                if(!(flags() & file_flags::Write) && !(flags() & file_flags::Append))
                {
                    BOOST_AFIO_POSIX_STAT_STRUCT s={0};
                    if(-1!=fstat(fd, &s))
                    {
                        if((mapaddr=BOOST_AFIO_POSIX_MMAP(nullptr, s.st_size, PROT_READ, MAP_SHARED, fd, 0)))
                            mapsize=s.st_size;
                    }
                }
            }
#endif
            return mapaddr;
        }
    };

    struct async_file_io_dispatcher_op
    {
        OpType optype;
        async_op_flags flags;
        enqueued_task<std::shared_ptr<async_io_handle>()> enqueuement;
        typedef std::pair<size_t, std::shared_ptr<detail::async_file_io_dispatcher_op>> completion_t;
        std::vector<completion_t> completions;
        const shared_future<std::shared_ptr<async_io_handle>> &h() const { return enqueuement.get_future(); }
#ifdef BOOST_AFIO_OP_STACKBACKTRACEDEPTH
        std::vector<void *> stack;
        void fillStack()
        {
#ifdef UNW_LOCAL_ONLY
            // libunwind seems a bit more accurate than glibc's backtrace()
            unw_context_t uc;
            unw_cursor_t cursor;
            unw_getcontext(&uc);
            stack.reserve(BOOST_AFIO_OP_STACKBACKTRACEDEPTH);
            unw_init_local(&cursor, &uc);
            size_t n=0;
            while(unw_step(&cursor)>0 && n++<BOOST_AFIO_OP_STACKBACKTRACEDEPTH)
            {
                unw_word_t ip;
                unw_get_reg(&cursor, UNW_REG_IP, &ip);
                stack.push_back((void *) ip);
            }
#else
            stack.reserve(BOOST_AFIO_OP_STACKBACKTRACEDEPTH);
            stack.resize(BOOST_AFIO_OP_STACKBACKTRACEDEPTH);
            stack.resize(backtrace(&stack.front(), stack.size()));
#endif
        }
#else
        void fillStack() { }
#endif
        async_file_io_dispatcher_op(OpType _optype, async_op_flags _flags)
            : optype(_optype), flags(_flags)
        {
            // Stop the future from being auto-set on task return
            enqueuement.disable_auto_set_future();
            //completions.reserve(4); // stop needless storage doubling for small numbers
            fillStack();
        }
        async_file_io_dispatcher_op(async_file_io_dispatcher_op &&o) BOOST_NOEXCEPT_OR_NOTHROW : optype(o.optype), flags(std::move(o.flags)),
            enqueuement(std::move(o.enqueuement)), completions(std::move(o.completions))
#ifdef BOOST_AFIO_OP_STACKBACKTRACEDEPTH
            , stack(std::move(o.stack))
#endif
            {
            }
    private:
        async_file_io_dispatcher_op(const async_file_io_dispatcher_op &o) = delete;
    };
    struct async_file_io_dispatcher_base_p
    {
        std::shared_ptr<thread_source> pool;
        file_flags flagsforce, flagsmask;
        std::vector<std::pair<detail::OpType, std::function<async_file_io_dispatcher_base::filter_t>>> filters;
        std::vector<std::pair<detail::OpType, std::function<async_file_io_dispatcher_base::filter_readwrite_t>>> filters_buffers;

#ifdef BOOST_AFIO_USE_CONCURRENT_UNORDERED_MAP
        typedef null_lock fdslock_t;
        typedef null_lock opslock_t;
#else
        typedef spinlock<size_t> fdslock_t;
        typedef spinlock<size_t,
            spins_to_loop<100>::policy,
            spins_to_yield<500>::policy,
            spins_to_sleep::policy
        > opslock_t;
#endif
        typedef recursive_mutex dircachelock_t;
        fdslock_t fdslock; engine_unordered_map_t<void *, std::weak_ptr<async_io_handle>> fds;
        opslock_t opslock; atomic<size_t> monotoniccount; engine_unordered_map_t<size_t, std::shared_ptr<async_file_io_dispatcher_op>> ops;
        dircachelock_t dircachelock; std::unordered_map<filesystem::path, std::weak_ptr<async_io_handle>, filesystem_hash> dirhcache;

        async_file_io_dispatcher_base_p(std::shared_ptr<thread_source> _pool, file_flags _flagsforce, file_flags _flagsmask) : pool(_pool),
            flagsforce(_flagsforce), flagsmask(_flagsmask), monotoniccount(0)
        {
#ifdef BOOST_AFIO_USE_CONCURRENT_UNORDERED_MAP
            // concurrent_unordered_map doesn't lock, so we actually don't need many buckets for max performance
            fds.min_bucket_capacity(2);
            fds.reserve(499);
            ops.min_bucket_capacity(2);
            ops.reserve(499);
#else
            // unordered_map needs to release the lock as quickly as possible
            ops.reserve(10000);
#endif
        }
        ~async_file_io_dispatcher_base_p()
        {
        }

        // Returns a handle to a directory from the cache, or creates a new directory handle.
        template<class F> std::shared_ptr<async_io_handle> get_handle_to_dir(F *parent, size_t id, async_path_op_req req, typename async_file_io_dispatcher_base::completion_returntype(F::*dofile)(size_t, async_io_op, async_path_op_req))
        {
            std::shared_ptr<async_io_handle> dirh;
            lock_guard<dircachelock_t> dircachelockh(dircachelock);
            do
            {
                std::unordered_map<filesystem::path, std::weak_ptr<async_io_handle>, filesystem_hash>::iterator it=dirhcache.find(req.path);
                if(dirhcache.end()==it || it->second.expired())
                {
                    if(dirhcache.end()!=it) dirhcache.erase(it);
                    auto result=(parent->*dofile)(id, async_io_op(), req);
                    if(!result.first) abort();
                    dirh=std::move(result.second);
                    if(dirh)
                    {
                        auto _it=dirhcache.insert(std::make_pair(req.path, std::weak_ptr<async_io_handle>(dirh)));
                        return dirh;
                    }
                    else
                        abort();
                }
                else
                    dirh=std::shared_ptr<async_io_handle>(it->second);
            } while(!dirh);
            return dirh;
        }
        // Returns a handle to a containing directory from the cache, or creates a new directory handle.
        template<class F> std::shared_ptr<async_io_handle> get_handle_to_containing_dir(F *parent, size_t id, async_path_op_req req, typename async_file_io_dispatcher_base::completion_returntype(F::*dofile)(size_t, async_io_op, async_path_op_req))
        {
            req.path=req.path.parent_path();
            req.flags=req.flags&~file_flags::FastDirectoryEnumeration;
            return get_handle_to_dir(parent, id, req, dofile);
        }
    };
    class async_file_io_dispatcher_compat;
    class async_file_io_dispatcher_windows;
    class async_file_io_dispatcher_linux;
    class async_file_io_dispatcher_qnx;
    struct immediate_async_ops
    {
        typedef std::shared_ptr<async_io_handle> rettype;
        typedef rettype retfuncttype();
        size_t reservation;
        std::vector<enqueued_task<retfuncttype>> toexecute;

        immediate_async_ops(size_t reserve) : reservation(reserve) { }
        // Returns a promise which is fulfilled when this is destructed
        void enqueue(enqueued_task<retfuncttype> task)
        {
          if(toexecute.empty())
            toexecute.reserve(reservation);
          toexecute.push_back(task);
        }
        ~immediate_async_ops()
        {
            for(auto &i: toexecute)
            {
                i();
            }
        }
    private:
        immediate_async_ops(const immediate_async_ops &);
        immediate_async_ops &operator=(const immediate_async_ops &);
        immediate_async_ops(immediate_async_ops &&);
        immediate_async_ops &operator=(immediate_async_ops &&);
    };
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC metadata_flags directory_entry::metadata_supported() BOOST_NOEXCEPT_OR_NOTHROW
{
    metadata_flags ret;
#ifdef WIN32
    ret=metadata_flags::None
        //| metadata_flags::dev
        | metadata_flags::ino        // FILE_INTERNAL_INFORMATION, enumerated
        | metadata_flags::type       // FILE_BASIC_INFORMATION, enumerated
        //| metadata_flags::perms
        | metadata_flags::nlink      // FILE_STANDARD_INFORMATION
        //| metadata_flags::uid
        //| metadata_flags::gid
        //| metadata_flags::rdev
        | metadata_flags::atim       // FILE_BASIC_INFORMATION, enumerated
        | metadata_flags::mtim       // FILE_BASIC_INFORMATION, enumerated
        | metadata_flags::ctim       // FILE_BASIC_INFORMATION, enumerated
        | metadata_flags::size       // FILE_STANDARD_INFORMATION, enumerated
        | metadata_flags::allocated  // FILE_STANDARD_INFORMATION, enumerated
        | metadata_flags::blocks
        | metadata_flags::blksize    // FILE_ALIGNMENT_INFORMATION
        //| metadata_flags::flags
        //| metadata_flags::gen
        | metadata_flags::birthtim   // FILE_BASIC_INFORMATION, enumerated
        | metadata_flags::sparse     // FILE_BASIC_INFORMATION, enumerated
        | metadata_flags::compressed // FILE_BASIC_INFORMATION, enumerated
        ;
#elif defined(__linux__)
    ret=metadata_flags::None
        | metadata_flags::dev
        | metadata_flags::ino
        | metadata_flags::type
        | metadata_flags::perms
        | metadata_flags::nlink
        | metadata_flags::uid
        | metadata_flags::gid
        | metadata_flags::rdev
        | metadata_flags::atim
        | metadata_flags::mtim
        | metadata_flags::ctim
        | metadata_flags::size
        | metadata_flags::allocated
        | metadata_flags::blocks
        | metadata_flags::blksize
    // Sadly these must wait until someone fixes the Linux stat() call e.g. the xstat() proposal.
        //| metadata_flags::flags
        //| metadata_flags::gen
        //| metadata_flags::birthtim
    // According to http://computer-forensics.sans.org/blog/2011/03/14/digital-forensics-understanding-ext4-part-2-timestamps
    // ext4 keeps birth time at offset 144 to 151 in the inode. If we ever got round to it, birthtime could be hacked.
        | metadata_flags::sparse
        //| metadata_flags::compressed
        ;
#else
    // Kinda assumes FreeBSD or OS X really ...
    ret=metadata_flags::None
        | metadata_flags::dev
        | metadata_flags::ino
        | metadata_flags::type
        | metadata_flags::perms
        | metadata_flags::nlink
        | metadata_flags::uid
        | metadata_flags::gid
        | metadata_flags::rdev
        | metadata_flags::atim
        | metadata_flags::mtim
        | metadata_flags::ctim
        | metadata_flags::size
        | metadata_flags::allocated
        | metadata_flags::blocks
        | metadata_flags::blksize
#define HAVE_STAT_FLAGS
        | metadata_flags::flags
#define HAVE_STAT_GEN
        | metadata_flags::gen
#define HAVE_BIRTHTIMESPEC
        | metadata_flags::birthtim
        | metadata_flags::sparse
        //| metadata_flags::compressed
        ;
#endif
    return ret;
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC metadata_flags directory_entry::metadata_fastpath() BOOST_NOEXCEPT_OR_NOTHROW
{
    metadata_flags ret;
#ifdef WIN32
    ret=metadata_flags::None
        //| metadata_flags::dev
        | metadata_flags::ino        // FILE_INTERNAL_INFORMATION, enumerated
        | metadata_flags::type       // FILE_BASIC_INFORMATION, enumerated
        //| metadata_flags::perms
        //| metadata_flags::nlink      // FILE_STANDARD_INFORMATION
        //| metadata_flags::uid
        //| metadata_flags::gid
        //| metadata_flags::rdev
        | metadata_flags::atim       // FILE_BASIC_INFORMATION, enumerated
        | metadata_flags::mtim       // FILE_BASIC_INFORMATION, enumerated
        | metadata_flags::ctim       // FILE_BASIC_INFORMATION, enumerated
        | metadata_flags::size       // FILE_STANDARD_INFORMATION, enumerated
        | metadata_flags::allocated  // FILE_STANDARD_INFORMATION, enumerated
        //| metadata_flags::blocks
        //| metadata_flags::blksize    // FILE_ALIGNMENT_INFORMATION
        //| metadata_flags::flags
        //| metadata_flags::gen
        | metadata_flags::birthtim   // FILE_BASIC_INFORMATION, enumerated
        | metadata_flags::sparse     // FILE_BASIC_INFORMATION, enumerated
        | metadata_flags::compressed // FILE_BASIC_INFORMATION, enumerated
        ;
#elif defined(__linux__)
    ret=metadata_flags::None
        | metadata_flags::dev
        | metadata_flags::ino
        | metadata_flags::type
        | metadata_flags::perms
        | metadata_flags::nlink
        | metadata_flags::uid
        | metadata_flags::gid
        | metadata_flags::rdev
        | metadata_flags::atim
        | metadata_flags::mtim
        | metadata_flags::ctim
        | metadata_flags::size
        | metadata_flags::allocated
        | metadata_flags::blocks
        | metadata_flags::blksize
    // Sadly these must wait until someone fixes the Linux stat() call e.g. the xstat() proposal.
        //| metadata_flags::flags
        //| metadata_flags::gen
        //| metadata_flags::birthtim
    // According to http://computer-forensics.sans.org/blog/2011/03/14/digital-forensics-understanding-ext4-part-2-timestamps
    // ext4 keeps birth time at offset 144 to 151 in the inode. If we ever got round to it, birthtime could be hacked.
        | metadata_flags::sparse
        //| metadata_flags::compressed
        ;
#else
    // Kinda assumes FreeBSD or OS X really ...
    ret=metadata_flags::None
        | metadata_flags::dev
        | metadata_flags::ino
        | metadata_flags::type
        | metadata_flags::perms
        | metadata_flags::nlink
        | metadata_flags::uid
        | metadata_flags::gid
        | metadata_flags::rdev
        | metadata_flags::atim
        | metadata_flags::mtim
        | metadata_flags::ctim
        | metadata_flags::size
        | metadata_flags::allocated
        | metadata_flags::blocks
        | metadata_flags::blksize
#define HAVE_STAT_FLAGS
        | metadata_flags::flags
#define HAVE_STAT_GEN
        | metadata_flags::gen
#define HAVE_BIRTHTIMESPEC
        | metadata_flags::birthtim
        | metadata_flags::sparse
        //| metadata_flags::compressed
        ;
#endif
    return ret;
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC size_t directory_entry::compatibility_maximum() BOOST_NOEXCEPT_OR_NOTHROW
{
#ifdef WIN32
    // Let's choose 100k entries. Why not!
    return 100000;
#else
    return 100000;
    // This is what glibc uses, a 32Kb buffer.
    //return 32768/sizeof(dirent);
#endif
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC async_file_io_dispatcher_base::async_file_io_dispatcher_base(std::shared_ptr<thread_source> threadpool, file_flags flagsforce, file_flags flagsmask) : p(new detail::async_file_io_dispatcher_base_p(threadpool, flagsforce, flagsmask))
{
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC async_file_io_dispatcher_base::~async_file_io_dispatcher_base()
{
#ifndef BOOST_AFIO_COMPILING_FOR_GCOV
    engine_unordered_map_t<const detail::async_file_io_dispatcher_op *, std::pair<size_t, future_status>> reallyoutstanding;
    for(;;)
    {
        std::vector<std::pair<const size_t, std::shared_ptr<detail::async_file_io_dispatcher_op>>> outstanding;
        {
            lock_guard<decltype(p->opslock)> g(p->opslock); // may have no effect under concurrent_unordered_map
            if(!p->ops.empty())
            {
                outstanding.reserve(p->ops.size());
                for(auto &op: p->ops)
                {
                    if(op.second->h().valid())
                    {
                        auto it=reallyoutstanding.find(op.second.get());
                        if(reallyoutstanding.end()!=it)
                        {
                            if(it->second.first>=5)
                            {
                                static const char *statuses[]={ "ready", "timeout", "deferred", "unknown" };
                                int status=static_cast<int>(it->second.second);
                                std::cerr << "WARNING: ~async_file_dispatcher_base() detects stuck async_io_op in total of " << p->ops.size() << " extant ops\n"
                                    "   id=" << op.first << " type=" << detail::optypes[static_cast<size_t>(op.second->optype)] << " flags=0x" << std::hex << static_cast<size_t>(op.second->flags) << std::dec << " status=" << statuses[(status>=0 && status<=2) ? status : 3] << " failcount=" << it->second.first << " Completions:";
                                for(auto &c: op.second->completions)
                                {
                                    std::cerr << " id=" << c.first;
                                }
                                std::cerr << std::endl;
#ifdef BOOST_AFIO_OP_STACKBACKTRACEDEPTH
                                std::cerr << "  Allocation backtrace:" << std::endl;
                                size_t n=0;
                                for(void *addr: op.second.stack)
                                {
                                    Dl_info info;
                                    std::cerr << "    " << ++n << ". 0x" << std::hex << addr << std::dec << ": ";
                                    if(dladdr(addr, &info))
                                    {
                                        // This is hacky ...
                                        if(info.dli_fname)
                                        {
                                            char buffer2[4096];
                                            sprintf(buffer2, "/usr/bin/addr2line -C -f -i -e %s %lx", info.dli_fname, (long)((size_t) addr - (size_t) info.dli_fbase));
                                            FILE *ih=popen(buffer2, "r");
                                            auto unih=detail::Undoer([&ih]{ if(ih) pclose(ih); });
                                            if(ih)
                                            {
                                                size_t length=fread(buffer2, 1, sizeof(buffer2), ih);
                                                buffer2[length]=0;
                                                std::string buffer(buffer2, length-1);
                                                boost::replace_all(buffer, "\n", "\n       ");
                                                std::cerr << buffer << " (+0x" << std::hex << ((size_t) addr - (size_t) info.dli_saddr) << ")" << std::dec;
                                            }
                                            else std::cerr << info.dli_fname << ":0 (+0x" << std::hex << ((size_t) addr - (size_t) info.dli_fbase) << ")" << std::dec;
                                        }
                                        else std::cerr << "unknown:0";
                                    }
                                    else
                                        std::cerr << "completely unknown";
                                    std::cerr << std::endl;
                                }
#endif
                            }
                        }
                        outstanding.push_back(op);
                    }
                }
            }
        }
        if(outstanding.empty()) break;
        size_t mincount=(size_t)-1;
        for(auto &op: outstanding)
        {
            future_status status=op.second->h().wait_for(chrono::duration<int, ratio<1, 10>>(1));
            switch(status)
            {
            case future_status::ready:
                reallyoutstanding.erase(op.second.get());
                break;
            case future_status::deferred:
                // Probably pending on others, but log
            case future_status::timeout:
                auto it=reallyoutstanding.find(op.second.get());
                if(reallyoutstanding.end()==it)
                    it=reallyoutstanding.insert(std::make_pair(op.second.get(), std::make_pair(0, status))).first;
                it->second.first++;
                if(it->second.first<mincount) mincount=it->second.first;
                break;
            }
        }
        if(mincount>=10 && mincount!=(size_t)-1) // i.e. nothing is changing
        {
            std::cerr << "WARNING: ~async_file_dispatcher_base() sees no change in " << reallyoutstanding.size() << " stuck async_io_ops, so exiting destructor wait" << std::endl;
            break;
        }
    }
    for(size_t n=0; !p->fds.empty(); n++)
    {
        this_thread::sleep_for(chrono::milliseconds(10));
        if(n>300)
        {
            std::cerr << "WARNING: ~async_file_dispatcher_base() sees no change in " << p->fds.size() << " stuck shared_ptr<async_io_handle>'s, so exiting destructor wait" << std::endl;
            break;
        }
    }
#endif
    delete p;
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC void async_file_io_dispatcher_base::int_add_io_handle(void *key, std::shared_ptr<async_io_handle> h)
{
    {
        lock_guard<decltype(p->fdslock)> g(p->fdslock);
        p->fds.insert(std::make_pair(key, std::weak_ptr<async_io_handle>(h)));
    }
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC void async_file_io_dispatcher_base::int_del_io_handle(void *key)
{
    {
        lock_guard<decltype(p->fdslock)> g(p->fdslock);
        p->fds.erase(key);
    }
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::shared_ptr<thread_source> async_file_io_dispatcher_base::threadsource() const
{
    return p->pool;
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC file_flags async_file_io_dispatcher_base::fileflags(file_flags flags) const
{
    file_flags ret=(flags&~p->flagsmask)|p->flagsforce;
    if(!!(ret&file_flags::EnforceDependencyWriteOrder))
    {
        // The logic (for now) is this:
        // If the data is sequentially accessed, we won't be seeking much
        // so turn on AlwaysSync.
        // If not sequentially accessed and we might therefore be seeking,
        // turn on SyncOnClose.
        if(!!(ret&file_flags::WillBeSequentiallyAccessed))
            ret=ret|file_flags::AlwaysSync;
        else
            ret=ret|file_flags::SyncOnClose;
    }
    return ret;
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC size_t async_file_io_dispatcher_base::wait_queue_depth() const
{
    size_t ret=0;
    {
        lock_guard<decltype(p->opslock)> g(p->opslock);
        ret=p->ops.size();
    }
    return ret;
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC size_t async_file_io_dispatcher_base::fd_count() const
{
    size_t ret=0;
    {
        lock_guard<decltype(p->fdslock)> g(p->fdslock);
        ret=p->fds.size();
    }
    return ret;
}

// Non op lock holding variant
BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC async_io_op async_file_io_dispatcher_base::int_op_from_scheduled_id(size_t id) const
{
#ifdef BOOST_AFIO_USE_CONCURRENT_UNORDERED_MAP
    async_io_op ret;
    typedef decltype(p->ops)::value_type op_value_type;
    if(!p->ops.visit(id, [this, id, &ret](const op_value_type &op){
        ret=async_io_op(const_cast<async_file_io_dispatcher_base *>(this), id, op.second->h());
    }))
    {
        BOOST_AFIO_THROW(std::runtime_error("Failed to find this operation in list of currently executing operations"));
    }
    return ret;
#else
    engine_unordered_map_t<size_t, std::shared_ptr<detail::async_file_io_dispatcher_op>>::iterator it=p->ops.find(id);
    if(p->ops.end()==it)
    {
        BOOST_AFIO_THROW(std::runtime_error("Failed to find this operation in list of currently executing operations"));
    }
    return async_io_op(const_cast<async_file_io_dispatcher_base *>(this), id, it->second->h());
#endif
}
BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC async_io_op async_file_io_dispatcher_base::op_from_scheduled_id(size_t id) const
{
    async_io_op ret;
    {
        lock_guard<decltype(p->opslock)> g(p->opslock);
        ret=int_op_from_scheduled_id(id);
    }
    return ret;
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC void async_file_io_dispatcher_base::post_op_filter_clear()
{
    p->filters.clear();
    p->filters_buffers.clear();
}
BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC void async_file_io_dispatcher_base::post_op_filter(std::vector<std::pair<detail::OpType, std::function<async_file_io_dispatcher_base::filter_t>>> filters)
{
    p->filters.reserve(p->filters.size()+filters.size());
    p->filters.insert(p->filters.end(), std::make_move_iterator(filters.begin()), std::make_move_iterator(filters.end()));
}
BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC void async_file_io_dispatcher_base::post_readwrite_filter(std::vector<std::pair<detail::OpType, std::function<async_file_io_dispatcher_base::filter_readwrite_t>>> filters)
{
    p->filters_buffers.reserve(p->filters_buffers.size()+filters.size());
    p->filters_buffers.insert(p->filters_buffers.end(), std::make_move_iterator(filters.begin()), std::make_move_iterator(filters.end()));
}


#if defined(BOOST_AFIO_ENABLE_BENCHMARKING_COMPLETION) || BOOST_AFIO_HEADERS_ONLY==0
// Called in unknown thread
BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC async_file_io_dispatcher_base::completion_returntype async_file_io_dispatcher_base::invoke_user_completion_fast(size_t id, async_io_op op, async_file_io_dispatcher_base::completion_t *callback)
{
    return callback(id, op);
}
BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::vector<async_io_op> async_file_io_dispatcher_base::completion(const std::vector<async_io_op> &ops, const std::vector<std::pair<async_op_flags, async_file_io_dispatcher_base::completion_t *>> &callbacks)
{
    if(!ops.empty() && ops.size()!=callbacks.size())
        BOOST_AFIO_THROW(std::runtime_error("The sequence of preconditions must either be empty or exactly the same length as callbacks."));
    std::vector<async_io_op> ret;
    ret.reserve(callbacks.size());
    std::vector<async_io_op>::const_iterator i;
    std::vector<std::pair<async_op_flags, async_file_io_dispatcher_base::completion_t *>>::const_iterator c;
    detail::immediate_async_ops immediates(callbacks.size());
    if(ops.empty())
    {
        async_io_op empty;
        for(auto & c: callbacks)
        {
            ret.push_back(chain_async_op(immediates, (int) detail::OpType::UserCompletion, empty, c.first, &async_file_io_dispatcher_base::invoke_user_completion_fast, c.second));
        }
    }
    else for(i=ops.begin(), c=callbacks.begin(); i!=ops.end() && c!=callbacks.end(); ++i, ++c)
        ret.push_back(chain_async_op(immediates, (int) detail::OpType::UserCompletion, *i, c->first, &async_file_io_dispatcher_base::invoke_user_completion_fast, c->second));
    return ret;
}
#endif
// Called in unknown thread
BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC async_file_io_dispatcher_base::completion_returntype async_file_io_dispatcher_base::invoke_user_completion_slow(size_t id, async_io_op op, std::function<async_file_io_dispatcher_base::completion_t> callback)
{
    return callback(id, std::move(op));
}
BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::vector<async_io_op> async_file_io_dispatcher_base::completion(const std::vector<async_io_op> &ops, const std::vector<std::pair<async_op_flags, std::function<async_file_io_dispatcher_base::completion_t>>> &callbacks)
{
    if(!ops.empty() && ops.size()!=callbacks.size())
        BOOST_AFIO_THROW(std::runtime_error("The sequence of preconditions must either be empty or exactly the same length as callbacks."));
    std::vector<async_io_op> ret;
    ret.reserve(callbacks.size());
    std::vector<async_io_op>::const_iterator i;
    std::vector<std::pair<async_op_flags, std::function<async_file_io_dispatcher_base::completion_t>>>::const_iterator c;
    detail::immediate_async_ops immediates(callbacks.size());
    if(ops.empty())
    {
        async_io_op empty;
        for(auto & c: callbacks)
        {
            ret.push_back(chain_async_op(immediates, (int) detail::OpType::UserCompletion, empty, c.first, &async_file_io_dispatcher_base::invoke_user_completion_slow, c.second));
        }
    }
    else for(i=ops.begin(), c=callbacks.begin(); i!=ops.end() && c!=callbacks.end(); ++i, ++c)
            ret.push_back(chain_async_op(immediates, (int) detail::OpType::UserCompletion, *i, c->first, &async_file_io_dispatcher_base::invoke_user_completion_slow, c->second));
    return ret;
}

// Called in unknown thread
BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC void async_file_io_dispatcher_base::complete_async_op(size_t id, std::shared_ptr<async_io_handle> h, exception_ptr e)
{
    detail::immediate_async_ops immediates(1);
    std::shared_ptr<detail::async_file_io_dispatcher_op> thisop;
    std::vector<detail::async_file_io_dispatcher_op::completion_t> completions;
    {
#ifdef BOOST_AFIO_USE_CONCURRENT_UNORDERED_MAP
        // We can atomically remove without destruction
        auto np=p->ops.extract(id);
        if(!np)
#else
        lock_guard<decltype(p->opslock)> g(p->opslock);
        // Find me in ops, remove my completions and delete me from extant ops
        auto it=p->ops.find(id);
        if(p->ops.end()==it)
#endif
        {
#ifndef NDEBUG
            std::vector<size_t> opsids;
            for(auto &i: p->ops)
            {
                opsids.push_back(i.first);
            }
            std::sort(opsids.begin(), opsids.end());
#endif
            BOOST_AFIO_THROW_FATAL(std::runtime_error("Failed to find this operation in list of currently executing operations"));
        }
#ifdef BOOST_AFIO_USE_CONCURRENT_UNORDERED_MAP
        thisop.swap(np->second);
#else
        thisop.swap(it->second); // thisop=it->second;
        // Erase me from ops
        p->ops.erase(it);
#endif
        // Ok so this op is now removed from the ops list.
        // Because chain_async_op() holds the opslock during the finding of preconditions
        // and adding ops to its completions, we can now safely detach our completions
        // into stack storage and process them from there without holding any locks
        completions=std::move(thisop->completions);
    }
    // Early set future
    if(e)
    {
        thisop->enqueuement.set_future_exception(e);
        /*if(!thisop->enqueuement.get_future().is_ready())
        {
            int a=1;
        }
        assert(thisop->enqueuement.get_future().is_ready());
        assert(thisop->h().is_ready());
        assert(thisop->enqueuement.get_future().get_exception_ptr()==e);
        assert(thisop->h().get_exception_ptr()==e);*/
    }
    else
    {
        thisop->enqueuement.set_future_value(h);
        /*if(!thisop->enqueuement.get_future().is_ready())
        {
            int a=1;
        }
        assert(thisop->enqueuement.get_future().is_ready());
        assert(thisop->h().is_ready());
        assert(thisop->enqueuement.get_future().get()==h);
        assert(thisop->h().get()==h);*/
    }
    BOOST_AFIO_DEBUG_PRINT("X %u %p e=%d f=%p (uc=%u, c=%u)\n", (unsigned) id, h.get(), !!e, &thisop->h(), (unsigned) h.use_count(), (unsigned) thisop->completions.size());
    // Any post op filters installed? If so, invoke those now.
    if(!p->filters.empty())
    {
        async_io_op me(this, id, thisop->h(), false);
        for(auto &i: p->filters)
        {
            if(i.first==detail::OpType::Unknown || i.first==thisop->optype)
            {
                try
                {
                    i.second(thisop->optype, me);
                }
                catch(...)
                {
                    // throw it away
                }
            }
        }
    }
    if(!completions.empty())
    {
        for(auto &c: completions)
        {
            detail::async_file_io_dispatcher_op *c_op=c.second.get();
            BOOST_AFIO_DEBUG_PRINT("X %u (f=%u) > %u\n", (unsigned) id, (unsigned) c_op->flags, (unsigned) c.first);
            if(!!(c_op->flags & async_op_flags::immediate))
                immediates.enqueue(c_op->enqueuement);
            else
                p->pool->enqueue(c_op->enqueuement);
        }
    }
}


// Called in unknown thread 
template<class F, class... Args> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::shared_ptr<async_io_handle> async_file_io_dispatcher_base::invoke_async_op_completions(size_t id, async_io_op op, completion_returntype(F::*f)(size_t, async_io_op, Args...), Args... args)
{
    try
    {
#ifndef NDEBUG
        // Find our op
        {
            lock_guard<decltype(p->opslock)> g(p->opslock);
            auto it(p->ops.find(id));
            if(p->ops.end()==it)
            {
#ifndef NDEBUG
                std::vector<size_t> opsids;
                for(auto &i: p->ops)
                {
                    opsids.push_back(i.first);
                }
                std::sort(opsids.begin(), opsids.end());
#endif
                BOOST_AFIO_THROW_FATAL(std::runtime_error("Failed to find this operation in list of currently executing operations"));
            }
        }
#endif
        completion_returntype ret((static_cast<F *>(this)->*f)(id, std::move(op), args...));
        // If boolean is false, reschedule completion notification setting it to ret.second, otherwise complete now
        if(ret.first)
        {
            complete_async_op(id, ret.second);
        }
        return ret.second;
    }
    catch(...)
    {
        exception_ptr e(current_exception());
        assert(e);
        BOOST_AFIO_DEBUG_PRINT("E %u begin\n", (unsigned) id);
        complete_async_op(id, e);
        BOOST_AFIO_DEBUG_PRINT("E %u end\n", (unsigned) id);
        // complete_async_op() ought to have sent our exception state to our future,
        // so can silently drop the exception now
        return std::shared_ptr<async_io_handle>();
    }
}


// You MUST hold opslock before entry!
template<class F, class... Args> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC async_io_op async_file_io_dispatcher_base::chain_async_op(detail::immediate_async_ops &immediates, int optype, const async_io_op &precondition, async_op_flags flags, completion_returntype(F::*f)(size_t, async_io_op, Args...), Args... args)
{   
    size_t thisid=0;
    while(!(thisid=++p->monotoniccount));
#if 0 //ndef NDEBUG
    if(!p->ops.empty())
    {
        std::vector<size_t> opsids;
        for(auto &i: p->ops)
        {
            opsids.push_back(i.first);
        }
        std::sort(opsids.begin(), opsids.end());
        assert(thisid==opsids.back()+1);
    }
#endif
    // Wrap supplied implementation routine with a completion dispatcher
    auto wrapperf=&async_file_io_dispatcher_base::invoke_async_op_completions<F, Args...>;
    // Make a new async_io_op ready for returning
    auto thisop=std::make_shared<detail::async_file_io_dispatcher_op>((detail::OpType) optype, flags);
    // Bind supplied implementation routine to this, unique id, precondition and any args they passed
    thisop->enqueuement.set_task(std::bind(wrapperf, this, thisid, precondition, f, args...));
    // Set the output shared future
    async_io_op ret(this, thisid, thisop->h());
    typename detail::async_file_io_dispatcher_op::completion_t item(std::make_pair(thisid, thisop));
    bool done=false;
    auto unopsit=detail::Undoer([this, thisid](){
        std::string what;
        try { throw; } catch(std::exception &e) { what=e.what(); } catch(...) { what="not a std exception"; }
        BOOST_AFIO_DEBUG_PRINT("E X %u (%s)\n", (unsigned) thisid, what.c_str());
        {
#ifdef BOOST_AFIO_USE_CONCURRENT_UNORDERED_MAP
            p->ops.erase(thisid);
#else
            lock_guard<decltype(p->opslock)> g(p->opslock);
            auto opsit=p->ops.find(thisid);
            if(p->ops.end()!=opsit) p->ops.erase(opsit);
#endif
        }
    });
#ifdef BOOST_AFIO_USE_CONCURRENT_UNORDERED_MAP
    p->ops.insert(item);
    if(precondition.id)
    {
        // If still in flight, chain item to be executed when precondition completes
        typedef typename decltype(p->ops)::value_type op_value_type;
        p->ops.visit(precondition.id, [&item, &done](const op_value_type &dep){
          dep.second->completions.push_back(item);
          done=true;
        });
    }
#else
    {
        /* This is a weird bug which took me several days to track down ...
        It turns out that libstdc++ 4.8.0 will *move* insert item into
        p->ops because item's type is not *exactly* the value_type wanted
        by unordered_map. That destroys item for use later, which is
        obviously _insane_. The workaround is to feed insert() a copy. */
        auto item2(item);
        {
            lock_guard<decltype(p->opslock)> g(p->opslock);
            auto opsit=p->ops.insert(std::move(item2));
            assert(opsit.second);
            if(precondition.id)
            {
                // If still in flight, chain item to be executed when precondition completes
                auto dep(p->ops.find(precondition.id));
                if(p->ops.end()!=dep)
                {
                    dep->second->completions.push_back(item);
                    done=true;
                }
            }
        }
    }
#endif
    auto undep=detail::Undoer([done, this, &precondition, &item](){
        if(done)
        {
#ifdef BOOST_AFIO_USE_CONCURRENT_UNORDERED_MAP
            typedef typename decltype(p->ops)::value_type op_value_type;
            p->ops.visit(precondition.id, [&item](const op_value_type &dep){
                // Items may have been added by other threads ...
                for(auto it=--dep.second->completions.end(); true; --it)
                {
                    if(it->first==item.first)
                    {
                        dep.second->completions.erase(it);
                        break;
                    }
                    if(dep.second->completions.begin()==it) break;
                }              
            });
#else
            lock_guard<decltype(p->opslock)> g(p->opslock);
            auto dep(p->ops.find(precondition.id));
            // Items may have been added by other threads ...
            for(auto it=--dep->second->completions.end(); true; --it)
            {
                if(it->first==item.first)
                {
                    dep->second->completions.erase(it);
                    break;
                }
                if(dep->second->completions.begin()==it) break;
            }
#endif
        }
    });
    BOOST_AFIO_DEBUG_PRINT("I %u (d=%d) < %u (%s)\n", (unsigned) thisid, done, (unsigned) precondition.id, detail::optypes[static_cast<int>(optype)]);
    if(!done)
    {
        // Bind input handle now and queue immediately to next available thread worker
        if(precondition.id && !precondition.h.valid())
        {
            // It should never happen that precondition.id is valid but removed from extant ops
            // which indicates it completed and yet h remains invalid
            BOOST_AFIO_THROW_FATAL(std::runtime_error("Precondition was not in list of extant ops, yet its future is invalid. This should never happen for any real op, so it's probably memory corruption."));
        }
        if(!!(flags & async_op_flags::immediate))
            immediates.enqueue(thisop->enqueuement);
        else
            p->pool->enqueue(thisop->enqueuement);
    }
    undep.dismiss();
    unopsit.dismiss();
    return ret;
}


// General non-specialised implementation taking some arbitrary parameter T with precondition
template<class F, class T> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::vector<async_io_op> async_file_io_dispatcher_base::chain_async_ops(int optype, const std::vector<async_io_op> &preconditions, const std::vector<T> &container, async_op_flags flags, completion_returntype(F::*f)(size_t, async_io_op, T))
{
    std::vector<async_io_op> ret;
    ret.reserve(container.size());
    assert(preconditions.size()==container.size());
    if(preconditions.size()!=container.size())
        BOOST_AFIO_THROW(std::runtime_error("preconditions size does not match size of ops data"));
    detail::immediate_async_ops immediates(preconditions.size());
    auto precondition_it=preconditions.cbegin();
    auto container_it=container.cbegin();
    for(; precondition_it!=preconditions.cend() && container_it!=container.cend(); ++precondition_it, ++container_it)
        ret.push_back(chain_async_op(immediates, optype, *precondition_it, flags, f, *container_it));
    return ret;
}
// General non-specialised implementation taking some arbitrary parameter T without precondition
template<class F, class T> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::vector<async_io_op> async_file_io_dispatcher_base::chain_async_ops(int optype, const std::vector<T> &container, async_op_flags flags, completion_returntype(F::*f)(size_t, async_io_op, T))
{
    std::vector<async_io_op> ret;
    ret.reserve(container.size());
    async_io_op precondition;
    detail::immediate_async_ops immediates(container.size());
    for(auto &i: container)
    {
        ret.push_back(chain_async_op(immediates, optype, precondition, flags, f, i));
    }
    return ret;
}
// Generic op receiving specialisation i.e. precondition is also input op. Skips sanity checking.
template<class F> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::vector<async_io_op> async_file_io_dispatcher_base::chain_async_ops(int optype, const std::vector<async_io_op> &container, async_op_flags flags, completion_returntype(F::*f)(size_t, async_io_op, async_io_op))
{
    std::vector<async_io_op> ret;
    ret.reserve(container.size());
    detail::immediate_async_ops immediates(container.size());
    for(auto &i: container)
    {
        ret.push_back(chain_async_op(immediates, optype, i, flags, f, i));
    }
    return ret;
}
// Dir/file open specialisation
template<class F> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::vector<async_io_op> async_file_io_dispatcher_base::chain_async_ops(int optype, const std::vector<async_path_op_req> &container, async_op_flags flags, completion_returntype(F::*f)(size_t, async_io_op, async_path_op_req))
{
    std::vector<async_io_op> ret;
    ret.reserve(container.size());
    detail::immediate_async_ops immediates(container.size());
    for(auto &i: container)
    {
        ret.push_back(chain_async_op(immediates, optype, i.precondition, flags, f, i));
    }
    return ret;
}
// Data read and write specialisation
template<class F, bool iswrite> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::vector<async_io_op> async_file_io_dispatcher_base::chain_async_ops(int optype, const std::vector<detail::async_data_op_req_impl<iswrite>> &container, async_op_flags flags, completion_returntype(F::*f)(size_t, async_io_op, detail::async_data_op_req_impl<iswrite>))
{
    std::vector<async_io_op> ret;
    ret.reserve(container.size());
    detail::immediate_async_ops immediates(container.size());
    for(auto &i: container)
    {
        ret.push_back(chain_async_op(immediates, optype, i.precondition, flags, f, i));
    }
    return ret;
}
// Directory enumerate specialisation
template<class F> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::pair<std::vector<future<std::pair<std::vector<directory_entry>, bool>>>, std::vector<async_io_op>> async_file_io_dispatcher_base::chain_async_ops(int optype, const std::vector<async_enumerate_op_req> &container, async_op_flags flags, completion_returntype(F::*f)(size_t, async_io_op, async_enumerate_op_req, std::shared_ptr<promise<std::pair<std::vector<directory_entry>, bool>>> ret))
{
    typedef std::pair<std::vector<directory_entry>, bool> retitemtype;
    std::vector<async_io_op> ret;
    std::vector<future<retitemtype>> retfutures;
    ret.reserve(container.size());
    retfutures.reserve(container.size());
    detail::immediate_async_ops immediates(container.size());
    for(auto &i: container)
    {
        // Unfortunately older C++0x compilers don't cope well with feeding move only std::future<> into std::bind
        auto transport=std::make_shared<promise<retitemtype>>();
        retfutures.push_back(std::move(transport->get_future()));
        ret.push_back(chain_async_op(immediates, optype, i.precondition, flags, f, i, transport));
    }
    return std::make_pair(std::move(retfutures), std::move(ret));
}
// extents specialisation
template<class F> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::pair<std::vector<future<std::vector<std::pair<off_t, off_t>>>>, std::vector<async_io_op>> async_file_io_dispatcher_base::chain_async_ops(int optype, const std::vector<async_io_op> &container, async_op_flags flags, completion_returntype(F::*f)(size_t, async_io_op, std::shared_ptr<promise<std::vector<std::pair<off_t, off_t>>>> ret))
{
    typedef std::vector<std::pair<off_t, off_t>> retitemtype;
    std::vector<async_io_op> ret;
    std::vector<future<retitemtype>> retfutures;
    ret.reserve(container.size());
    retfutures.reserve(container.size());
    detail::immediate_async_ops immediates(container.size());
    for(auto &i: container)
    {
        // Unfortunately older C++0x compilers don't cope well with feeding move only std::future<> into std::bind
        auto transport=std::make_shared<promise<retitemtype>>();
        retfutures.push_back(std::move(transport->get_future()));
        ret.push_back(chain_async_op(immediates, optype, i, flags, f, transport));
    }
    return std::make_pair(std::move(retfutures), std::move(ret));
}
// statfs specialisation
template<class F> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::pair<std::vector<future<statfs_t>>, std::vector<async_io_op>> async_file_io_dispatcher_base::chain_async_ops(int optype, const std::vector<async_io_op> &container, const std::vector<fs_metadata_flags> &req, async_op_flags flags, completion_returntype(F::*f)(size_t, async_io_op, fs_metadata_flags, std::shared_ptr<promise<statfs_t>> ret))
{
    typedef statfs_t retitemtype;
    std::vector<async_io_op> ret;
    std::vector<future<retitemtype>> retfutures;
    ret.reserve(container.size());
    retfutures.reserve(container.size());
    assert(req.size()==container.size());
    if(req.size()!=container.size())
        BOOST_AFIO_THROW(std::runtime_error("req size does not match size of ops data"));
    detail::immediate_async_ops immediates(container.size());
    auto req_it=req.cbegin();
    auto container_it=container.cbegin();
    for(; req_it!=req.cend() && container_it!=container.cend(); ++req_it, ++container_it)
    {
        // Unfortunately older C++0x compilers don't cope well with feeding move only std::future<> into std::bind
        auto transport=std::make_shared<promise<retitemtype>>();
        retfutures.push_back(std::move(transport->get_future()));
        ret.push_back(chain_async_op(immediates, optype, *container_it, flags, f, *req_it, transport));
    }
    return std::make_pair(std::move(retfutures), std::move(ret));
}
// lock specialisation
template<class F> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::vector<async_io_op> async_file_io_dispatcher_base::chain_async_ops(int optype, const std::vector<async_lock_op_req> &container, async_op_flags flags, completion_returntype(F::*f)(size_t, async_io_op, async_lock_op_req))
{
    std::vector<async_io_op> ret;
    ret.reserve(container.size());
    detail::immediate_async_ops immediates(container.size());
    for(auto &i: container)
    {
        ret.push_back(chain_async_op(immediates, optype, i.precondition, flags, f, i));
    }
    return ret;
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::vector<async_io_op> async_file_io_dispatcher_base::adopt(const std::vector<std::shared_ptr<async_io_handle>> &hs)
{
    return chain_async_ops((int) detail::OpType::adopt, hs, async_op_flags::immediate, &async_file_io_dispatcher_base::doadopt);
}

namespace detail
{
    struct barrier_count_completed_state
    {
        atomic<size_t> togo;
        std::vector<std::pair<size_t, std::shared_ptr<async_io_handle>>> out;
        std::vector<shared_future<std::shared_ptr<async_io_handle>>> insharedstates;
        barrier_count_completed_state(const std::vector<async_io_op> &ops) : togo(ops.size()), out(ops.size())
        {
            insharedstates.reserve(ops.size());
            for(auto &i: ops)
            {
                insharedstates.push_back(i.h);
            }
        }
    };
}

/* This is extremely naughty ... you really shouldn't be using templates to hide implementation
types, but hey it works and is non-header so so what ...
*/
//template<class T> async_file_io_dispatcher_base::completion_returntype async_file_io_dispatcher_base::dobarrier(size_t id, async_io_op op, T state);
template<> BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC async_file_io_dispatcher_base::completion_returntype async_file_io_dispatcher_base::dobarrier<std::pair<std::shared_ptr<detail::barrier_count_completed_state>, size_t>>(size_t id, async_io_op op, std::pair<std::shared_ptr<detail::barrier_count_completed_state>, size_t> state)
{
    std::shared_ptr<async_io_handle> h(op.get(true)); // Ignore any error state until later
    size_t idx=state.second;
    detail::barrier_count_completed_state &s=*state.first;
    s.out[idx]=std::make_pair(id, h); // This might look thread unsafe, but each idx is unique
    if(--state.first->togo)
        return std::make_pair(false, h);
#if 1
    // On the basis that probably the preceding decrementing thread has yet to signal their future,
    // give up my timeslice
    this_thread::yield();
#endif
#if BOOST_AFIO_USE_BOOST_THREAD
    // Rather than potentially expend a syscall per wait on each input op to complete, compose a list of input futures and wait on them all
    std::vector<shared_future<std::shared_ptr<async_io_handle>>> notready;
    notready.reserve(s.insharedstates.size()-1);
    for(idx=0; idx<s.insharedstates.size(); idx++)
    {
        shared_future<std::shared_ptr<async_io_handle>> &f=s.insharedstates[idx];
        if(idx==state.second || is_ready(f)) continue;
        notready.push_back(f);
    }
    if(!notready.empty())
        boost::wait_for_all(notready.begin(), notready.end());
#else
    for(idx=0; idx<s.insharedstates.size(); idx++)
    {
        shared_future<std::shared_ptr<async_io_handle>> &f=s.insharedstates[idx];
        if(idx==state.second || is_ready(f)) continue;
        f.wait();
    }
#endif
    // Last one just completed, so issue completions for everything in out including myself
    for(idx=0; idx<s.out.size(); idx++)
    {
        shared_future<std::shared_ptr<async_io_handle>> &thisresult=s.insharedstates[idx];
        exception_ptr e(get_exception_ptr(thisresult));
        complete_async_op(s.out[idx].first, s.out[idx].second, e);
    }
    // As I just completed myself above, prevent any further processing
    return std::make_pair(false, std::shared_ptr<async_io_handle>());
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::vector<async_io_op> async_file_io_dispatcher_base::barrier(const std::vector<async_io_op> &ops)
{
#if BOOST_AFIO_VALIDATE_INPUTS
        for(auto &i: ops)
        {
            if(!i.validate(false))
                BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
        }
#endif
    // Create a shared state for the completions to be attached to all the items we are waiting upon
    auto state(std::make_shared<detail::barrier_count_completed_state>(ops));
    // Create each of the parameters to be sent to each dobarrier
    std::vector<std::pair<std::shared_ptr<detail::barrier_count_completed_state>, size_t>> statev;
    statev.reserve(ops.size());
    size_t idx=0;
    for(auto &op: ops)
    {
        statev.push_back(std::make_pair(state, idx++));
    }
    return chain_async_ops((int) detail::OpType::barrier, ops, statev, async_op_flags::immediate, &async_file_io_dispatcher_base::dobarrier<std::pair<std::shared_ptr<detail::barrier_count_completed_state>, size_t>>);
}

namespace detail {
    class async_file_io_dispatcher_compat : public async_file_io_dispatcher_base
    {
        // Called in unknown thread
        completion_returntype dodir(size_t id, async_io_op _, async_path_op_req req)
        {
            int ret=0;
            req.flags=fileflags(req.flags)|file_flags::int_opening_dir|file_flags::Read;
            if(!!(req.flags & (file_flags::Create|file_flags::CreateOnlyIfNotExist)))
            {
                ret=BOOST_AFIO_POSIX_MKDIR(req.path.c_str(), 0x1f8/*770*/);
                if(-1==ret && EEXIST==errno)
                {
                    // Ignore already exists unless we were asked otherwise
                    if(!(req.flags & file_flags::CreateOnlyIfNotExist))
                        ret=0;
                }
                BOOST_AFIO_ERRHOSFN(ret, req.path);
                req.flags=req.flags&~(file_flags::Create|file_flags::CreateOnlyIfNotExist);
            }

            BOOST_AFIO_POSIX_STAT_STRUCT s={0};
            ret=BOOST_AFIO_POSIX_STAT(req.path.c_str(), &s);
            if(0==ret && S_IFDIR!=(s.st_mode&S_IFDIR))
                BOOST_AFIO_THROW(std::runtime_error("Not a directory"));
            if(!(req.flags & file_flags::UniqueDirectoryHandle) && !!(req.flags & file_flags::Read) && !(req.flags & file_flags::Write))
            {
                // Return a copy of the one in the dir cache if available
                return std::make_pair(true, p->get_handle_to_dir(this, id, req, &async_file_io_dispatcher_compat::dofile));
            }
            else
            {
                return dofile(id, _, req);
            }
        }
        // Called in unknown thread
        completion_returntype dormdir(size_t id, async_io_op _, async_path_op_req req)
        {
            req.flags=fileflags(req.flags);
            BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_RMDIR(req.path.c_str()), req.path);
            auto ret=std::make_shared<async_io_handle_posix>(this, std::shared_ptr<async_io_handle>(), req.path, req.flags, false, false, -999);
            return std::make_pair(true, ret);
        }
        // Called in unknown thread
        completion_returntype dofile(size_t id, async_io_op, async_path_op_req req)
        {
            int flags=0;
            std::shared_ptr<async_io_handle> dirh;
            req.flags=fileflags(req.flags);
            if(!!(req.flags & file_flags::Read) && !!(req.flags & file_flags::Write)) flags|=O_RDWR;
            else if(!!(req.flags & file_flags::Read)) flags|=O_RDONLY;
            else if(!!(req.flags & file_flags::Write)) flags|=O_WRONLY;
            if(!!(req.flags & file_flags::Append)) flags|=O_APPEND;
            if(!!(req.flags & file_flags::Truncate)) flags|=O_TRUNC;
            if(!!(req.flags & file_flags::CreateOnlyIfNotExist)) flags|=O_EXCL|O_CREAT;
            else if(!!(req.flags & file_flags::Create)) flags|=O_CREAT;
#ifdef O_DIRECT
            if(!!(req.flags & file_flags::OSDirect)) flags|=O_DIRECT;
#endif
#ifdef O_SYNC
            if(!!(req.flags & file_flags::AlwaysSync)) flags|=O_SYNC;
#endif
            if(!!(req.flags & file_flags::int_opening_dir))
            {
#ifdef O_DIRECTORY
                flags|=O_DIRECTORY;
#endif
                // Some POSIXs don't like opening directories without buffering.
                if(!!(req.flags & file_flags::OSDirect))
                {
                    req.flags=req.flags & ~file_flags::OSDirect;
#ifdef O_DIRECT
                    flags&=~O_DIRECT;
#endif
                }
            }
            if(!!(req.flags & file_flags::FastDirectoryEnumeration))
                dirh=p->get_handle_to_containing_dir(this, id, req, &async_file_io_dispatcher_compat::dofile);
            // If writing and SyncOnClose and NOT synchronous, turn on SyncOnClose
            auto ret=std::make_shared<async_io_handle_posix>(this, dirh, req.path, req.flags, (file_flags::CreateOnlyIfNotExist|file_flags::DeleteOnClose)==(req.flags & (file_flags::CreateOnlyIfNotExist|file_flags::DeleteOnClose)), (file_flags::SyncOnClose|file_flags::Write)==(req.flags & (file_flags::SyncOnClose|file_flags::Write|file_flags::AlwaysSync)),
                BOOST_AFIO_POSIX_OPEN(req.path.c_str(), flags, 0x1b0/*660*/));
            static_cast<async_io_handle_posix *>(ret.get())->do_add_io_handle_to_parent();
            if(!(req.flags & file_flags::int_opening_dir) && !(req.flags & file_flags::int_opening_link) && !!(req.flags & file_flags::OSMMap))
                ret->try_mapfile();
            return std::make_pair(true, ret);
        }
        // Called in unknown thread
        completion_returntype dormfile(size_t id, async_io_op _, async_path_op_req req)
        {
            req.flags=fileflags(req.flags);
            BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_UNLINK(req.path.c_str()), req.path);
            auto ret=std::make_shared<async_io_handle_posix>(this, std::shared_ptr<async_io_handle>(), req.path, req.flags, false, false, -999);
            return std::make_pair(true, ret);
        }
        // Called in unknown thread
        completion_returntype dosymlink(size_t id, async_io_op op, async_path_op_req req)
        {
            std::shared_ptr<async_io_handle> h(op.get());
#ifdef WIN32
            BOOST_AFIO_THROW(std::runtime_error("Creating symbolic links via MSVCRT is not supported on Windows."));
#else
            req.flags=fileflags(req.flags)|file_flags::int_opening_link;
            BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_SYMLINK(h->path().c_str(), req.path.c_str()), req.path);
            auto ret=std::make_shared<async_io_handle_posix>(this, std::shared_ptr<async_io_handle>(), req.path, req.flags, false, false, -999);
            return std::make_pair(true, ret);
#endif
        }
        // Called in unknown thread
        completion_returntype dormsymlink(size_t id, async_io_op _, async_path_op_req req)
        {
            req.flags=fileflags(req.flags);
            BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_UNLINK(req.path.c_str()), req.path);
            auto ret=std::make_shared<async_io_handle_posix>(this, std::shared_ptr<async_io_handle>(), req.path, req.flags, false, false, -999);
            return std::make_pair(true, ret);
        }
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 6262) // Excessive stack usage
#endif
        // Called in unknown thread
        completion_returntype dozero(size_t id, async_io_op op, std::vector<std::pair<off_t, off_t>> ranges)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_posix *p=static_cast<async_io_handle_posix *>(h.get());
            bool done=false;
#if defined(__linux__)
            done=true;
            for(auto &i: ranges)
            {
              int ret;
              while(-1==(ret=fallocate(p->fd, 0x02/*FALLOC_FL_PUNCH_HOLE*/|0x01/*FALLOC_FL_KEEP_SIZE*/, i.first, i.second)) && EINTR==errno);
              if(-1==ret)
              {
                // The filing system may not support trim
                if(EOPNOTSUPP==errno)
                {
                  done=false;
                  break;
                }
                BOOST_AFIO_ERRHOSFN(-1, p->path());
              }
            }
#endif
            // Fall back onto a write of zeros
            if(!done)
            {
              char buffer[1024*1024];
              memset(buffer, 0, sizeof(buffer));
              for(auto &i: ranges)
              {
                ssize_t byteswritten=0;
                std::vector<iovec> vecs(1+(size_t)(i.second/sizeof(buffer)));
                for(size_t n=0; n<vecs.size(); n++)
                {
                  vecs[n].iov_base=buffer;
                  vecs[n].iov_len=(n<vecs.size()-1) ? sizeof(buffer) : (size_t)(i.second-(off_t) n*sizeof(buffer));
                }
                for(size_t n=0; n<vecs.size(); n+=IOV_MAX)
                {
                    ssize_t _byteswritten;
                    size_t amount=std::min((int) (vecs.size()-n), IOV_MAX);
                    off_t offset=i.first+byteswritten;
                    while(-1==(_byteswritten=pwritev(p->fd, (&vecs.front())+n, (int) amount, offset)) && EINTR==errno);
                    BOOST_AFIO_ERRHOSFN((int) _byteswritten, p->path());
                    byteswritten+=_byteswritten;
                } 
              }
            }
            return std::make_pair(true, h);
        }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
        // Called in unknown thread
        completion_returntype dosync(size_t id, async_io_op op, async_io_op)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_posix *p=static_cast<async_io_handle_posix *>(h.get());
            off_t bytestobesynced=p->write_count_since_fsync();
            if(bytestobesynced)
                BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_FSYNC(p->fd), p->path());
            p->has_ever_been_fsynced=true;
            p->byteswrittenatlastfsync+=bytestobesynced;
            return std::make_pair(true, h);
        }
        // Called in unknown thread
        completion_returntype doclose(size_t id, async_io_op op, async_io_op)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_posix *p=static_cast<async_io_handle_posix *>(h.get());
            if(!!(p->flags() & file_flags::int_opening_dir) && !(p->flags() & file_flags::UniqueDirectoryHandle) && !!(p->flags() & file_flags::Read) && !(p->flags() & file_flags::Write))
            {
                // As this is a directory which may be a fast directory enumerator, ignore close
            }
            else
            {
                p->close();
            }
            return std::make_pair(true, h);
        }
        // Called in unknown thread
        completion_returntype doread(size_t id, async_io_op op, detail::async_data_op_req_impl<false> req)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_posix *p=static_cast<async_io_handle_posix *>(h.get());
            ssize_t bytesread=0, bytestoread=0;
            iovec v;
            BOOST_AFIO_DEBUG_PRINT("R %u %p (%c) @ %u, b=%u\n", (unsigned) id, h.get(), p->path().native().back(), (unsigned) req.where, (unsigned) req.buffers.size());
#ifdef DEBUG_PRINTING
            for(auto &b: req.buffers)
            {   
                BOOST_AFIO_DEBUG_PRINT("  R %u: %p %u\n", (unsigned) id, asio::buffer_cast<const void *>(b), (unsigned) asio::buffer_size(b));
            }
#endif
            if(p->mapaddr)
            {
                void *addr=(void *)((char *)p->mapaddr + req.where);
                for(auto &b: req.buffers)
                {
                    memcpy(asio::buffer_cast<void *>(b), addr, asio::buffer_size(b));
                    addr=(void *)((char *)addr + asio::buffer_size(b));
                }
                return std::make_pair(true, h);
            }
            std::vector<iovec> vecs;
            vecs.reserve(req.buffers.size());
            for(auto &b: req.buffers)
            {
                v.iov_base=asio::buffer_cast<void *>(b);
                v.iov_len=asio::buffer_size(b);
                bytestoread+=v.iov_len;
                vecs.push_back(v);
            }
            for(size_t n=0; n<vecs.size(); n+=IOV_MAX)
            {
                ssize_t _bytesread;
                size_t amount=std::min((int) (vecs.size()-n), IOV_MAX);
                off_t offset=req.where+bytesread;
                while(-1==(_bytesread=preadv(p->fd, (&vecs.front())+n, (int) amount, offset)) && EINTR==errno);
                if(!this->p->filters_buffers.empty())
                {
                    asio::error_code ec(errno, generic_category());
                    for(auto &i: this->p->filters_buffers)
                    {
                        if(i.first==OpType::Unknown || i.first==OpType::read)
                        {
                            i.second(OpType::read, p, req, offset, n, amount, ec, (size_t)_bytesread);
                        }
                    }
                }
                BOOST_AFIO_ERRHOSFN((int) _bytesread, p->path());
                p->bytesread+=_bytesread;
                bytesread+=_bytesread;
            }
            if(bytesread!=bytestoread)
                BOOST_AFIO_THROW_FATAL(std::runtime_error("Failed to read all buffers"));
            return std::make_pair(true, h);
        }
        // Called in unknown thread
        completion_returntype dowrite(size_t id, async_io_op op, detail::async_data_op_req_impl<true> req)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_posix *p=static_cast<async_io_handle_posix *>(h.get());
            ssize_t byteswritten=0, bytestowrite=0;
            iovec v;
            std::vector<iovec> vecs;
            vecs.reserve(req.buffers.size());
            BOOST_AFIO_DEBUG_PRINT("W %u %p (%c) @ %u, b=%u\n", (unsigned) id, h.get(), p->path().native().back(), (unsigned) req.where, (unsigned) req.buffers.size());
#ifdef DEBUG_PRINTING
            for(auto &b: req.buffers)
            {   
                BOOST_AFIO_DEBUG_PRINT("  W %u: %p %u\n", (unsigned) id, asio::buffer_cast<const void *>(b), (unsigned) asio::buffer_size(b));
            }
#endif
            for(auto &b: req.buffers)
            {
                v.iov_base=(void *) asio::buffer_cast<const void *>(b);
                v.iov_len=asio::buffer_size(b);
                bytestowrite+=v.iov_len;
                vecs.push_back(v);
            }
            for(size_t n=0; n<vecs.size(); n+=IOV_MAX)
            {
                ssize_t _byteswritten;
                size_t amount=std::min((int) (vecs.size()-n), IOV_MAX);
                off_t offset=req.where+byteswritten;
                while(-1==(_byteswritten=pwritev(p->fd, (&vecs.front())+n, (int) amount, offset)) && EINTR==errno);
                if(!this->p->filters_buffers.empty())
                {
                    asio::error_code ec(errno, generic_category());
                    for(auto &i: this->p->filters_buffers)
                    {
                        if(i.first==OpType::Unknown || i.first==OpType::write)
                        {
                            i.second(OpType::write, p, req, offset, n, amount, ec, (size_t) _byteswritten);
                        }
                    }
                }
                BOOST_AFIO_ERRHOSFN((int) _byteswritten, p->path());
                p->byteswritten+=_byteswritten;
                byteswritten+=_byteswritten;
            }
            if(byteswritten!=bytestowrite)
                BOOST_AFIO_THROW_FATAL(std::runtime_error("Failed to write all buffers"));
            return std::make_pair(true, h);
        }
        // Called in unknown thread
        completion_returntype dotruncate(size_t id, async_io_op op, off_t newsize)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_posix *p=static_cast<async_io_handle_posix *>(h.get());
            BOOST_AFIO_DEBUG_PRINT("T %u %p (%c)\n", (unsigned) id, h.get(), p->path().native().back());
            int ret;
            while(-1==(ret=BOOST_AFIO_POSIX_FTRUNCATE(p->fd, newsize)) && EINTR==errno);
            BOOST_AFIO_ERRHOSFN(ret, p->path());
            return std::make_pair(true, h);
        }
#ifdef __linux__
        static int int_getdents(int fd, char *buf, int count) { return syscall(SYS_getdents64, fd, buf, count); }
#endif
        // Called in unknown thread
        completion_returntype doenumerate(size_t id, async_io_op op, async_enumerate_op_req req, std::shared_ptr<promise<std::pair<std::vector<directory_entry>, bool>>> ret)
        {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_posix *p=static_cast<async_io_handle_posix *>(h.get());
            try
            {
                auto globstr=req.glob.native();
                // Is glob a single entry match? If so, skip enumerating.
                if(!globstr.empty() && std::string::npos==globstr.find('*') && std::string::npos==globstr.find('?') && std::string::npos==globstr.find('['))
                {
                    std::vector<directory_entry> _ret;
                    _ret.reserve(1);
                    BOOST_AFIO_POSIX_STAT_STRUCT s={0};
                    filesystem::path path(p->path());
                    path/=req.glob;
                    if(-1!=BOOST_AFIO_POSIX_LSTAT(path.c_str(), &s))
                    {
                        stat_t stat(nullptr);
                        fill_stat_t(stat, s, req.metadata);
                        _ret.push_back(directory_entry(path
#ifdef BOOST_AFIO_USE_LEGACY_FILESYSTEM_SEMANTICS
                         .leaf()
#else
                         .filename()
#endif
                        , stat, req.metadata));
                    }
                    ret->set_value(std::make_pair(std::move(_ret), false));
                    return std::make_pair(true, h);
                }
#ifdef WIN32
                BOOST_AFIO_THROW(std::runtime_error("Enumerating directories via MSVCRT is not supported."));
#else
#ifdef __linux__
                // Unlike FreeBSD, Linux doesn't define a getdents() function, so we'll do that here.
                typedef int (*getdents64_t)(int, char *, int);
                getdents64_t getdents=(getdents64_t)(&async_file_io_dispatcher_compat::int_getdents);
                typedef dirent64 dirent;
#endif
                auto buffer=std::unique_ptr<dirent[]>(new dirent[req.maxitems]);
                if(req.restart)
                {
#ifdef __linux__
                    BOOST_AFIO_ERRHOS(lseek64(p->fd, 0, SEEK_SET));
#else
                    BOOST_AFIO_ERRHOS(lseek(p->fd, 0, SEEK_SET));
#endif
                }
                int bytes;
                bool done;
                do
                {
                    bytes=getdents(p->fd, (char *) buffer.get(), sizeof(dirent)*req.maxitems);
                    if(-1==bytes && EINVAL==errno)
                    {
                        req.maxitems++;
                        buffer=std::unique_ptr<dirent[]>(new dirent[req.maxitems]);
                        done=false;
                    }
                    else done=true;
                } while(!done);
                BOOST_AFIO_ERRHOS(bytes);
                if(!bytes)
                {
                    ret->set_value(std::make_pair(std::vector<directory_entry>(), false));
                    return std::make_pair(true, h);
                }
                VALGRIND_MAKE_MEM_DEFINED_IF_ADDRESSABLE(buffer.get(), bytes);
                bool thisbatchdone=(sizeof(dirent)*req.maxitems-bytes)>sizeof(dirent);
                std::vector<directory_entry> _ret;
                _ret.reserve(req.maxitems);
                directory_entry item;
                // This is what POSIX returns with getdents()
                item.have_metadata=item.have_metadata|metadata_flags::ino|metadata_flags::type;
                bool needmoremetadata=!!(req.metadata&~item.have_metadata);
                done=false;
                for(dirent *dent=buffer.get(); !done; dent=(dirent *)((size_t) dent + dent->d_reclen))
                {
                    if((bytes-=dent->d_reclen)<=0) done=true;
                    if(!dent->d_ino)
                        continue;
                    size_t length=strchr(dent->d_name, 0)-dent->d_name;
                    if(length<=2 && '.'==dent->d_name[0])
                        if(1==length || '.'==dent->d_name[1]) continue;
                    if(!req.glob.empty() && fnmatch(globstr.c_str(), dent->d_name, 0)) continue;
                    filesystem::path::string_type leafname(dent->d_name, length);
                    item.leafname=std::move(leafname);
                    item.stat.st_ino=dent->d_ino;
                    char d_type=dent->d_type;
                    if(DT_UNKNOWN==d_type)
                        item.have_metadata=item.have_metadata&~metadata_flags::type;
                    else
                    {
                        item.have_metadata=item.have_metadata|metadata_flags::type;
                        switch(d_type)
                        {
                        case DT_BLK:
                            item.stat.st_type=filesystem::file_type::block_file;
                            break;
                        case DT_CHR:
                            item.stat.st_type=filesystem::file_type::character_file;
                            break;
                        case DT_DIR:
                            item.stat.st_type=filesystem::file_type::directory_file;
                            break;
                        case DT_FIFO:
                            item.stat.st_type=filesystem::file_type::fifo_file;
                            break;
                        case DT_LNK:
                            item.stat.st_type=filesystem::file_type::symlink_file;
                            break;
                        case DT_REG:
                            item.stat.st_type=filesystem::file_type::regular_file;
                            break;
                        case DT_SOCK:
                            item.stat.st_type=filesystem::file_type::socket_file;
                            break;
                        default:
                            item.have_metadata=item.have_metadata&~metadata_flags::type;
                            item.stat.st_type=filesystem::file_type::type_unknown;
                            break;
                        }
                    }
                    _ret.push_back(std::move(item));
                }
                // NOTE: Potentially the OS didn't return type, and we're not checking
                // for that here.
                if(needmoremetadata)
                {
                    for(auto &i: _ret)
                    {
                        i.fetch_metadata(h, req.metadata);
                    }
                }
                ret->set_value(std::make_pair(std::move(_ret), !thisbatchdone));
#endif
                return std::make_pair(true, h);
            }
            catch(...)
            {
                ret->set_exception(current_exception());
                throw;
            }
        }
        // Called in unknown thread
        completion_returntype doextents(size_t id, async_io_op op, std::shared_ptr<promise<std::vector<std::pair<off_t, off_t>>>> ret)
        {
          try
          {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_posix *p=static_cast<async_io_handle_posix *>(h.get());
            std::vector<std::pair<off_t, off_t>> out;
            off_t start=0, end=0;
#ifndef WIN32
            for(;;)
            {
#ifdef __linux__
                start=lseek64(p->fd, end, SEEK_DATA);
                if((off_t)-1==start) break;
                end=lseek64(p->fd, start, SEEK_HOLE);
                if((off_t)-1==end) break;
#else
                start=lseek(p->fd, end, SEEK_DATA);
                if((off_t)-1==start) break;
                end=lseek(p->fd, start, SEEK_HOLE);
                if((off_t)-1==end) break;
#endif
                // Data region may have been concurrently deleted
                if(end>start)
                  out.push_back(std::make_pair<off_t, off_t>(std::move(start), end-start));
            }
            if(ENXIO!=errno)
            {
              if(EINVAL==errno)
              {
                // If it failed with no output, probably this filing system doesn't support extents
                if(out.empty())
                {
                  struct stat s;
                  BOOST_AFIO_ERRHOS(fstat(p->fd, &s));
                  out.push_back(std::make_pair<off_t, off_t>(0, s.st_size));                
                }
              }
              else
                BOOST_AFIO_ERRHOS(-1);
            }
#endif
            // A problem with SEEK_DATA and SEEK_HOLE is that they are racy under concurrent extents changing
            // Coalesce sequences of contiguous data e.g. 0, 64; 64, 64; 128, 64 ...
            std::vector<std::pair<off_t, off_t>> outfixed; outfixed.reserve(out.size());
            outfixed.push_back(out.front());
            for(size_t n=1; n<out.size(); n++)
            {
              if(outfixed.back().first+outfixed.back().second==out[n].first)
                outfixed.back().second+=out[n].second;
              else
                outfixed.push_back(out[n]);
            }
            ret->set_value(std::move(outfixed));
            return std::make_pair(true, h);
          }
          catch(...)
          {
            ret->set_exception(current_exception());
            throw;
          }
        }
        // Called in unknown thread
        completion_returntype dostatfs(size_t id, async_io_op op, fs_metadata_flags req, std::shared_ptr<promise<statfs_t>> ret)
        {
          try
          {
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_posix *p=static_cast<async_io_handle_posix *>(h.get());
            statfs_t out;
#ifndef WIN32
#ifdef __linux__
            struct statfs64 s={0};
            BOOST_AFIO_ERRHOS(fstatfs64(p->fd, &s));
            if(!!(req&fs_metadata_flags::bsize))       out.f_bsize      =s.f_bsize;
            if(!!(req&fs_metadata_flags::iosize))      out.f_iosize     =s.f_frsize;
            if(!!(req&fs_metadata_flags::blocks))      out.f_blocks     =s.f_blocks;
            if(!!(req&fs_metadata_flags::bfree))       out.f_bfree      =s.f_bfree;
            if(!!(req&fs_metadata_flags::bavail))      out.f_bavail     =s.f_bavail;
            if(!!(req&fs_metadata_flags::files))       out.f_files      =s.f_files;
            if(!!(req&fs_metadata_flags::ffree))       out.f_ffree      =s.f_ffree;
            if(!!(req&fs_metadata_flags::namemax))     out.f_namemax    =s.f_namelen;
//            if(!!(req&fs_metadata_flags::owner))       out.f_owner      =s.f_owner;
            if(!!(req&fs_metadata_flags::fsid))        { out.f_fsid[0]=(unsigned) s.f_fsid.__val[0]; out.f_fsid[1]=(unsigned) s.f_fsid.__val[1]; }
            if(!!(req&fs_metadata_flags::flags) || !!(req&fs_metadata_flags::fstypename) || !!(req&fs_metadata_flags::mntfromname) || !!(req&fs_metadata_flags::mntonname))
            {
              struct mountentry
              {
                std::string mnt_fsname, mnt_dir, mnt_type, mnt_opts;
                mountentry(const char *a, const char *b, const char *c, const char *d) : mnt_fsname(a), mnt_dir(b), mnt_type(c), mnt_opts(d) { }
              };
              std::vector<std::pair<mountentry, struct statfs64>> mountentries;
              {
                // Need to parse mount options on Linux
                FILE *mtab=setmntent("/etc/mtab", "r");
                if(!mtab) BOOST_AFIO_ERRHOS(-1);
                auto unmtab=detail::Undoer([mtab]{endmntent(mtab);});
                struct mntent m;
                char buffer[32768];
                while(getmntent_r(mtab, &m, buffer, sizeof(buffer)))
                {
                  struct statfs64 temp={0};
                  if(0==statfs64(m.mnt_dir, &temp) && temp.f_type==s.f_type && !memcmp(&temp.f_fsid, &s.f_fsid, sizeof(s.f_fsid)))
                    mountentries.push_back(std::make_pair(mountentry(m.mnt_fsname, m.mnt_dir, m.mnt_type, m.mnt_opts), temp));
                }
              }
#ifndef BOOST_AFIO_COMPILING_FOR_GCOV
              if(mountentries.empty())
                BOOST_AFIO_THROW("The filing system of this handle does not appear in /etc/mtab!");
              // Choose the mount entry with the most closely matching statfs. You can't choose
              // exclusively based on mount point because of bind mounts
              if(mountentries.size()>1)
              {
                std::vector<std::pair<size_t, size_t>> scores(mountentries.size());
                for(size_t n=0; n<mountentries.size(); n++)
                {
                  const char *a=(const char *) &mountentries[n].second;
                  const char *b=(const char *) &s;
                  scores[n].first=0;
                  for(size_t x=0; x<sizeof(struct statfs64); x++)
                    scores[n].first+=abs(a[x]-b[x]);
                  scores[n].second=n;
                }
                std::sort(scores.begin(), scores.end());
                auto temp(std::move(mountentries[scores.front().second]));
                mountentries.clear();
                mountentries.push_back(std::move(temp));
              }
#endif
              if(!!(req&fs_metadata_flags::flags))
              {
                out.f_flags.rdonly     =!!(s.f_flags & MS_RDONLY);
                out.f_flags.noexec     =!!(s.f_flags & MS_NOEXEC);
                out.f_flags.nosuid     =!!(s.f_flags & MS_NOSUID);
                out.f_flags.acls       =(std::string::npos!=mountentries.front().first.mnt_opts.find("acl") && std::string::npos==mountentries.front().first.mnt_opts.find("noacl"));
                out.f_flags.xattr      =(std::string::npos!=mountentries.front().first.mnt_opts.find("xattr") && std::string::npos==mountentries.front().first.mnt_opts.find("nouser_xattr"));
//                out.f_flags.compression=0;
                // Those filing systems supporting FALLOC_FL_PUNCH_HOLE
                out.f_flags.extents    =(mountentries.front().first.mnt_type=="btrfs"
                                      || mountentries.front().first.mnt_type=="ext4"
                                      || mountentries.front().first.mnt_type=="xfs"
                                      || mountentries.front().first.mnt_type=="tmpfs");
              }
              if(!!(req&fs_metadata_flags::fstypename)) out.f_fstypename=mountentries.front().first.mnt_type;
              if(!!(req&fs_metadata_flags::mntfromname)) out.f_mntfromname=mountentries.front().first.mnt_fsname;
              if(!!(req&fs_metadata_flags::mntonname)) out.f_mntonname=mountentries.front().first.mnt_dir;
            }
#else
            struct statfs s;
            BOOST_AFIO_ERRHOS(fstatfs(p->fd, &s));
            if(!!(req&fs_metadata_flags::flags))
            {
              out.f_flags.rdonly     =!!(s.f_flags & MNT_RDONLY);
              out.f_flags.noexec     =!!(s.f_flags & MNT_NOEXEC);
              out.f_flags.nosuid     =!!(s.f_flags & MNT_NOSUID);
              out.f_flags.acls       =!!(s.f_flags & (MNT_ACLS|MNT_NFS4ACLS));
              out.f_flags.xattr      =1; // UFS and ZFS support xattr. TODO FIXME actually calculate this, zfs get xattr <f_mntfromname> would do it.
              out.f_flags.compression=!strcmp(s.f_fstypename, "zfs");
              out.f_flags.extents    =!strcmp(s.f_fstypename, "ufs") || !strcmp(s.f_fstypename, "zfs");
            }
            if(!!(req&fs_metadata_flags::bsize))       out.f_bsize      =s.f_bsize;
            if(!!(req&fs_metadata_flags::iosize))      out.f_iosize     =s.f_iosize;
            if(!!(req&fs_metadata_flags::blocks))      out.f_blocks     =s.f_blocks;
            if(!!(req&fs_metadata_flags::bfree))       out.f_bfree      =s.f_bfree;
            if(!!(req&fs_metadata_flags::bavail))      out.f_bavail     =s.f_bavail;
            if(!!(req&fs_metadata_flags::files))       out.f_files      =s.f_files;
            if(!!(req&fs_metadata_flags::ffree))       out.f_ffree      =s.f_ffree;
            if(!!(req&fs_metadata_flags::namemax))     out.f_namemax    =s.f_namemax;
            if(!!(req&fs_metadata_flags::owner))       out.f_owner      =s.f_owner;
            if(!!(req&fs_metadata_flags::fsid))        { out.f_fsid[0]=(unsigned) s.f_fsid.val[0]; out.f_fsid[1]=(unsigned) s.f_fsid.val[1]; }
            if(!!(req&fs_metadata_flags::fstypename))  out.f_fstypename =s.f_fstypename;
            if(!!(req&fs_metadata_flags::mntfromname)) out.f_mntfromname=s.f_mntfromname;
            if(!!(req&fs_metadata_flags::mntonname))   out.f_mntonname  =s.f_mntonname;            
#endif
#endif
            ret->set_value(std::move(out));
            return std::make_pair(true, h);
          }
          catch(...)
          {
            ret->set_exception(current_exception());
            throw;
          }
        }
        // Called in unknown thread
        completion_returntype dolock(size_t id, async_io_op op, async_lock_op_req req)
        {
#ifndef BOOST_AFIO_COMPILING_FOR_GCOV
            std::shared_ptr<async_io_handle> h(op.get());
            async_io_handle_posix *p=static_cast<async_io_handle_posix *>(h.get());
            if(!p->lockfile)
              BOOST_AFIO_THROW(std::invalid_argument("This file handle was not opened with OSLockable."));
            return p->lockfile->lock(id, std::move(op), std::move(req));
#endif
        }

    public:
        async_file_io_dispatcher_compat(std::shared_ptr<thread_source> threadpool, file_flags flagsforce, file_flags flagsmask) : async_file_io_dispatcher_base(threadpool, flagsforce, flagsmask)
        {
        }


        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> dir(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::dir, reqs, async_op_flags::none, &async_file_io_dispatcher_compat::dodir);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> rmdir(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::rmdir, reqs, async_op_flags::none, &async_file_io_dispatcher_compat::dormdir);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> file(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::file, reqs, async_op_flags::none, &async_file_io_dispatcher_compat::dofile);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> rmfile(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::rmfile, reqs, async_op_flags::none, &async_file_io_dispatcher_compat::dormfile);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> symlink(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::symlink, reqs, async_op_flags::none, &async_file_io_dispatcher_compat::dosymlink);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> rmsymlink(const std::vector<async_path_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::rmsymlink, reqs, async_op_flags::none, &async_file_io_dispatcher_compat::dormsymlink);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> sync(const std::vector<async_io_op> &ops) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: ops)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::sync, ops, async_op_flags::none, &async_file_io_dispatcher_compat::dosync);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> zero(const std::vector<async_io_op> &ops, const std::vector<std::vector<std::pair<off_t, off_t>>> &ranges) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
          for(auto &i: ops)
          {
            if(!i.validate())
              BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
          }
#endif
          return chain_async_ops((int) detail::OpType::zero, ops, ranges, async_op_flags::none, &async_file_io_dispatcher_compat::dozero);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> close(const std::vector<async_io_op> &ops) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: ops)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::close, ops, async_op_flags::none, &async_file_io_dispatcher_compat::doclose);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> read(const std::vector<detail::async_data_op_req_impl<false>> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::read, reqs, async_op_flags::none, &async_file_io_dispatcher_compat::doread);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> write(const std::vector<detail::async_data_op_req_impl<true>> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::write, reqs, async_op_flags::none, &async_file_io_dispatcher_compat::dowrite);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> truncate(const std::vector<async_io_op> &ops, const std::vector<off_t> &sizes) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: ops)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::truncate, ops, sizes, async_op_flags::none, &async_file_io_dispatcher_compat::dotruncate);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::pair<std::vector<future<std::pair<std::vector<directory_entry>, bool>>>, std::vector<async_io_op>> enumerate(const std::vector<async_enumerate_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::enumerate, reqs, async_op_flags::none, &async_file_io_dispatcher_compat::doenumerate);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::pair<std::vector<future<std::vector<std::pair<off_t, off_t>>>>, std::vector<async_io_op>> extents(const std::vector<async_io_op> &ops) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: ops)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::extents, ops, async_op_flags::none, &async_file_io_dispatcher_compat::doextents);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::pair<std::vector<future<statfs_t>>, std::vector<async_io_op>> statfs(const std::vector<async_io_op> &ops, const std::vector<fs_metadata_flags> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: ops)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
            }
            if(ops.size()!=reqs.size())
                BOOST_AFIO_THROW(std::runtime_error("Inputs are invalid."));
#endif
            return chain_async_ops((int) detail::OpType::statfs, ops, reqs, async_op_flags::none, &async_file_io_dispatcher_compat::dostatfs);
        }
        BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC std::vector<async_io_op> lock(const std::vector<async_lock_op_req> &reqs) override final
        {
#if BOOST_AFIO_VALIDATE_INPUTS
            for(auto &i: reqs)
            {
                if(!i.validate())
                    BOOST_AFIO_THROW(std::invalid_argument("Inputs are invalid."));
            }
#endif
            return chain_async_ops((int) detail::OpType::lock, reqs, async_op_flags::none, &async_file_io_dispatcher_compat::dolock);
        }
    };
}

BOOST_AFIO_V1_NAMESPACE_END

#if defined(WIN32) && !defined(USE_POSIX_ON_WIN32)
#include "afio_iocp.ipp"
#endif

BOOST_AFIO_V1_NAMESPACE_BEGIN

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC void directory_entry::_int_fetch(metadata_flags wanted, std::shared_ptr<async_io_handle> dirh)
{
#ifdef WIN32
    detail::async_file_io_dispatcher_windows *dispatcher=dynamic_cast<detail::async_file_io_dispatcher_windows *>(dirh->parent());
    if(dispatcher)
    {
        windows_nt_kernel::init();
        using namespace windows_nt_kernel;
        bool slowPath=(!!(wanted&metadata_flags::nlink) || !!(wanted&metadata_flags::blocks) || !!(wanted&metadata_flags::blksize));
        if(!slowPath)
        {
            // Fast path skips opening a handle per file by enumerating the containing directory using a glob
            // exactly matching the leafname. This is about 10x quicker, so it's very much worth it.
            BOOST_AFIO_TYPEALIGNMENT(8) filesystem::path::value_type buffer[sizeof(FILE_ID_FULL_DIR_INFORMATION)/sizeof(filesystem::path::value_type)+32769];
            IO_STATUS_BLOCK isb={ 0 };
            UNICODE_STRING _glob;
            NTSTATUS ntstat;
            _glob.Buffer=const_cast<filesystem::path::value_type *>(leafname.c_str());
            _glob.MaximumLength=(_glob.Length=(USHORT) (leafname.native().size()*sizeof(filesystem::path::value_type)))+sizeof(filesystem::path::value_type);
            FILE_ID_FULL_DIR_INFORMATION *ffdi=(FILE_ID_FULL_DIR_INFORMATION *) buffer;
            ntstat=NtQueryDirectoryFile(dirh->native_handle(), NULL, NULL, NULL, &isb, ffdi, sizeof(buffer),
                FileIdFullDirectoryInformation, TRUE, &_glob, FALSE);
            if(STATUS_PENDING==ntstat)
                ntstat=NtWaitForSingleObject(dirh->native_handle(), FALSE, NULL);
            BOOST_AFIO_ERRHNTFN(ntstat, dirh->path());
            if(!!(wanted&metadata_flags::ino)) { stat.st_ino=ffdi->FileId.QuadPart; }
            if(!!(wanted&metadata_flags::type)) { stat.st_type=windows_nt_kernel::to_st_type(ffdi->FileAttributes); }
            if(!!(wanted&metadata_flags::atim)) { stat.st_atim=to_timepoint(ffdi->LastAccessTime); }
            if(!!(wanted&metadata_flags::mtim)) { stat.st_mtim=to_timepoint(ffdi->LastWriteTime); }
            if(!!(wanted&metadata_flags::ctim)) { stat.st_ctim=to_timepoint(ffdi->ChangeTime); }
            if(!!(wanted&metadata_flags::size)) { stat.st_size=ffdi->EndOfFile.QuadPart; }
            if(!!(wanted&metadata_flags::allocated)) { stat.st_allocated=ffdi->AllocationSize.QuadPart; }
            if(!!(wanted&metadata_flags::birthtim)) { stat.st_birthtim=to_timepoint(ffdi->CreationTime); }
            if(!!(wanted&metadata_flags::sparse)) { stat.st_sparse=!!(ffdi->FileAttributes & FILE_ATTRIBUTE_SPARSE_FILE); }
            if(!!(wanted&metadata_flags::compressed)) { stat.st_compressed=!!(ffdi->FileAttributes & FILE_ATTRIBUTE_COMPRESSED); }
        }
        else
        {
            // No choice here, open a handle and stat it.
            async_path_op_req req(dirh->path()/name(), file_flags::Read);
            auto fileh=dispatcher->dofile(0, async_io_op(), req).second;
            auto direntry=fileh->direntry(wanted);
            wanted=wanted & direntry.metadata_ready(); // direntry() can fail to fill some entries on Win XP
            //if(!!(wanted&metadata_flags::dev)) { stat.st_dev=direntry.stat.st_dev; }
            if(!!(wanted&metadata_flags::ino)) { stat.st_ino=direntry.stat.st_ino; }
            if(!!(wanted&metadata_flags::type)) { stat.st_type=direntry.stat.st_type; }
            //if(!!(wanted&metadata_flags::perms)) { stat.st_mode=direntry.stat.st_perms; }
            if(!!(wanted&metadata_flags::nlink)) { stat.st_nlink=direntry.stat.st_nlink; }
            //if(!!(wanted&metadata_flags::uid)) { stat.st_uid=direntry.stat.st_uid; }
            //if(!!(wanted&metadata_flags::gid)) { stat.st_gid=direntry.stat.st_gid; }
            //if(!!(wanted&metadata_flags::rdev)) { stat.st_rdev=direntry.stat.st_rdev; }
            if(!!(wanted&metadata_flags::atim)) { stat.st_atim=direntry.stat.st_atim; }
            if(!!(wanted&metadata_flags::mtim)) { stat.st_mtim=direntry.stat.st_mtim; }
            if(!!(wanted&metadata_flags::ctim)) { stat.st_ctim=direntry.stat.st_ctim; }
            if(!!(wanted&metadata_flags::size)) { stat.st_size=direntry.stat.st_size; }
            if(!!(wanted&metadata_flags::allocated)) { stat.st_allocated=direntry.stat.st_allocated; }
            if(!!(wanted&metadata_flags::blocks)) { stat.st_blocks=direntry.stat.st_blocks; }
            if(!!(wanted&metadata_flags::blksize)) { stat.st_blksize=direntry.stat.st_blksize; }
#ifdef HAVE_STAT_FLAGS
            if(!!(wanted&metadata_flags::flags)) { stat.st_flags=direntry.stat.st_flags; }
#endif
#ifdef HAVE_STAT_GEN
            if(!!(wanted&metadata_flags::gen)) { stat.st_gen=direntry.stat.st_gen; }
#endif
#ifdef HAVE_BIRTHTIMESPEC
            if(!!(wanted&metadata_flags::birthtim)) { stat.st_birthtim=direntry.stat.st_birthtim; }
#endif
            if(!!(wanted&metadata_flags::sparse)) { stat.st_sparse=direntry.stat.st_sparse; }
            if(!!(wanted&metadata_flags::compressed)) { stat.st_compressed=direntry.stat.st_compressed; }
        }
    }
    else
#endif
    {
        BOOST_AFIO_POSIX_STAT_STRUCT s={0};
        filesystem::path path(dirh->path());
        path/=leafname;
        BOOST_AFIO_ERRHOSFN(BOOST_AFIO_POSIX_LSTAT(path.c_str(), &s), path);
        fill_stat_t(stat, s, wanted);
    }
    have_metadata=have_metadata | wanted;
}

BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC std::shared_ptr<async_file_io_dispatcher_base> make_async_file_io_dispatcher(std::shared_ptr<thread_source> threadpool, file_flags flagsforce, file_flags flagsmask)
{
#if defined(WIN32) && !defined(USE_POSIX_ON_WIN32)
    return std::make_shared<detail::async_file_io_dispatcher_windows>(threadpool, flagsforce, flagsmask);
#else
    return std::make_shared<detail::async_file_io_dispatcher_compat>(threadpool, flagsforce, flagsmask);
#endif
}

BOOST_AFIO_V1_NAMESPACE_END

#ifdef _MSC_VER
#pragma warning(pop)
#endif

