/* io_service.hpp
Multiplex file i/o
(C) 2015 Niall Douglas http://www.nedprod.com/
File Created: Dec 2015


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#ifndef BOOST_AFIO_IO_SERVICE_H
#define BOOST_AFIO_IO_SERVICE_H

#include "deadline.h"

#include <deque>
#include <utility>
#include <vector>

# undef _threadid  // windows macro splosh sigh

BOOST_AFIO_V2_NAMESPACE_BEGIN

class io_service;
class BOOST_AFIO_DECL io_service
{
public:
  //! The file extent type used by this i/o service
  using extent_type = unsigned long long;
  //! The memory extent type used by this i/o service
  using size_type = size_t;
  //! The scatter buffer type used by this i/o service
  using buffer_type = std::pair<char *, size_type>;
  //! The gather buffer type used by this i/o service
  using const_buffer_type = std::pair<const char *, size_type>;
  //! The scatter buffers type used by this i/o service
  using buffers_type = std::vector<buffer_type>;
  //! The gather buffers type used by this i/o service
  using const_buffers_type = std::vector<const_buffer_type>;
  //! The i/o request type used by this i/o service
  template<class T> struct io_request
  {
    T buffers;
    extent_type offset;
    constexpr io_request() : buffers(), offset(0) { }
    constexpr io_request(T _buffers, extent_type _offset) : buffers(std::move(_buffers)), offset(_offset) { }
  };
  //! The i/o result type used by this i/o service
  template<class T> class io_result : public result<T>
  {
    using Base = result<T>;
    size_type _bytes_transferred;
  public:
    constexpr io_result() noexcept : _bytes_transferred((size_type)-1) { }
    io_result(const io_result &) = default;
    io_result(io_result &&) = default;
    io_result &operator=(const io_result &) = default;
    io_result &operator=(io_result &&) = default;
    io_result(T _buffers) noexcept : Base(std::move(_buffers)), _bytes_transferred((size_type)-1) { }
    io_result(result<T> v) noexcept : Base(std::move(v)), _bytes_transferred((size_type)-1) { }
    //! Returns bytes transferred
    size_type bytes_transferred() noexcept
    {
      if (_bytes_transferred == (size_type)-1)
      {
        _bytes_transferred = 0;
        for (auto &i : value())
          _bytes_transferred += i.second;
      }
      return _bytes_transferred;
    }
  };
private:
#ifdef WIN32
  win::handle _threadh;
  win::dword _threadid;
#else
  pthread_t _threadh;
#endif
  stl11::mutex _posts_lock;
  struct post_info
  {
    io_service *service;
    detail::function_ptr<void(io_service *)> f;
    post_info(io_service *s, detail::function_ptr<void(io_service *)> _f) : service(s), f(std::move(_f)) { }
  };
  std::deque<post_info> _posts;
  using shared_size_type = size_type;
  shared_size_type _work_queued;
public:
  void _post_done(post_info *pi)
  {
    std::lock_guard<decltype(_posts_lock)> g(_posts_lock);
    // Find the post_info and remove it
    for (auto &i : _posts)
    {
      if (&i == pi)
      {
        i.f.reset();
        i.service = nullptr;
        pi = nullptr;
        break;
      }
    }
    assert(!pi);
    if (pi)
      abort();
    _work_done();
    while (!_posts.front().service)
      _posts.pop_front();
  }
  void _work_enqueued() { ++_work_queued; }
  void _work_done() { --_work_queued; }
  //! Creates an i/o service for the calling thread
  BOOST_AFIO_HEADERS_ONLY_MEMFUNC_SPEC io_service();
  io_service(io_service &&) = delete;
  io_service &operator=(io_service &&) = delete;
  BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC ~io_service();

  /*! Runs the i/o service for the thread owning this i/o service. Returns true if more
  work remains and we just handled an i/o; false if there is no more work; ETIMEDOUT if
  the deadline passed; EOPNOTSUPP if you try to call it from a non-owning thread; EINVAL
  if deadline is invalid.
  */
  BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC result<bool> run_until(deadline d) noexcept;
  //! \overload
  result<bool> run() noexcept { return run_until(deadline()); }

private:
  BOOST_AFIO_HEADERS_ONLY_VIRTUAL_SPEC void post(detail::function_ptr<void(io_service *)> &&f);
public:
  /*! Schedule the callable to be invoked by the thread owning this object at its next
  available opportunity. Unlike any other function in this API layer, this function is thread safe.
  */
  template<class U> void post(U &&f) { _post(detail::make_function_ptr<void(io_service *)>(std::forward<U>(f))); }
};

BOOST_AFIO_V2_NAMESPACE_END

# if BOOST_AFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#  define BOOST_AFIO_INCLUDED_BY_HEADER 1
#  ifdef WIN32
#   include "detail/impl/windows/io_service.ipp"
#  else
#   include "detail/impl/posix/io_service.ipp"
#  endif
#  undef BOOST_AFIO_INCLUDED_BY_HEADER
# endif

#endif