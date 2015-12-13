/* fs_probe.cpp
Probes the OS and filing system for various characteristics
(C) 2015 Niall Douglas http://www.nedprod.com/
File Created: Nov 2015


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

// I absolutely require min Win32 API to be Vista
#define WINVER 0x0600
#define _WIN32_WINNT 0x0600

#define _CRT_SECURE_NO_WARNINGS

#include "../test/afio_pch.hpp"
#include <vector>
#include <deque>
#include <regex>
#include <thread>
#include <tuple>
#include <iomanip>
#undef _threadid

namespace afio = boost::afio;
namespace filesystem = boost::afio::filesystem;
using afio::result;
using afio::make_result;
using afio::make_errored_result;
namespace detail
{
  template<class F> using function_ptr = boost::outcome::detail::function_ptr<F>;
  using boost::outcome::detail::make_function_ptr;
  using boost::outcome::detail::emplace_function_ptr;
}

enum class mode
{
  read = 0,
  write,
  append
};
enum class creation
{
  open = 0,
  only_if_not_exist,
  if_needed,
  truncate
};

constexpr unsigned flag_direct = (1<<0);
constexpr unsigned flag_sync = (1<<1);
constexpr unsigned permute_flags_max = 4;
constexpr unsigned flag_delete_on_close = (1<<4);
constexpr unsigned flag_no_race_protection = (1 << 5);

class handle;

class io_service
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
  HANDLE _threadh;
  DWORD _threadid;
#else
  pthread_t _threadh;
#endif
  std::mutex _posts_lock;
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
  io_service() : _work_queued(0)
  {
#ifdef WIN32
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &_threadh, 0, false, DUPLICATE_SAME_ACCESS))
      throw std::runtime_error("Failed to create creating thread handle");
    _threadid = GetCurrentThreadId();
#else
    _threadh = pthread_self();
#endif
  }
  io_service(io_service &&) = delete;
  io_service &operator=(io_service &&) = delete;
  virtual ~io_service()
  {
    if (_work_queued)
    {
      std::cerr << "WARNING: ~io_service() sees work still queued, blocking until no work queued" << std::endl;
      while (_work_queued)
        std::this_thread::yield();
    }
#ifdef WIN32
    CloseHandle(_threadh);
#endif
  }

  /*! Runs the i/o service for the thread owning this i/o service. Returns true if more
  work remains and we just handled an i/o; false if there is no more work; ETIMEDOUT if
  the deadline passed; EOPNOTSUPP if you try to call it from a non-owning thread; EINVAL
  if deadline is invalid.
  */
  virtual result<bool> run_until(const std::chrono::system_clock::time_point *deadline) noexcept;
  //! \overload
  result<bool> run() noexcept { return run_until(nullptr); }

private:
  virtual void post(detail::function_ptr<void(io_service *)> &&f);
public:
  /*! Schedule the callable to be invoked by the thread owning this object at its next
  available opportunity. Unlike any other function in this API layer, this function is thread safe.
  */
  template<class U> void post(U &&f) { _post(detail::make_function_ptr<void(io_service *)>(std::forward<U>(f))); }
};

result<bool> io_service::run_until(const std::chrono::system_clock::time_point *deadline) noexcept
{
  if (!_work_queued)
    return false;
#ifdef WIN32
  if (GetCurrentThreadId() != _threadid)
    return make_errored_result<bool>(EOPNOTSUPP);
  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  DWORD tosleep = INFINITE;
  if (deadline)
  {
    auto _tosleep = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now).count();
    if (_tosleep <= 0)
      _tosleep = 0;
    tosleep = (DWORD)_tosleep;
  }
  // Execute any APCs queued to this thread
  if (!SleepEx(tosleep, true))
  {
    auto _tosleep = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now).count();
    if (_tosleep <= 0)
      return make_errored_result<bool>(ETIMEDOUT);
  }
#else
  if (pthread_self() != _threadh)
    return make_errored_result<bool>(EOPNOTSUPP);
#error todo
#endif
  return _work_queued != 0;
}

void io_service::post(detail::function_ptr<void(io_service *)> &&f)
{
  void *data = nullptr;
  {
    post_info pi(this, std::move(f));
    std::lock_guard<decltype(_posts_lock)> g(_posts_lock);
    _posts.push_back(std::move(pi));
    data = (void *) &_posts.back();
  }
#ifdef WIN32
  PAPCFUNC apcf = [](ULONG_PTR data) {
    post_info *pi = (post_info *) data;
    pi->f(pi->service);
    pi->service->_post_done(pi);
  };
  if (QueueUserAPC(apcf, _threadh, (ULONG_PTR)data))
    _work_enqueued();
#else
#error todo
#endif
}

// A handle object
class handle
{
  io_service *_service;
  filesystem::path _path;
  unsigned _flags;
  handle(io_service *service, filesystem::path path, unsigned flags) : _service(service), _path(std::move(path)), _flags(flags) { }
public:
  //! The file extent type used by this i/o service
  using extent_type = io_service::extent_type;
  //! The memory extent type used by this i/o service
  using size_type = io_service::size_type;
  //! The scatter buffer type used by this i/o service
  using buffer_type = io_service::buffer_type;
  //! The gather buffer type used by this i/o service
  using const_buffer_type = io_service::const_buffer_type;
  //! The scatter buffers type used by this i/o service
  using buffers_type = io_service::buffers_type;
  //! The gather buffers type used by this i/o service
  using const_buffers_type = io_service::const_buffers_type;
  //! The i/o request type used by this i/o service
  template<class T> using io_request = io_service::io_request<T>;
  //! The i/o result type used by this i/o service
  template<class T> using io_result = io_service::io_result<T>;

  handle(handle &&o) noexcept : _service(o._service), _path(std::move(o._path)), _flags(o._flags), _v(std::move(o._v))
  {
    o._service = nullptr;
    o._flags = 0;
    o._v = 0;
  }
  handle &operator=(handle &&o) noexcept
  {
    this->~handle();
    new(this) handle(std::move(o));
    return *this;
  }
  //! The i/o service this handle is attached to
  io_service *service() const noexcept { return _service; }
private:
  using shared_size_type = size_type;
#ifdef WIN32
  HANDLE _v;
public:
  //! Create a handle opening access to a file on path managed using i/o service service
  static result<handle> create(io_service &service, filesystem::path path, mode _mode=mode::read, creation _creation = creation::open, unsigned flags = 0) noexcept
  {
    DWORD access = GENERIC_READ;
    switch (_mode)
    {
    case mode::append:
      access = FILE_APPEND_DATA;
      break;
    case mode::write:
      access = GENERIC_WRITE | GENERIC_READ;
      break;
    }
    DWORD creation = OPEN_EXISTING;
    switch (_creation)
    {
    case creation::only_if_not_exist:
      creation = CREATE_NEW;
      break;
    case creation::if_needed:
      creation = OPEN_ALWAYS;
      break;
    case creation::truncate:
      creation = TRUNCATE_EXISTING;
      break;
    }
    DWORD attribs = FILE_FLAG_OVERLAPPED;
    if (!!(flags & flag_direct)) attribs |= FILE_FLAG_NO_BUFFERING;
    if (!!(flags & flag_sync)) attribs |= FILE_FLAG_WRITE_THROUGH;
    //if(flags & flag_delete_on_close)
    //  attribs |= FILE_FLAG_DELETE_ON_CLOSE;
    result<handle> ret(handle(&service, std::move(path), flags));
    if (INVALID_HANDLE_VALUE == (ret.value()._v = CreateFile(ret.value()._path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, creation, attribs, NULL)))
      return make_errored_result<handle>(GetLastError());
    return ret;
  }
  ~handle()
  {
    if (_v) CloseHandle(_v);
  }
#else
  int _v;
public:
  //! Create a handle opening access to a file on path managed using i/o service service
  static result<handle> create(io_service &service, filesystem::path path, mode _mode=mode::read, creation _creation = creation::open, unsigned flags = 0) noexcept
  {
    int attribs = O_RDONLY;
    switch (_mode)
    {
    case mode::write:
      attribs = O_RDWR;
      break;
    case mode::append:
      attribs = O_APPEND;
      break;
    }
    switch (_creation)
    {
    case creation::only_if_not_exist:
      attribs |= O_CREAT | O_EXCL;
      break;
    case creation::if_needed:
      attribs |= O_CREAT;
      break;
    case creation::truncate:
      attribs |= O_TRUNC;
      break;
    }
    if (!!(flags & flag_direct)) attribs |= O_DIRECT;
    if (!!(flags & flag_sync)) attribs |= O_SYNC;
    result<handle> ret(handle(&service, std::move(path), flags));
    if (-1 == (ret.value()._v = ::open(ret.value()._path.c_str(), attribs, 0x1b0/*660*/)))
      return make_errored_result<handle>(errno);
    return ret;
  }
  ~handle()
  {
    if (_v)
    {
      // FIXME: Should delete on last open file handle close instead
      if(_flags & flag_delete_on_close)
        ::unlink(_path.c_str());
      ::close(_v);
    }
  }
#endif
private:
  template<class CompletionRoutine, class BuffersType> struct _io_state_type
  {
    handle *parent;
    io_result<BuffersType> result;
    CompletionRoutine completion;
    size_t items;
    shared_size_type items_to_go;
    constexpr _io_state_type(handle *_parent, CompletionRoutine &&f, size_t _items) : parent(_parent), result(make_result(BuffersType())), completion(std::forward<CompletionRoutine>(f)), items(_items), items_to_go(0) { }
    virtual ~_io_state_type()
    {
      // i/o pending is very bad, this should never happen
      assert(!items_to_go);
      if (items_to_go)
      {
        std::cerr << "FATAL: io_state destructed while i/o still in flight, the derived class should never allow this." << std::endl;
        abort();
      }
    }
  };
  struct _io_state_deleter { template<class U> void operator()(U *_ptr) const { _ptr->~U(); char *ptr = (char *)_ptr; ::free(ptr); } };
  template<class CompletionRoutine, class BuffersType> using _io_state_ptr = std::unique_ptr<_io_state_type<CompletionRoutine, BuffersType>, _io_state_deleter>;
  template<class CompletionRoutine, class BuffersType, class IORoutine> result<_io_state_ptr<CompletionRoutine, BuffersType>> _begin_io(io_request<BuffersType> reqs, CompletionRoutine &&completion, IORoutine &&ioroutine, const std::chrono::system_clock::time_point *deadline = nullptr) noexcept
  {
#ifdef WIN32
    // Need to keep a set of OVERLAPPED matching the scatter-gather buffers
    struct state_type : public _io_state_type<CompletionRoutine, BuffersType>
    {
      OVERLAPPED ols[1];
      state_type(handle *_parent, CompletionRoutine &&f, size_t _items) : _io_state_type<CompletionRoutine, BuffersType>(_parent, std::forward<CompletionRoutine>(f), _items) { }
      virtual ~state_type() override final
      {
        // Do we need to cancel pending i/o?
        if (items_to_go)
        {
          for (size_t n = 0; n < items; n++)
          {
            // If this is non-zero, probably this i/o still in flight
            if (ols[n].hEvent)
              CancelIoEx(parent->_v, ols + n);
          }
          // Pump the i/o service until all pending i/o is completed
          while (items_to_go)
            parent->service()->run();
        }
      }
    } *state;
    extent_type offset = reqs.offset;
    size_t statelen = sizeof(state_type)+(reqs.buffers.size()-1)*sizeof(OVERLAPPED), items(reqs.buffers.size());
    using return_type = _io_state_ptr<CompletionRoutine, BuffersType>;
    void *mem = ::malloc(statelen);
    if (!mem)
      return make_errored_result<return_type>(ENOMEM);
    return_type _state((_io_state_type<CompletionRoutine, BuffersType> *) mem);
    memset((state=(state_type *) mem), 0, statelen);
    new(state) state_type(this, std::forward<CompletionRoutine>(completion), items);
    // To be called once each buffer is read
    struct handle_completion
    {
      static VOID CALLBACK Do(DWORD errcode, DWORD bytes_transferred, LPOVERLAPPED ol)
      {
        state_type *state = (state_type *)ol->hEvent;
        ol->hEvent = nullptr;
        if (state->result)
        {
          if (errcode)
            state->result = make_errored_result<BuffersType>(errcode);
          else
          {
            // Figure out which i/o I am and update the buffer in question
            size_t idx = ol - state->ols;
            state->result.value()[idx].second = bytes_transferred;
          }
        }
        state->parent->service()->_work_done();
        // Are we done?
        if (!--state->items_to_go)
          state->completion(state);
      }
    };
    // Noexcept move the buffers from req into result
    BuffersType &out = state->result.value();
    out = std::move(reqs.buffers);
    for (size_t n = 0; n < items; n++)
    {
      LPOVERLAPPED ol = state->ols + n;
      ol->Offset = offset & 0xffffffff;
      ol->OffsetHigh = (offset >> 32) & 0xffffffff;
      // Use the unused hEvent member to pass through the state
      ol->hEvent = (HANDLE)state;
      offset += out[n].second;
      ++state->items_to_go;
      if (!(IORoutine)(_v, out[n].first, out[n].second, ol, handle_completion::Do))
      {
        --state->items_to_go;
        state->result = make_errored_result<BuffersType>(GetLastError());
        // Fire completion now if we didn't schedule anything
        if (!n)
          state->completion(state);
        return _state;
      }
      service()->_work_enqueued();
    }
    return _state;
#else
#error todo
#endif
  }
public:
  /*! Scatter read buffers from an offset into the open file. Note buffers returned may not be buffers input,
  and the deadline is a best effort deadline, if i/o takes long to cancel it may be significantly late.
  */
  io_result<buffers_type> read(io_request<buffers_type> reqs, const std::chrono::system_clock::time_point *deadline = nullptr) noexcept
  {
    io_result<buffers_type> ret;
    auto _io_state(_begin_io(std::move(reqs), [&ret](auto *state) {
      ret = std::move(state->result);
    }, &ReadFileEx, deadline));
    BOOST_OUTCOME_FILTER_ERROR(io_state, _io_state);

    // While i/o is not done pump i/o completion
    while(!ret)
    {
      auto t(_service->run_until(deadline));
      // If i/o service pump failed or timed out, cancel outstanding i/o and return
      if (!t)
        return make_errored_result<buffers_type>(t.get_error());
    }
    return ret;
  }
  /*! Gather write buffers to an offset into the open file. Note buffers returned may not be buffers input,
  and the deadline is a best effort deadline, if i/o takes long to cancel it may be significantly late.
  */
  io_result<const_buffers_type> write(io_request<const_buffers_type> reqs, const std::chrono::system_clock::time_point *deadline = nullptr) noexcept
  {
    io_result<const_buffers_type> ret;
    auto _io_state(_begin_io(std::move(reqs), [&ret](auto *state) {
      ret = std::move(state->result);
    }, &WriteFileEx, deadline));
    BOOST_OUTCOME_FILTER_ERROR(io_state, _io_state);

    // While i/o is not done pump i/o completion
    while (!ret)
    {
      auto t(_service->run_until(deadline));
      // If i/o service pump failed or timed out, cancel outstanding i/o and return
      if (!t)
        return make_errored_result<const_buffers_type>(t.get_error());
    }
    return ret;
  }
};

template<class T> constexpr T default_value() { return T{}; }
template<> constexpr io_service::extent_type default_value<io_service::extent_type>() { return (io_service::extent_type)-1; }
static struct storage_profile
{
  template<class T> struct item
  {
    const char *name;
    T value;
    constexpr item(const char *_name, T _value=default_value<T>()) : name(_name), value(_value) { }
  };

  void write(size_t indent, std::ostream &out) const
  {
    std::vector<std::string> lastsection;
    auto print = [indent, &out, &lastsection](auto &i) mutable {
      if (i.value != default_value<decltype(i.value)>())
      {
        std::vector<std::string> thissection;
        const char *s, *e;
        for (s = i.name, e=i.name; *e; e++)
        {
          if (*e == ':')
          {
            thissection.push_back(std::string(s, e - s));
            s = e + 1;
          }
        }
        std::string name(s, e - s);
        for (size_t n = 0; n < thissection.size(); n++)
        {
          indent += 4;
          if (n >= lastsection.size() || thissection[n] != lastsection[n])
          {
            out << std::string(indent, ' ') << thissection[n] << ":\n";
          }
        }
        out << std::string(indent+4, ' ') << name << ": " << i.value << "\n";
        lastsection = std::move(thissection);
      }
    };
    print(max_atomic_write);
  }
  item<io_service::extent_type> max_atomic_write = { "concurrency:atomicity:max_atomic_write" };
} profile[permute_flags_max];

int main(int argc, char *argv[])
{
  std::regex torun(".*");
  bool regexvalid = false;
  unsigned torunflags = 0;
  if (argc > 1)
  {
    try
    {
      torun.assign(argv[1]);
      regexvalid = true;
    }
    catch (...) {}
    if (argc > 2)
      torunflags = atoi(argv[2]);
    if (!regexvalid)
    {
      std::cerr << "Usage: " << argv[0] << " <regex for tests to run> [<flags>]" << std::endl;
      return 1;
    }
  }

  std::ofstream results("fs_probe_results.yaml", std::ios::app);
  {
    std::time_t t = std::time(nullptr);
    results << "---\ntimestamp: " << std::put_time(std::gmtime(&t), "%F %T %z") << std::endl;
  }
  for (unsigned flags = 0; flags <= torunflags; flags++)
  {
    if (!flags || !!(flags & torunflags))
    {
      // Figure out what the maximum atomic write is
      if (std::regex_match(profile[0].max_atomic_write.name, torun))
      {
        using off_t = io_service::extent_type;
        for (off_t size = !!(flags & flag_direct) ? 512 : 64; size <= 16 * 1024 * 1024; size = size * 2)
        {
          // Create two concurrent writer threads
          std::vector<std::thread> writers;
          std::atomic<size_t> done(2);
          for (char no = '1'; no <= '2'; no++)
            writers.push_back(std::thread([size, flags, no, &done] {
            io_service service;
            auto _h(handle::create(service, "temp", mode::write, creation::if_needed, flags | flag_delete_on_close | flag_no_race_protection));
            if (!_h)
            {
              std::cerr << "FATAL ERROR: Could not open work file due to " << _h.get_error().message() << std::endl;
              abort();
            }
            auto h(std::move(_h.get()));
            std::vector<char> buffer(size, no);
            handle::io_request<handle::const_buffers_type> reqs({ std::make_pair(buffer.data(), size) }, 0);
            --done;
            while (done)
              std::this_thread::yield();
            while (!done)
            {
              h.write(reqs);
            }
          }));
          while (done)
            std::this_thread::yield();
          // Repeatedly read from the file and check for torn writes
          io_service service;
          auto _h(handle::create(service, "temp", mode::read, creation::open, flags | flag_delete_on_close | flag_no_race_protection));
          if (!_h)
          {
            std::cerr << "FATAL ERROR: Could not open work file due to " << _h.get_error().message() << std::endl;
            abort();
          }
          auto h(std::move(_h.get()));
          std::vector<char> buffer(size, 0);
          handle::io_request<handle::buffers_type> reqs({ std::make_pair(buffer.data(), size) }, 0);
          bool failed = false;
          std::cout << "Testing atomicity of writes of " << size << " bytes ..." << std::endl;
          for (size_t transitions = 0; transitions < 1000 && !failed; transitions++)
          {
            h.read(reqs);
            const size_t *data = (size_t *)buffer.data(), *end=(size_t *)(buffer.data()+size);
            for (const size_t *d = data; d < end; d++)
            {
              if (*d != *data)
              {
                failed = true;
                break;
              }
            }
          }
          if (!failed)
            profile[flags].max_atomic_write.value = size;
          done = true;
          for (auto &writer : writers)
            writer.join();
          if (failed)
            break;
        }
        
        // Write out results for this combination of flags
        std::cout << "\ndirect=" << !!(flags & flag_direct) << " sync=" << !!(flags & flag_sync) << ":\n";
        profile[flags].write(0, std::cout);
        std::cout.flush();
        results << "direct=" << !!(flags & flag_direct) << " sync=" << !!(flags & flag_sync) << ":\n";
        profile[flags].write(0, results);
        results.flush();
      }
    }
  }

  return 0;
}