/* 
 * File:   Utility.hpp
 * Author: atlas
 *
 * Created on June 25, 2013, 12:43 PM
 */

#ifndef BOOST_AFIO_UTILITY_HPP
#define BOOST_AFIO_UTILITY_HPP

#include <unordered_map>
#include "Undoer.hpp"
#include "ErrorHandling.hpp"

//! \def BOOST_AFIO_TYPEALIGNMENT(bytes) The markup this compiler uses to mark a type as having some given alignment
#ifndef BOOST_AFIO_TYPEALIGNMENT
#if __cplusplus>=201103L && GCC_VERSION > 40900
#define BOOST_AFIO_TYPEALIGNMENT(bytes) alignas(bytes)
#else
#ifdef BOOST_MSVC
#define BOOST_AFIO_TYPEALIGNMENT(bytes) __declspec(align(bytes))
#elif defined(__GNUC__)
#define BOOST_AFIO_TYPEALIGNMENT(bytes) __attribute__((aligned(bytes)))
#else
#define BOOST_AFIO_TYPEALIGNMENT(bytes) unknown_type_alignment_markup_for_this_compiler
#endif
#endif
#endif

BOOST_AFIO_V1_NAMESPACE_BEGIN
  namespace detail {
#ifdef _MSC_VER
            static inline int win32_exception_filter()
            {
                return EXCEPTION_EXECUTE_HANDLER;
            }
            static inline void set_threadname(const char *threadName)
            {
                const DWORD MS_VC_EXCEPTION=0x406D1388;

#pragma pack(push,8)
                typedef struct tagTHREADNAME_INFO
                {
                    DWORD dwType; // Must be 0x1000.
                    LPCSTR szName; // Pointer to name (in user addr space).
                    DWORD dwThreadID; // Thread ID (-1=caller thread).
                    DWORD dwFlags; // Reserved for future use, must be zero.
                } THREADNAME_INFO;
#pragma pack(pop)
                THREADNAME_INFO info;
                info.dwType = 0x1000;
                info.szName = threadName;
                info.dwThreadID = (DWORD) -1;
                info.dwFlags = 0;

                __try
                {
                    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*) &info);
                }
                __except(win32_exception_filter())
                {
                    int a=1;
                }
            }
#elif defined(__linux__)
            static inline void set_threadname(const char *threadName)
            {
                pthread_setname_np(pthread_self(), threadName);
            }
#else
            static inline void set_threadname(const char *threadName)
            {
            }
#endif
        }
BOOST_AFIO_V1_NAMESPACE_END

// Need some portable way of throwing a really absolutely definitely fatal exception
// If we guaranteed had noexcept, this would be easy, but for compilers without noexcept
// we'll bounce through extern "C" as well just to be sure
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4297) // function assumed not to throw an exception but does __declspec(nothrow) or throw() was specified on the function
#endif
#ifdef BOOST_AFIO_COMPILING_FOR_GCOV
#define BOOST_AFIO_THROW_FATAL(x) std::terminate()
#else
BOOST_AFIO_V1_NAMESPACE_BEGIN
  namespace detail {
            BOOST_AFIO_HEADERS_ONLY_FUNC_SPEC void print_fatal_exception_message_to_stderr(const char *msg);
            template<class T> inline void do_throw_fatal_exception(const T &v) BOOST_NOEXCEPT_OR_NOTHROW
            {
                print_fatal_exception_message_to_stderr(v.what());
                throw v;
            }
            extern "C" inline void boost_afio_do_throw_fatal_exception(std::function<void()> impl) BOOST_NOEXCEPT_OR_NOTHROW{ impl(); }
            template<class T> inline void throw_fatal_exception(const T &v) BOOST_NOEXCEPT_OR_NOTHROW
            {
                // In case the extern "C" fails to terminate, trap and terminate here
                try
                {
                    std::function<void()> doer=std::bind(&do_throw_fatal_exception<T>, std::ref(v));
                    boost_afio_do_throw_fatal_exception(doer);
                }
                catch(...)
                {
                    std::terminate(); // Sadly won't produce much of a useful error message
                }
            }
        }
BOOST_AFIO_V1_NAMESPACE_END
#ifndef BOOST_AFIO_THROW_FATAL
#define BOOST_AFIO_THROW_FATAL(x) boost::afio::detail::throw_fatal_exception(x)
#endif
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#define BOOST_AFIO_THROW(x) throw x
#define BOOST_AFIO_RETHROW throw


BOOST_AFIO_V1_NAMESPACE_BEGIN

  namespace detail {
    // Support for make_unique. I keep wishing it was already here!
    template<class T, class... Args>
    std::unique_ptr<T> make_unique(Args &&... args){
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }

    // Support for combining hashes.
    template <class T>
    inline void hash_combine(std::size_t& seed, const T& v)
    {
        std::hash<T> hasher;
        seed ^= hasher(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
    }
    
    // Debug printing of exception info
    inline std::ostream &output_exception_info(std::ostream &os, const std::exception &e)
    {
        return os << "Exception: '" << e.what() << "'";
    }
    inline std::ostream &output_exception_info(std::ostream &os)
    {
        try { throw; }
        catch(const std::exception &e) { return output_exception_info(os, e); }
        catch(...) { return os << "Exception : 'unknown type'"; }
    }
    
  } // namespace

#if BOOST_AFIO_USE_BOOST_THREAD
    typedef boost::exception_ptr exception_ptr;
    using boost::current_exception;
#else
    typedef std::exception_ptr exception_ptr;
    using std::current_exception;
#endif
    // Get an exception ptr from a future
    template<typename T> inline exception_ptr get_exception_ptr(future<T> &f)
    {
#if BOOST_AFIO_USE_BOOST_THREAD
        // Thanks to Vicente for adding this to Boost.Thread
        return f.get_exception_ptr();
#else
        // This seems excessive but I don't see any other legal way to extract the exception ...
        bool success=false;
        try
        {
            f.get();
            success=true;
        }
        catch(...)
        {
            exception_ptr e(std::current_exception());
            assert(e);
            return e;
        }
        return exception_ptr();
#endif
    }
    template<typename T> inline exception_ptr get_exception_ptr(shared_future<T> &f)
    {
#if BOOST_AFIO_USE_BOOST_THREAD
        // Thanks to Vicente for adding this to Boost.Thread
        return f.get_exception_ptr();
#else
        // This seems excessive but I don't see any other legal way to extract the exception ...
        bool success=false;
        try
        {
            f.get();
            success=true;
        }
        catch(...)
        {
            exception_ptr e(std::current_exception());
            assert(e);
            return e;
        }
        return exception_ptr();
#endif
    }
    // Is a future ready?
    template<typename T> inline bool is_ready(const future<T> &f)
    {
#if BOOST_AFIO_USE_BOOST_THREAD
        return f.is_ready();
#else
        return f.wait_for(chrono::seconds(0))==future_status::ready;
#endif
    }
    template<typename T> inline bool is_ready(const shared_future<T> &f)
    {
#if BOOST_AFIO_USE_BOOST_THREAD
        return f.is_ready();
#else
        return f.wait_for(chrono::seconds(0))==future_status::ready;
#endif
    }

    struct filesystem_hash
    {
        std::hash<filesystem::path::string_type> hasher;
    public:
        size_t operator()(const filesystem::path& p) const
        {
            return hasher(p.native());
        }
    };

BOOST_AFIO_V1_NAMESPACE_END

#endif  /* BOOST_AFIO_UTILITY_HPP */

