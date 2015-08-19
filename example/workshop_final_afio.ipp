#include "../detail/SpookyV2.h"

//[workshop_final_interface
namespace afio = BOOST_AFIO_V2_NAMESPACE;
namespace filesystem = BOOST_AFIO_V2_NAMESPACE::filesystem;
using BOOST_MONAD_V1_NAMESPACE::lightweight_futures::shared_future;

class data_store
{
  afio::dispatcher_ptr _dispatcher;
  afio::handle_ptr _store, _store_append;
public:
  // Type used for read streams
  using istream = std::shared_ptr<std::istream>;
  // Type used for write streams
  using ostream = std::shared_ptr<std::ostream>;
  // Type used for lookup
  using lookup_result_type = shared_future<istream>;
  // Type used for write
  using write_result_type = shared_future<ostream>;

  // Disposition flags
  static constexpr size_t writeable = (1<<0);

  // Open a data store at path
  data_store(size_t flags = 0, afio::path path = "store");
  
  // Look up item named name for reading, returning an istream for the item
  shared_future<istream> lookup(std::string name) noexcept;
  // Look up item named name for writing, returning an ostream for that item
  shared_future<ostream> write(std::string name) noexcept;
};
//]

//[workshop_final3]
namespace asio = BOOST_AFIO_V2_NAMESPACE::asio;
using BOOST_MONAD_V1_NAMESPACE::empty;
using BOOST_AFIO_V2_NAMESPACE::error_code;
using BOOST_AFIO_V2_NAMESPACE::generic_category;

// A special allocator of highly efficient file i/o memory
using file_buffer_type = std::vector<char, afio::utils::file_buffer_allocator<char>>;

// An iostream which reads directly from a memory mapped AFIO file
struct idirectstream : public std::istream
{
  struct directstreambuf : public std::streambuf
  {
    afio::handle_ptr h;  // Holds the file open
    std::shared_ptr<file_buffer_type> buffer;
    // From a mmap
    directstreambuf(afio::handle_ptr _h, char *addr, size_t length) : h(std::move(_h))
    {
      // Set the get buffer this streambuf is to use
      setg(addr, addr, addr + length);
    }
    // From a malloc
    directstreambuf(afio::handle_ptr _h, std::shared_ptr<file_buffer_type> _buffer, size_t length) : h(std::move(_h)), buffer(std::move(_buffer))
    {
      // Set the get buffer this streambuf is to use
      setg(buffer->data(), buffer->data(), buffer->data() + length);
    }
  };
  std::unique_ptr<directstreambuf> buf;
  template<class U> idirectstream(afio::handle_ptr h, U &&buffer, size_t length) : std::istream(new directstreambuf(std::move(h), std::forward<U>(buffer), length)), buf(static_cast<directstreambuf *>(rdbuf()))
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
      setp(buffer.data(), buffer.data() + buffer.size());
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
        // TODO: On Windows do I need to close and reopen the file to flush metadata before
        //       the rename or does the rename do it for me?
        // Get handle to the parent directory
        auto dirh(lastwrite->container());
        // Atomically rename "tmpXXXXXXXXXXXXXXXX" to "0"
        lastwrite->atomic_relink(afio::path_req::relative(dirh, "0"));
#ifdef __linux__
        // Flush metadata on Linux only
        async_sync(dirh);
#endif
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
        setp(buffer.data(), buffer.data() + buffer.size());
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

//[workshop_final1]
namespace asio = BOOST_AFIO_V2_NAMESPACE::asio;
using BOOST_AFIO_V2_NAMESPACE::error_code;
using BOOST_AFIO_V2_NAMESPACE::generic_category;

// Serialisation helpers
#pragma pack(push, 1)
struct header_type
{
  afio::off_t index_offset_begin;  // Hint to the length of the store when the index was last written
  unsigned int version;            // "1" also used for endian detection
};  // 12 bytes
struct record_type
{
  afio::off_t kind : 1;     // 0 for blob, 1 for index
  afio::off_t length : 63;  // Size of the object (including this preamble) (+8)
  uint64 hash[2];           // 128-bit SpookyHash of the object (from below onwards) (+24)
  // index_regions
};
struct index_regions
{
  unsigned int regions_size;    // count of regions with their status (+28)
  struct
  {
    uint64 hash[2];           // 128-bit hash (all bits zero if this region has been hole punched)
    afio::off_t offset;       // offset to this free region
    afio::off_t in_use : 1;   // 1 if this region is in use
    afio::off_t length : 63;  // length of this free region
  } regions[1];  // each record is 32 bytes
  // Here comes many index_key_value's up to length
};
struct index_key_value
{
  unsigned int region_index;  // Index into regions
  unsigned int name_size;     // Length of key
  char name[1];               // Key string (utf-8)
};
#pragma pack(pop)

struct index
{
  afio::off_t previous_index;  // offset of previous index this one is based from
  std::unique_ptr<char[]> previous_index_buffer;
  struct region
  {
    bool in_use;
    afio::off_t offset, length;
    uint64 hash[2];
    bool operator<(const region &o) const noexcept { return offset<o.offset; }
    bool operator==(const region &o) const noexcept { return offset==o.offset && length==o.length; }
  };
  std::vector<region> regions;
  std::unordered_map<std::string, size_t> key_to_region;

  monad<void> read(afio::handle_ptr h) noexcept
  {
    error_code ec;
    previous_index=0;
    previous_index_buffer.reset();
    try
    {
      // Read the first eight bytes at the front of the file to get the index offset hint
      header_type header;
      afio::read(h, &header, 0);
      afio::off_t offset=0;
      if(header.version=='1')
        offset=header.index_offset_begin;
      else if(header.version==0x31000000)  // wrong endian
        return error_code(ENOEXEC, generic_category());
      else  // unknown version
        return error_code(ENOTSUP, generic_category());
      // Iterate the records starting from offset hint, keeping track of last known good index
      bool done=true;
      do
      {
        record_type record;
        for(; offset<_h->lstat(afio::metadata_flags::size).st_size;)
        {
          afio::read(ec, h, &record, offset);
          if(ec)
            break;
          if(!record.kind)
          {
            offset+=record.length;
            continue;
          }
          std::unique_ptr<char[]> buffer;
          try
          {
            buffer.reset(new char[(size_t) record.length-sizeof(header)]);
          }
          catch(...)
          {
            // Probably a corrupted record. We're done.
            break;
          }
          afio::read(ec, h, buffer.get(), (size_t) record.length-sizeof(header), offset+sizeof(header));
          if(ec)
            break;
          uint64 hash[2]={0, 0};
          SpookyHash::Hash128(buffer.get(), (size_t) record.length-sizeof(header), hash, hash+1);
          if(!memcmp(hash, record.hash, sizeof(hash)))
          {
            previous_index_buffer=std::move(buffer);
            previous_index=offset;
          }
          offset+=record.length;
        }
        if(previous_index_buffer)  // we have a valid most recent index
          done=true;
        else if(header.index_offset_begin>sizeof(header))
        {
          // Looks like the end of the store got trashed.
          // Reparse from the very beginning
          offset=sizeof(header);
          header.index_offset_begin=0;
        }
        else
        {
          // No viable records at all, or empty store.
          return {};
        }
      } while(!done);
      index_regions *r=(index_regions *) previous_index_buffer.get();
      regions.reserve(r->regions_size);
      for(size_t n=0; n<r->regions_size; n++)
        regions.push_back(region(r->regions+n));
      index_key_value *k=(index_key_value *)(r+r->regions_size), *end=(index_key_value *)(previous_index_buffer.get()+previous_index_buffer_length);
      while(k<end)
        key_to_region.insert(std::make_pair(std::string(k->name, k->name_size), k->region_index));
    }
    catch(...)
    {
      return std::current_exception();
    }
  }
};

data_store::data_store(size_t flags, afio::path path)
{
  // Make a dispatcher for the local filesystem URI, masking out write flags on all operations if not writeable
  _dispatcher=afio::make_dispatcher("file:///", afio::file_flags::none, !(flags & writeable) ? afio::file_flags::write : afio::file_flags::none).get();
  // Set the dispatcher for this thread, and open a handle to the store file
  afio::current_dispatcher_guard h(_dispatcher);
  _store_write=afio::file(path, afio::file_flags::create | afio::file_flags::append);  // throws if there was an error
  _store_read=afio::file(path, afio::file_flags::read_write);     // throws if there was an error
}

shared_future<data_store::istream> data_store::lookup(std::string name) noexcept
{
  if(!is_valid_name(name))
    return error_code(EINVAL, generic_category());
  try
  {
    name.append("/0");
    // Schedule the opening of the file for reading
    afio::future<> h(afio::async_file(_store, name, afio::file_flags::read));
    // When it completes, call this continuation
    return h.then([](afio::future<> &_h) -> shared_future<data_store::istream> {
      // If file didn't open, propagate the error
      if(!_h)
      {
        if(_h.has_error())
          return _h.get_error();
        else
          return _h.get_exception();
      }
      size_t length=(size_t) _h->lstat(afio::metadata_flags::size).st_size;
      // Is a memory map more appropriate?
      if(length>=128*1024)
      {
        char *addr;
        if((addr=(char *) _h->try_mapfile()))
        {
          data_store::istream ret(std::make_shared<idirectstream>(_h.get_handle(), addr, length));
          return ret;
        }
      }
      // Schedule the reading of the file into a buffer
      auto buffer=std::make_shared<file_buffer_type>(length);
      afio::future<> h(afio::async_read(_h, buffer->data(), length, 0));
      // When the read completes call this continuation
      return h.then([buffer, length](const afio::future<> &h) -> shared_future<data_store::istream> {
        // If read failed, propagate the error
        if(!h)
        {
          if(h.has_error())
            return h.get_error();
          else
            return h.get_exception();
        }
        data_store::istream ret(std::make_shared<idirectstream>(h.get_handle(), buffer, length));
        return ret;
      });
    });
  }
  catch(...)
  {
    return std::current_exception();
  }
}
//]

//[workshop_final2]
shared_future<data_store::ostream> data_store::write(std::string name) noexcept
{
  if(!is_valid_name(name))
    return error_code(EINVAL, generic_category());
  try
  {
    // Schedule the opening of the directory
    afio::future<> dirh(afio::async_dir(_store, name, afio::file_flags::create));
    // Make a crypto strong random file name
    std::string randomname("tmp");
    randomname.append(afio::utils::random_string(16));  // 128 bits
    // Schedule the opening of the file for writing
    afio::future<> h(afio::async_file(dirh, randomname, afio::file_flags::create | afio::file_flags::write
      | afio::file_flags::hold_parent_open    // handle should keep a handle_ptr to its parent dir
      /*| afio::file_flags::always_sync*/     // writes don't complete until upon physical storage
      ));
    // When it completes, call this continuation
    return h.then([](const afio::future<> &h) -> shared_future<data_store::ostream> {
      // If file didn't open, propagate the error
      if(!h)
      {
        if(h.has_error())
          return h.get_error();
        else
          return h.get_exception();
      }
      // Create an ostream which directly uses the file.
      data_store::ostream ret(std::make_shared<odirectstream>(h.get_handle()));
      return std::move(ret);
    });
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
