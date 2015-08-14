#include "afio_pch.hpp"

//[workshop_naive_afio_interface
namespace afio = BOOST_AFIO_V2_NAMESPACE;
namespace filesystem = BOOST_AFIO_V2_NAMESPACE::filesystem;
using BOOST_MONAD_V1_NAMESPACE::monad;

class data_store
{
  afio::dispatcher_ptr _dispatcher;
  afio::handle_ptr _store;
public:
  // Type used for read streams
  using istream = std::shared_ptr<std::istream>;
  // Type used for write streams
  using ostream = std::shared_ptr<std::ostream>;
  // Type used for lookup
  using lookup_result_type = monad<istream>;
  // Type used for write
  using write_result_type = monad<ostream>;

  // Disposition flags
  static constexpr size_t writeable = (1<<0);

  // Open a data store at path
  data_store(size_t flags = 0, afio::path path = "store");
  
  // Look up item named name for reading, returning a std::istream for the item if it exists
  monad<istream> lookup(std::string name) noexcept;
  // Look up item named name for writing, returning an ostream for that item
  monad<ostream> write(std::string name) noexcept;
};
//]

//[workshop_naive_afio2]
namespace asio = BOOST_AFIO_V2_NAMESPACE::asio;
using BOOST_MONAD_V1_NAMESPACE::empty;
using BOOST_AFIO_V2_NAMESPACE::error_code;
using BOOST_AFIO_V2_NAMESPACE::generic_category;

// An iostream which reads directly from a memory mapped AFIO file
struct idirectstream : public std::istream
{
  struct directstreambuf : public std::streambuf
  {
    afio::handle_ptr h;  // Holds the file open and therefore mapped
    directstreambuf(afio::handle_ptr _h) : h(std::move(_h))
    {
      // Get the size of the file
      size_t length=(size_t) h->lstat(afio::metadata_flags::size).st_size;
      char *p = (char *) h->try_mapfile();
      // Set the get buffer this streambuf is to use
      setg(p, p, p + length);
    }
  };
  std::unique_ptr<directstreambuf> buf;
  idirectstream(afio::handle_ptr h) : std::istream(new directstreambuf(std::move(h))), buf(static_cast<directstreambuf *>(rdbuf()))
  {
  }
  virtual ~idirectstream() override
  {
    // Reset the stream before deleting the buffer
    rdbuf(nullptr);
  }
};

// An iostream which writes to an AFIO file in 4Kb pages
struct odirectstream : public std::ostream
{
  // A special allocator of highly efficient file i/o memory
  using file_buffer_type = std::vector<char, afio::utils::file_buffer_allocator<char>>;
  struct directstreambuf : public std::streambuf
  {
    using int_type = std::streambuf::int_type;
    using traits_type = std::streambuf::traits_type;
    afio::future<> lastwrite; // the last async write performed
    afio::off_t offset;       // offset of next write
    file_buffer_type buffer;  // a page size on this machine
    file_buffer_type lastbuffer;
    directstreambuf(afio::handle_ptr _h) : lastwrite(std::move(_h)), offset(0), buffer(afio::utils::page_sizes().front())
    {
      // Set the put buffer this streambuf is to use
      setp(buffer.data(), buffer.data(), buffer.data() + buffer.size());
    }
    virtual ~directstreambuf() override
    {
      try
      {
        // Flush buffers and wait until last write completes
        // Schedule an asynchronous write of the buffer to storage
        size_t thisbuffer = pptr() - pbase();
        if(thisbuffer)
          lastwrite = afio::async_write(afio::async_truncate(lastwrite, offset+thisbuffer), buffer.data(), thisbuffer, offset);
        lastwrite.get();
      }
      catch(...)
      {
      }
    }
    virtual int_type overflow(int_type c) override
    {
      size_t thisbuffer=pptr()-pbase();
      if(thisbuffer>=buffer.size())
        sync();
      if(c!=traits_type::eof())
      {
        *pptr()=(char)c;
        pbump(1);
        return traits_type::to_int_type(c);
      }
      return traits_type::eof();
    }
    virtual int sync() override
    {
      // Wait for the last write to complete, propagating any exceptions
      lastwrite.get();
      size_t thisbuffer=pptr()-pbase();
      if(thisbuffer > 0)
      {
        // Detach the current buffer and replace with a fresh one to allow the kernel to steal the page
        lastbuffer=std::move(buffer);
        buffer.resize(lastbuffer.size());
        setp(buffer.data(), buffer.data(), buffer.data() + buffer.size());
        // Schedule an extension of physical storage by an extra page
        lastwrite = afio::async_truncate(lastwrite, offset + thisbuffer);
        // Schedule an asynchronous write of the buffer to storage
        lastwrite=afio::async_write(lastwrite, lastbuffer.data(), thisbuffer, offset);
        offset+=thisbuffer;
      }
      return 0;
    }
  };
  std::unique_ptr<directstreambuf> buf;
  odirectstream(afio::handle_ptr h) : std::ostream(new directstreambuf(std::move(h))), buf(static_cast<directstreambuf *>(rdbuf()))
  {
  }
  virtual ~odirectstream() override
  {
    // Reset the stream before deleting the buffer
    rdbuf(nullptr);
  }
};
//]

//[workshop_naive_afio1]
namespace asio = BOOST_AFIO_V2_NAMESPACE::asio;
using BOOST_MONAD_V1_NAMESPACE::empty;
using BOOST_AFIO_V2_NAMESPACE::error_code;
using BOOST_AFIO_V2_NAMESPACE::generic_category;

static bool is_valid_name(const std::string &name) noexcept
{
  static const std::string banned("<>:\"/\\|?*\0", 10);
  if(std::string::npos!=name.find_first_of(banned))
    return false;
  // No leading period
  return name[0]!='.';
}

data_store::data_store(size_t flags, afio::path path)
{
  // Make a dispatcher for the local filesystem URI, masking out write flags on all operations if not writeable
  _dispatcher=afio::make_dispatcher("file:///", afio::file_flags::none, !(flags & writeable) ? afio::file_flags::write : afio::file_flags::none).get();
  // Set the dispatcher for this thread, and open a handle to the store directory
  afio::current_dispatcher_guard h(_dispatcher);
  _store=afio::dir(std::move(path), afio::file_flags::create);  // throws if there was an error
}

monad<data_store::istream> data_store::lookup(std::string name) noexcept
{
  if(!is_valid_name(name))
    return error_code(EINVAL, generic_category());
  try
  {
    error_code ec;
    // Open the file using the handle to the store directory as the base.
    // The store directory can be freely renamed by any third party process
    // and everything here will work perfectly.
    afio::handle_ptr h(afio::file(ec, _store, name, afio::file_flags::read));
    if(ec)
    {
      // If the file was not found, return empty else the error
      if(ec==error_code(ENOENT, generic_category()))
        return empty;
      return ec;
    }
    // Try to map the file into memory. This only works if the file was
    // opened read-only and there is enough virtual address space.
    void *addr=h->try_mapfile();
    if(!addr)
      return error_code(ENOMEM, generic_category());
    // Create an istream which directly uses the mapped file.
    return std::make_shared<idirectstream>(std::move(h));
  }
  catch(...)
  {
    return std::current_exception();
  }
}

monad<data_store::ostream> data_store::write(std::string name) noexcept
{
  if(!is_valid_name(name))
    return error_code(EINVAL, generic_category());
  try
  {
    error_code ec;
    // Open the file using the handle to the store directory as the base.
    // The store directory can be freely renamed by any third party process
    // and everything here will work perfectly. You could enable direct
    // buffer writing - this sends 4Kb pages directly to the physical hardware
    // bypassing the kernel file page cache, however this is not optimal if reads of
    // the value are likely to occur soon.
    afio::handle_ptr h(afio::file(ec, _store, name, afio::file_flags::create | afio::file_flags::write
      /*| afio::file_flags::os_direct*/));
    if(ec)
      return ec;
    // Create an ostream which directly uses the mapped file.
    return std::make_shared<odirectstream>(std::move(h));    
  }
  catch (...)
  {
    return std::current_exception();
  }
}
//]

int main(void)
{
  // To write a key-value item called "dog"
  {
    data_store ds;
    auto dogh = ds.write("dog").get();
    auto &dogs = *dogh;
    dogs << "I am a dog";
  }

  // To retrieve a key-value item called "dog"
  {
    data_store ds;
    auto dogh = ds.lookup("dog");
    if (dogh.empty())
      std::cerr << "No item called dog" << std::endl;
    else if(dogh.has_error())
      std::cerr << "ERROR: Looking up dog returned error " << dogh.get_error().message() << std::endl;
    else if (dogh.has_exception())
    {
      std::cerr << "FATAL: Looking up dog threw exception" << std::endl;
      std::terminate();
    }
    else
    {
      std::string buffer;
      *dogh.get() >> buffer;
      std::cout << "Item dog has value " << buffer << std::endl;
    }
  }
  return 0;
}
