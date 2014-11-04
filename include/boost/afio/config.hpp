/*
* File:   config.hpp
* Author: Paul Kirth
*
* Created on June 18, 2013, 7:30 PM
*/

 //  Copyright (c) 2013 Paul Kirth
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)


// most of this comes from Boost.Atomic 

#ifndef BOOST_AFIO_CONFIG_HPP
#define BOOST_AFIO_CONFIG_HPP

#if !defined(BOOST_AFIO_HEADERS_ONLY) && !defined(BOOST_ALL_DYN_LINK)
#define BOOST_AFIO_HEADERS_ONLY 1
#endif

// Get Mingw to assume we are on at least Windows 2000
#if __MSVCRT_VERSION__ < 0x601
#undef __MSVCRT_VERSION__
#define __MSVCRT_VERSION__ 0x601
#endif

// Fix up mingw weirdness
#if !defined(WIN32) && defined(_WIN32)
#define WIN32 1
#endif
// Boost ASIO needs this
#if !defined(_WIN32_WINNT) && defined(WIN32)
#define _WIN32_WINNT 0x0501
#endif
#if defined(WIN32) && _WIN32_WINNT<0x0501
#error _WIN32_WINNT must at least be set to Windows XP for Boost ASIO to compile
#endif


#if AFIO_STANDALONE 
#include "bindlib/include/boost/config.hpp"
#else
#include "boost/config.hpp"
#endif

#include "bindlib/include/import.hpp"
// Default to the C++ 11 STL for atomic, chrono, mutex and thread
#if defined(BOOST_AFIO_USE_BOOST_THREAD) && BOOST_AFIO_USE_BOOST_THREAD
# define BOOST_AFIO_V1_STL11_IMPL boost
#else
# define BOOST_AFIO_V1_STL11_IMPL std
# ifndef BOOST_AFIO_USE_BOOST_THREAD
#  define BOOST_AFIO_USE_BOOST_THREAD 0
# endif
#endif
// Default to the C++ 11 STL if on MSVC (Dinkumware ships a copy), else Boost
#ifndef BOOST_AFIO_USE_BOOST_FILESYSTEM
# if _MSC_VER >= 1900  // >= VS 14
#  define BOOST_AFIO_USE_BOOST_FILESYSTEM 0
# endif
#endif
#ifndef BOOST_AFIO_USE_BOOST_FILESYSTEM
# define BOOST_AFIO_USE_BOOST_FILESYSTEM 1
#endif
#if BOOST_AFIO_USE_BOOST_FILESYSTEM
# define BOOST_AFIO_V1_FILESYSTEM_IMPL boost
#else
# define BOOST_AFIO_V1_FILESYSTEM_IMPL std
#endif
// If building standalone, use a local asio, else Boost
#ifndef BOOST_AFIO_V1_ASIO_IMPL
# if ASIO_STANDALONE
#  define BOOST_AFIO_V1_ASIO_IMPL asio
# else
#  define BOOST_AFIO_V1_ASIO_IMPL boost
# endif
#endif
#define BOOST_AFIO_V1 (boost), (afio), (BOOST_LOCAL_BIND_NAMESPACE_VERSION(v1, BOOST_AFIO_V1_STL11_IMPL, BOOST_AFIO_V1_FILESYSTEM_IMPL, BOOST_AFIO_V1_ASIO_IMPL), inline)
#define BOOST_AFIO_V1_NAMESPACE       BOOST_LOCAL_BIND_NAMESPACE      (BOOST_AFIO_V1)
#define BOOST_AFIO_V1_NAMESPACE_BEGIN BOOST_LOCAL_BIND_NAMESPACE_BEGIN(BOOST_AFIO_V1)
#define BOOST_AFIO_V1_NAMESPACE_END   BOOST_LOCAL_BIND_NAMESPACE_END  (BOOST_AFIO_V1)

#define BOOST_STL11_ATOMIC_MAP_NAMESPACE_BEGIN        BOOST_LOCAL_BIND_NAMESPACE_BEGIN(BOOST_AFIO_V1, (stl11, inline))
#define BOOST_STL11_ATOMIC_MAP_NAMESPACE_END          BOOST_LOCAL_BIND_NAMESPACE_END  (BOOST_AFIO_V1, (stl11, inline))
#define BOOST_STL11_ATOMIC_MAP_NO_ATOMIC_CHAR32_T // missing VS14
#define BOOST_STL11_ATOMIC_MAP_NO_ATOMIC_CHAR16_T // missing VS14
#define BOOST_STL11_CHRONO_MAP_NAMESPACE_BEGIN        BOOST_LOCAL_BIND_NAMESPACE_BEGIN(BOOST_AFIO_V1, (stl11, inline), (chrono))
#define BOOST_STL11_CHRONO_MAP_NAMESPACE_END          BOOST_LOCAL_BIND_NAMESPACE_END  (BOOST_AFIO_V1, (stl11, inline), (chrono))
#define BOOST_STL1z_FILESYSTEM_MAP_NAMESPACE_BEGIN    BOOST_LOCAL_BIND_NAMESPACE_BEGIN(BOOST_AFIO_V1, (stl1z, inline), (filesystem))
#define BOOST_STL1z_FILESYSTEM_MAP_NAMESPACE_END      BOOST_LOCAL_BIND_NAMESPACE_END  (BOOST_AFIO_V1, (stl1z, inline), (filesystem))
// Match Dinkumware's TR2 implementation
#define BOOST_STL1z_FILESYSTEM_MAP_NO_SYMLINK_OPTION
#define BOOST_STL1z_FILESYSTEM_MAP_NO_COPY_OPTION
#define BOOST_STL1z_FILESYSTEM_MAP_NO_CHANGE_EXTENSION
#define BOOST_STL1z_FILESYSTEM_MAP_NO_WRECURSIVE_DIRECTORY_ITERATOR
#define BOOST_STL1z_FILESYSTEM_MAP_NO_EXTENSION
#define BOOST_STL1z_FILESYSTEM_MAP_NO_TYPE_PRESENT
#define BOOST_STL1z_FILESYSTEM_MAP_NO_PORTABLE_FILE_NAME
#define BOOST_STL1z_FILESYSTEM_MAP_NO_PORTABLE_DIRECTORY_NAME
#define BOOST_STL1z_FILESYSTEM_MAP_NO_PORTABLE_POSIX_NAME
#define BOOST_STL1z_FILESYSTEM_MAP_NO_LEXICOGRAPHICAL_COMPARE
#define BOOST_STL1z_FILESYSTEM_MAP_NO_WINDOWS_NAME
#define BOOST_STL1z_FILESYSTEM_MAP_NO_PORTABLE_NAME
#define BOOST_STL1z_FILESYSTEM_MAP_NO_BASENAME
#define BOOST_STL1z_FILESYSTEM_MAP_NO_COMPLETE
#define BOOST_STL1z_FILESYSTEM_MAP_NO_IS_REGULAR
#define BOOST_STL1z_FILESYSTEM_MAP_NO_INITIAL_PATH
#define BOOST_STL1z_FILESYSTEM_MAP_NO_PERMISSIONS_PRESENT
#define BOOST_STL1z_FILESYSTEM_MAP_NO_CODECVT_ERROR_CATEGORY
#define BOOST_STL1z_FILESYSTEM_MAP_NO_WPATH
#define BOOST_STL1z_FILESYSTEM_MAP_NO_SYMBOLIC_LINK_EXISTS
#define BOOST_STL1z_FILESYSTEM_MAP_NO_COPY_DIRECTORY
#define BOOST_STL1z_FILESYSTEM_MAP_NO_NATIVE
#define BOOST_STL11_FUTURE_MAP_NAMESPACE_BEGIN        BOOST_LOCAL_BIND_NAMESPACE_BEGIN(BOOST_AFIO_V1, (stl11, inline))
#define BOOST_STL11_FUTURE_MAP_NAMESPACE_END          BOOST_LOCAL_BIND_NAMESPACE_END  (BOOST_AFIO_V1, (stl11, inline))
#define BOOST_STL11_MUTEX_MAP_NAMESPACE_BEGIN         BOOST_LOCAL_BIND_NAMESPACE_BEGIN(BOOST_AFIO_V1, (stl11, inline))
#define BOOST_STL11_MUTEX_MAP_NAMESPACE_END           BOOST_LOCAL_BIND_NAMESPACE_END  (BOOST_AFIO_V1, (stl11, inline))
#define BOOST_STL1z_NETWORKING_MAP_NAMESPACE_BEGIN    BOOST_LOCAL_BIND_NAMESPACE_BEGIN(BOOST_AFIO_V1, (stl1z, inline), (asio))
#define BOOST_STL1z_NETWORKING_MAP_NAMESPACE_END      BOOST_LOCAL_BIND_NAMESPACE_END  (BOOST_AFIO_V1, (stl1z, inline), (asio))
#define BOOST_STL11_RATIO_MAP_NAMESPACE_BEGIN         BOOST_LOCAL_BIND_NAMESPACE_BEGIN(BOOST_AFIO_V1, (stl11, inline))
#define BOOST_STL11_RATIO_MAP_NAMESPACE_END           BOOST_LOCAL_BIND_NAMESPACE_END  (BOOST_AFIO_V1, (stl11, inline))
#define BOOST_STL11_THREAD_MAP_NAMESPACE_BEGIN        BOOST_LOCAL_BIND_NAMESPACE_BEGIN(BOOST_AFIO_V1, (stl11, inline))
#define BOOST_STL11_THREAD_MAP_NAMESPACE_END          BOOST_LOCAL_BIND_NAMESPACE_END  (BOOST_AFIO_V1, (stl11, inline))
#include BOOST_LOCAL_BIND_INCLUDE_STL11(bindlib, BOOST_AFIO_V1_STL11_IMPL, atomic)
#include BOOST_LOCAL_BIND_INCLUDE_STL11(bindlib, BOOST_AFIO_V1_STL11_IMPL, chrono)
#include BOOST_LOCAL_BIND_INCLUDE_STL1z(bindlib, BOOST_AFIO_V1_FILESYSTEM_IMPL, filesystem)
#include BOOST_LOCAL_BIND_INCLUDE_STL11(bindlib, BOOST_AFIO_V1_STL11_IMPL, future)
#include BOOST_LOCAL_BIND_INCLUDE_STL11(bindlib, BOOST_AFIO_V1_STL11_IMPL, mutex)
#include BOOST_LOCAL_BIND_INCLUDE_STL1z(bindlib, BOOST_AFIO_V1_ASIO_IMPL, networking)
#include BOOST_LOCAL_BIND_INCLUDE_STL11(bindlib, BOOST_AFIO_V1_STL11_IMPL, ratio)
#include BOOST_LOCAL_BIND_INCLUDE_STL11(bindlib, BOOST_AFIO_V1_STL11_IMPL, thread)

// Need to bind in asio::windows
#ifdef WIN32
#define BOOST_STL1z_NETWORKING_MAP_NAMESPACE_BEGIN    BOOST_LOCAL_BIND_NAMESPACE_BEGIN(BOOST_AFIO_V1, (stl1z, inline), (asio))
#define BOOST_STL1z_NETWORKING_MAP_NAMESPACE_END      BOOST_LOCAL_BIND_NAMESPACE_END  (BOOST_AFIO_V1, (stl1z, inline), (asio))
BOOST_STL1z_NETWORKING_MAP_NAMESPACE_BEGIN
#if ASIO_STANDALONE
namespace windows = ::asio::windows;
#else
namespace windows = ::boost::asio::windows;
#endif
BOOST_STL1z_NETWORKING_MAP_NAMESPACE_END
#undef BOOST_STL1z_NETWORKING_MAP_NAMESPACE_BEGIN
#undef BOOST_STL1z_NETWORKING_MAP_NAMESPACE_END
#endif

// TODO FIXME: Replace this with bindings
#include "spinlock/include/boost/spinlock/concurrent_unordered_map.hpp"
BOOST_AFIO_V1_NAMESPACE_BEGIN
  template<class Key, class T, class Hash, class Pred, class Alloc> using concurrent_unordered_map = boost::spinlock::v1_std::concurrent_unordered_map<Key, T, Hash, Pred, Alloc>;
  using boost::spinlock::v1_std::is_lockable_locked;
  using spins_to_sleep = boost::spinlock::v1_std::spins_to_sleep;
  template<size_t _0> using spins_to_yield = boost::spinlock::v1_std::spins_to_yield<_0>;
  template<size_t _0, bool _1=true> using spins_to_loop = boost::spinlock::v1_std::spins_to_loop<_0, _1>;
  using null_spin_policy = boost::spinlock::v1_std::null_spin_policy;
  template<class T> using spinlockbase = boost::spinlock::v1_std::spinlockbase<T>;
  template<class T> using lockable_ptr = boost::spinlock::v1_std::lockable_ptr<T>;
  template<typename T, template<class> class spinpolicy2=spins_to_loop<125>::policy, template<class> class spinpolicy3=spins_to_yield<250>::policy, template<class> class spinpolicy4=spins_to_sleep::policy> using spinlock = boost::spinlock::v1_std::spinlock<T, spinpolicy2, spinpolicy3, spinpolicy4>;
BOOST_AFIO_V1_NAMESPACE_END

///////////////////////////////////////////////////////////////////////////////
//  Set up dll import/export options
#if (defined(BOOST_AFIO_DYN_LINK) || defined(BOOST_ALL_DYN_LINK)) && \
    !defined(BOOST_AFIO_STATIC_LINK)

#if defined(BOOST_AFIO_SOURCE)
#undef BOOST_AFIO_HEADERS_ONLY
#define BOOST_AFIO_DECL BOOST_SYMBOL_EXPORT
#define BOOST_AFIO_BUILD_DLL
#else
#define BOOST_AFIO_DECL
#endif
#else
# define BOOST_AFIO_DECL
#endif // building a shared library


///////////////////////////////////////////////////////////////////////////////
//  Auto library naming
#if !defined(BOOST_AFIO_SOURCE) && !defined(BOOST_ALL_NO_LIB) && \
    !defined(BOOST_AFIO_NO_LIB) && !AFIO_STANDALONE

#define BOOST_LIB_NAME boost_afio

// tell the auto-link code to select a dll when required:
#if defined(BOOST_ALL_DYN_LINK) || defined(BOOST_AFIO_DYN_LINK)
#define BOOST_DYN_LINK
#endif

#include <boost/config/auto_link.hpp>

#endif  // auto-linking disabled

//#define BOOST_THREAD_VERSION 4
//#define BOOST_THREAD_PROVIDES_VARIADIC_THREAD
//#define BOOST_THREAD_DONT_PROVIDE_FUTURE
//#define BOOST_THREAD_PROVIDES_SIGNATURE_PACKAGED_TASK
#if BOOST_AFIO_HEADERS_ONLY == 1
# define BOOST_AFIO_HEADERS_ONLY_FUNC_SPEC inline
# define BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC inline
# define BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC inline virtual
// GCC gets upset if inline virtual functions aren't defined
# ifdef BOOST_GCC
#  define BOOST_AFIO_HEADERS_ONLY_VIRTUAL_UNDEFINED_SPEC { BOOST_AFIO_THROW_FATAL(std::runtime_error("Attempt to call pure virtual member function")); abort(); }
# else
#  define BOOST_AFIO_HEADERS_ONLY_VIRTUAL_UNDEFINED_SPEC =0;
# endif
#else
# define BOOST_AFIO_HEADERS_ONLY_FUNC_SPEC extern BOOST_AFIO_DECL
# define BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC
# define BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC virtual
# define BOOST_AFIO_HEADERS_ONLY_VIRTUAL_UNDEFINED_SPEC =0;
#endif

#if defined(__has_feature)
# if __has_feature(thread_sanitizer)
# define BOOST_AFIO_DISABLE_THREAD_SANITIZE __attribute__((no_sanitize_thread))
# endif
#endif
#ifndef BOOST_AFIO_DISABLE_THREAD_SANITIZE
# define BOOST_AFIO_DISABLE_THREAD_SANITIZE
#endif

#endif  /* BOOST_AFIO_CONFIG_HPP */

