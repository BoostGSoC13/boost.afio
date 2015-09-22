#include "workshop_interface.hpp"
namespace transactional_key_store
{
  namespace ondisk
  {
    // Serialisation helper types
#pragma pack(push, 1)
    // A dense on-disk hash map
    template<class Policy> struct dense_hashmap : public Policy
    {
      unsigned count;
      typename Policy::value_type items[1];

      char *end_of_items() const { return (char *)(items + count); }
      template<class Sequence> size_t bytes_needed(const Sequence &newitems) const noexcept
      {
        size_t ret = sizeof(*this)-sizeof(items[0]);
        for (unsigned n = 0; n < count; n++)
          if (items[n].key!=Policy::deleted_key)
            ret += sizeof(items[n]) + Policy::additional_size(items[n]);
        for (auto &i : newitems)
          ret += sizeof(items[0]) + Policy::additional_size(i);
        return ret;
      }
      // Predicate search on the dense hash map
      template<class Pred> const typename Policy::value_type *search(unsigned start, Pred &&pred) const noexcept
      {
        unsigned int left = start, right = start;
        if (pred(items+left))
          return items + left;
        do
        {
          left = (left == 0) ? count - 1 : left - 1;
          right = (++right == count) ? 0 : right;
          if (pred(items+left))
            return items + left;
          if (pred(items+right))
            return items + right;
        } while (left != right);
        return nullptr;
      }
      // Find an item in the dense hash map
      template<class T> const typename Policy::value_type *find(const T &k) const noexcept
      {
        unsigned int start = Policy::hash(k) % count;
        return search(start, [this, &k](const typename Policy::value_type *v) { return Policy::compare(v, k); });
      }
      // Find an empty slot in the dense hash map
      template<class T> typename Policy::value_type *find_empty(const T &k) const noexcept
      {
        unsigned int start = Policy::hash(k) % count;
        return const_cast<typename Policy::value_type *>(search(start, [](const typename Policy::value_type *v) { return !v->key && !v->value; }));
      }
      // Remove item
      void remove(typename Policy::value_type *i) noexcept
      {
        memset((void *) i, 0, sizeof(Policy::value_type));
        i->key = Policy::deleted_key;
      }
      // Update value on key
      void update(const typename Policy::value_type *i, typename Policy::value_type *v)
      {
        assert(i->key == v->key);
        *(typename Policy::value_type *)i = *v;
      }
    };
    struct StringKeyPolicy
    {
      struct value_type
      {
        unsigned int key;
        unsigned int value;

        unsigned int key_length;
      };
      static constexpr unsigned int deleted_key = 0;
      static unsigned hash(const std::string &k) noexcept
      {
        uint32 seed=0;
        return SpookyHash::Hash32(k.c_str(), k.size(), seed);
      }
      static unsigned hash(const std::pair<const char *, size_t> &k) noexcept
      {
        uint32 seed=0;
        return SpookyHash::Hash32(k.first, k.second, seed);
      }
      bool compare(const value_type *v, const std::string &k) const noexcept
      {
        if (v->key_length != (unsigned)k.size())
          return false;
        return !memcmp((char *) this + v->key, k.c_str(), k.size());
      }
      bool compare(const value_type *v, const std::pair<const char *, size_t> &k) const noexcept
      {
        if (v->key_length != (unsigned)k.second)
          return false;
        return !memcmp((char *) this + v->key, k.first, (unsigned) k.second);
      }
      static size_t additional_size(const value_type &v) noexcept { return v.key_length+1; }
      static size_t additional_size(const std::pair<std::string, int> &v) noexcept { return v.first.size() + 1; }
      std::pair<const char *, size_t> key(const value_type *v) const noexcept { return std::make_pair((const char *) this + v->key, v->key_length); }
      void store(value_type *newslot, const value_type *oldslot, char *&afteritems, const std::pair<const char *, size_t> &oldkey) noexcept
      {
        *newslot = *oldslot;
        newslot->key = (unsigned)(afteritems - (char *) this);
        memcpy(afteritems, oldkey.first, newslot->key_length + 1);
        afteritems += newslot->key_length + 1;
      }
      void store(value_type *newslot, const std::pair<std::string, int> &i, char *&afteritems) noexcept
      {
        newslot->value = i.second;
        newslot->key_length = (unsigned) i.first.size();
        newslot->key = (unsigned)(afteritems - (char *) this);
        memcpy(afteritems, i.first.c_str(), newslot->key_length + 1);
        afteritems += newslot->key_length + 1;
      }
    };
    struct BlobKeyPolicy
    {
      struct value_type
      {
        unsigned int key;
        afio::off_t value;

        unsigned int age;
      };
      static constexpr unsigned int deleted_key = (unsigned)-1;
      static unsigned hash(unsigned k) noexcept
      {
        return k;
      }
      bool compare(const value_type *v, unsigned k) const noexcept
      {
        return v->key==k;
      }
      static size_t additional_size(const value_type &v) noexcept { return 0; }
      static size_t additional_size(const std::pair<unsigned, afio::off_t> &v) noexcept { return 0; }
      unsigned key(const value_type *v) const noexcept { return v->key; }
      void store(value_type *newslot, const value_type *oldslot, char *&afteritems, unsigned oldkey) noexcept
      {
        *newslot = *oldslot;
      }
      void store(value_type *newslot, const std::pair<unsigned, afio::off_t> &i, char *&afteritems) noexcept
      {
        newslot->key = i.first;
        newslot->value = i.second;
      }
    };

    // A record length
    struct record_length  // 8 bytes  0xMMLLLLLLLLLLLLLLLLLLLLLLLLLLLLLK
    {
      afio::off_t magic : 16;     // 0xad magic
      afio::off_t _length : 43;   // 32 byte multiple size of the object (including this preamble, regions, key values) (+8)
      afio::off_t _spare : 1;
      afio::off_t hash_kind : 2;  // 1 = spookyhash, 2=blake2b
      afio::off_t kind : 2;       // 0 for zeroed space, 1,2 for blob, 3 for index
      afio::off_t value() const noexcept { return _length*32; }
      void value(afio::off_t v)
      {
        assert(!(v & 31));
        assert(v<(1ULL<<43));
        if((v & 31) || v>=(1ULL<<43))
          throw std::invalid_argument("Cannot store a non 32 byte multiple or a value greater than or equal to 2^43");
        _length=v/32;
      }
    };
    struct file_header   // 20 bytes
    {
      union
      {
        record_length length;          // Always 8 bytes
        char endian[8];
      };
      afio::off_t index_offset_begin;  // Hint to the length of the store when the index was last written
      unsigned int time_count;         // Racy monotonically increasing count
    };
    struct record_header  // 24 bytes - ALWAYS ALIGNED TO 32 BYTE FILE OFFSET
    {
      record_length length;      // (+8)
      hash_value_type hash;      // 128-bit hash of the object (from below onwards) (+28)
      hash_kind_type hash_kind() const noexcept { return static_cast<hash_kind_type>(length.hash_kind); }
      void do_hash(unsigned record_type, afio::off_t bytes, hash_kind_type k)
      {
        length.value(bytes);
        length.magic=0xad;
        length.hash_kind=static_cast<unsigned>(k);
        length.kind=record_type;
        const char *myend=((const char *) this)+sizeof(*this);
        SpookyHash::Hash128(myend, bytes-sizeof(*this), hash._uint64+0, hash._uint64+1);
      }
    };
    struct small_blob_record     // 8 bytes + length
    {
      record_header header;
      afio::off_t length;        // Exact byte length of this small blob
      char data[1];
    };
    struct large_blob_record     // 8 bytes + 16 bytes * extents
    {
      record_header header;
      afio::off_t length;        // Exact byte length of this large blob
      struct { afio::off_t offset; afio::off_t length; } pages[1];  // What in the large blob store makes up this blob
    };
    template<class Policy> struct index_record
    {
      record_header header;
      afio::off_t thisoffset;     // this index only valid if stored at this offset
      afio::off_t bloboffset;     // offset of the blob index we are using as a base
      ondisk::dense_hashmap<Policy> map;
    };
    using blobindex_record = index_record<BlobKeyPolicy>;
#pragma pack(pop)
  }

  // A special allocator of highly efficient file i/o memory
  using file_buffer_type = std::vector<char, afio::utils::page_allocator<char>>;

  template<class Policy> class dense_hashmap
  {
    afio::handle::mapped_file_ptr _maph;
    file_buffer_type _data;
    ondisk::dense_hashmap<Policy> *_get_mut() noexcept { return !_data.empty() ? (ondisk::dense_hashmap<Policy> *) _data.data() : nullptr; }
    const ondisk::dense_hashmap<Policy> *_get_const() const noexcept
    {
      if(!_data.empty())
        return (const ondisk::dense_hashmap<Policy> *) _data.data();
      if(_maph)
        return (const ondisk::dense_hashmap<Policy> *) _maph->addr;
      static const ondisk::dense_hashmap<Policy> empty;
      return &empty;
    }
    typename Policy::value_type *_cow(typename Policy::value_type *i=nullptr)
    {
      if(_data.empty())
      {
        _data.resize(_maph->length);
        memcpy(_data.data(), _maph->addr, _maph->length);
        i=(typename Policy::value_type *)(_data.data() + ((char *) i - (char *) _maph->addr)); 
        _maph.reset();
      }
      return i;
    }
  public:
    std::pair<const ondisk::dense_hashmap<Policy> *, size_t> raw_buffer() const noexcept
    {
      if(_data.empty())
        return std::make_pair((const ondisk::dense_hashmap<Policy> *) _maph->addr, _maph->length);
      else
        return std::make_pair((const ondisk::dense_hashmap<Policy> *) _data.data(), _data.size());
    }
    constexpr dense_hashmap() { }
    // From a file
    dense_hashmap(afio::handle_ptr h, afio::off_t offset, size_t length)
    {
      if(length>=128*1024)
        _maph=h->map_file(length, offset, true);
      if(!_maph)
      {
        _data.resize(length);
        afio::read(h, _data.data(), length, offset);
      }
    }
    dense_hashmap(const dense_hashmap &) = delete;
    dense_hashmap(dense_hashmap &&o) noexcept : _maph(std::move(o._maph)), _data(std::move(o._data)) { }
    dense_hashmap &operator=(const dense_hashmap &) = delete;
    dense_hashmap &operator=(dense_hashmap &&o) noexcept { _maph=std::move(o._maph); _data=std::move(o._data); return *this; }
    template<class Sequence> void insert(const Sequence &items)
    {
      size_t newsize = _get_const()->bytes_needed(items);
      file_buffer_type newdata(newsize);
      const ondisk::dense_hashmap<Policy> *oldmap = _get_const();
      ondisk::dense_hashmap<Policy> *newmap = (ondisk::dense_hashmap<Policy> *) newdata.data();
      // What's the new count?
      newmap->count = (unsigned) items.size();
      for (unsigned n = 0; n < oldmap->count; n++)
        if (oldmap->items[n].key != Policy::deleted_key)
          newmap->count++;
      char *afteritems = newmap->end_of_items();
      // Rehash old to new
      for (unsigned n = 0; n < oldmap->count; n++)
        if (oldmap->items[n].key != Policy::deleted_key)
        {
          auto key = oldmap->key(oldmap->items + n);
          auto *newslot = newmap->find_empty(key);
          newmap->store(newslot, oldmap->items + n, afteritems, key);
        }
      // Add the new items
      for (auto &i : items)
      {
        auto *newslot = newmap->find_empty(i.first);
        newmap->store(newslot, i, afteritems);
      }
      _data = std::move(newdata);
      _maph.reset();
    }
    template<class Key> const typename Policy::value_type *find(const Key &k) const noexcept
    {
      auto p=_get_const();
      return p ? p->find(k) : nullptr;
    }
    void erase(typename Policy::value_type *i) noexcept
    {
      assert(i);
      _get_mut()->remove(_cow(i));
    }
    void erase(const typename Policy::value_type *i) noexcept { erase(const_cast<typename Policy::value_type *>(i)); }
  };



  struct data_store::data_store_private
  {
    afio::dispatcher_ptr _dispatcher;
    afio::handle_ptr _small_blob_store_append, _small_blob_store, _large_blob_store, _large_blob_store_ecc, _index_store_appned, _index_store;
  };

  blob_reference::~blob_reference()
  {
    // TODO: Could stop this blob's age being refreshed to keep it from being garbage collected
  }

  future<void> blob_reference::load(buffers_type buffers)
  {
    return future<void>();
  }

  //future<std::shared_ptr<const_buffers_type>> blob_reference::map() noexcept

  data_store::data_store(size_t flags, filesystem::path path) : p(new data_store_private)
  {
    // Make a dispatcher for the local filesystem URI, masking out write flags on all operations if not writeable
    p->_dispatcher=afio::make_dispatcher("file:///", afio::file_flags::none, !(flags & writeable) ? afio::file_flags::write : afio::file_flags::none).get();
    // Set the dispatcher for this thread, and create/open a handle to the store directory
    afio::current_dispatcher_guard h(p->_dispatcher);
    auto dirh(afio::dir(std::move(path), afio::file_flags::create));  // throws if there was an error

    // The small blob store keeps non-memory mappable blobs at 32 byte alignments
    p->_small_blob_store_append=afio::file(dirh, "small_blob_store", afio::file_flags::create | afio::file_flags::append);  // throws if there was an error
    p->_small_blob_store=afio::file(dirh, "small_blob_store", afio::file_flags::read_write);          // throws if there was an error
    // Is this store just created?
    if(!p->_small_blob_store_append->lstat(afio::metadata_flags::size).st_size)
    {
      char buffer[96];
      memset(buffer, 0, sizeof(buffer));
      ondisk::file_header *header=(ondisk::file_header *) buffer;
      ondisk::blobindex_record *index=(ondisk::blobindex_record *)(buffer+32);
      header->length.value(32);
      header->index_offset_begin=32;
      header->time_count=0;
      index->thisoffset=32;
      index->bloboffset=0;
      index->header.do_hash(3/*index*/, 64, hash_kind_type::fast);
      // This is racy, but the file format doesn't care
      afio::write(p->_small_blob_store_append, buffer, 96, 0);
    }
#if 0
    // The large blob store keeps memory mappable blobs at 4Kb alignments
    // TODO

    // The index is where we keep the map of keys to blobs
    p->_index_store_append=afio::file(dirh, "index", afio::file_flags::create | afio::file_flags::append);  // throws if there was an error
    p->_index_store=afio::file(dirh, "index", afio::file_flags::read_write);          // throws if there was an error
    // Is this store just created?
    if(!p->_index_store_append->lstat(afio::metadata_flags::size).st_size)
    {
      ondisk_file_header header;
      header.length=32;
      header.index_offset_begin=32;
      header.time_count=0;
      // This is racy, but the file format doesn't care
      afio::write(_index_store_append, header, 0);
    }
#endif
  }

  future<blob_reference> data_store::store_blob(hash_kind_type hash_type, const_buffers_type buffers) noexcept
  {
    return future<blob_reference>();
  }

  future<blob_reference> data_store::find_blob(hash_kind_type hash_type, hash_value_type hash) noexcept
  {
    return future<blob_reference>();
  }

}



#if 0


//[workshop_final_ondisk_dense_hashmap4]
//]

//[workshop_final1]
namespace asio = BOOST_AFIO_V2_NAMESPACE::asio;
using BOOST_AFIO_V2_NAMESPACE::error_code;
using BOOST_AFIO_V2_NAMESPACE::generic_category;
using BOOST_OUTCOME_V1_NAMESPACE::outcome;

// A special allocator of highly efficient file i/o memory
using file_buffer_type = std::vector<char, afio::utils::page_allocator<char>>;

// Serialisation helper types
#pragma pack(push, 1)
struct ondisk_length  // 8 bytes  0xMMLLLLLLLLLLLLLLLLLLLLLLLLLLLLLK
{
  afio::off_t magic : 16;   // 0xad magic
  afio::off_t _length : 43; // 32 byte multiple size of the object (including this preamble, regions, key values) (+8)
  afio::off_t _spare : 3;
  afio::off_t kind : 2;     // 0 for zeroed space, 1,2 for blob, 3 for index
  afio::off_t value() const noexcept { return _length*32; }
  void value(afio::off_t v) noexcept
  {
    assert(!(v & 31));
    assert(v<(1ULL<<43));
    if((v & 31) || v>=(1ULL<<43))
      throw std::invalid_argument("Cannot store a non 32 byte multiple or a value greater than or equal to 2^43");
    _length=v/32;
  }
};
struct ondisk_file_header   // 20 bytes
{
  union
  {
    ondisk_length length;          // Always 32 bytes
    char endian[8];
  };
  afio::off_t index_offset_begin;  // Hint to the length of the store when the index was last written
  unsigned int time_count;         // Racy monotonically increasing count
};
struct ondisk_record_header  // 24 bytes - ALWAYS ALIGNED TO 32 BYTE FILE OFFSET
{
  ondisk_length length;      // (+8)
  uint64 hash[2];            // 128-bit SpookyHash of the object (from below onwards) (+28)
  // Potentially followed by one of:
  //   - ondisk_small_blob
  //   - ondisk_large_blob
  //   - ondisk_index
};
struct ondisk_small_blob     // 8 bytes + length
{
  afio::off_t length;        // Exact byte length of this small blob
  char data[1];
};
struct ondisk_large_blob     // 8 bytes + 16 bytes * extents
{
  afio::off_t length;        // Exact byte length of this large blob
  struct { afio::off_t offset, afio::off_t length } pages[1];  // What in the large blob store makes up this blob
};
template<class Policy> struct ondisk_index
{
  afio::off_t thisoffset;     // this index only valid if stored at this offset
  afio::off_t bloboffset;     // offset of the blob index we are using as a base
  ondisk_dense_hashmap<Policy> map;
};
#pragma pack(pop)

template<class Policy> index
{
  // Smart pointer to a dense hash map which can work directly from a read-only memory map
  dense_hashmap<Policy> current_map;
  afio::off_t offset_loaded_from;  // Offset this index was loaded from
  unsigned int last_time_count;    // Header time count
  index() : offset_loaded_from(0), last_time_count(0) { }
//]
//[workshop_final2]
  struct last_good_ondisk_index_info
  {
    afio::off_t offset;
    dense_hashmap<Policy> map;
    last_good_ondisk_index_info() : offset(0) { }
  };
  // Finds the last good index in the store
  outcome<last_good_ondisk_index_info> find_last_good_ondisk_index(afio::handle_ptr h) noexcept
  {
    last_good_ondisk_index_info ret;
    error_code ec;
    try
    {
      // Read the front of the store file to get the index offset hint
      ondisk_file_header header;
      afio::read(h, header, 0);
      afio::off_t file_length=h->lstat(afio::metadata_flags::size).st_size;
      afio::off_t offset=0;
      if(header.length==32)
      {
        offset=header.index_offset_begin;
        // Does the hint exceed the file length? If so, need to replay from the beginning
        if(offset>=file_length)
        {
          header.index_offset_begin=0;
          offset=32;
        }
        if(offset<32)
          offset=32;
      }
      else if(header.endian[0]==32)  // wrong endian
        return error_code(ENOEXEC, generic_category());
      else  // not one of our store files
        return error_code(ENOTSUP, generic_category());
      last_time_count=header.time_count;
      // Fetch the valid extents
      auto valid_extents(afio::extents(h));
      auto valid_extents_it=valid_extents.begin();
      // Iterate the records starting from index offset hint, keeping track of last known good index
      bool done=true;
      do
      {
        afio::off_t linear_scanning=0;
        auto finish_linear_scanning=[&linear_scanning, &h]
        {
          std::cerr << "NOTE: Found valid record after linear scan at " << offset << std::endl;
          std::cerr << "      Removing invalid data between " << linear_scanning << " and " << offset << std::endl;
          // Rewrite a valid record to span the invalid section
          ondisk_record_header temp={0};
          temp.length.magic=0xad;
          temp.length.value(offset-linear_scanning);
          temp.age=last_time_count;
          afio::write(ec, h, temp, linear_scanning);
          // Deallocate the physical storage for the invalid section
          afio::zero(ec, h, {{linear_scanning+12, offset-linear_scanning-12}});
          linear_scanning=0;
        };
        char buffer[sizeof(ondisk_record_header)+sizeof(ondisk_index)-sizeof(typename Policy::value_type)];
        ondisk_record_header *record=(ondisk_record_header *) buffer;
        // Iterate until end of the file, refreshing end of the file at the end of the file
        for(; (offset==file_length ? file_length=h->lstat(afio::metadata_flags::size).st_size : 0), offset<file_length;)
        {
          assert(!(offset & 31));
          // Find my valid extent
          while(offset>=valid_extents_it->first+valid_extents_it->second)
          {
            if(valid_extents.end()==++valid_extents_it)
            {
              valid_extents=afio::extents(h);
              valid_extents_it=valid_extents.begin();
            }
          }
          // Is this offset within a valid extent? If not, bump it.
          if(offset<valid_extents_it->first)
            offset=valid_extents_it->first;
          afio::read(ec, h, buffer, sizeof(buffer), offset);
          if(ec) return ec;
          // If this does not contain a valid record, linear scan
          // until we find one
          if(record->magic!=0xad)
          {
start_linear_scan:
            if(!linear_scanning)
            {
              std::cerr << "WARNING: Corrupt record detected at " << offset << ", linear scanning ..." << std::endl;
              linear_scanning=offset;
            }
            offset+=32;
            continue;
          }
          // Is this the first good record after a corrupted section?
          if(linear_scanning)
            finish_linear_scanning();
          // If not an index, skip entire record
          if(record->kind!=3 /*index*/)
          {
            // If this record length exceeds the file length, start a linear scan
            if(record->length.value()>file_length-offset)
            {
              file_length=h->lstat(afio::metadata_flags::size).st_size;
              if(record->length.value()>file_length-offset)
                goto start_linear_scan;
            }
            else
              offset+=record->length.value();
            continue;
          }
          ondisk_index *index=(ondisk_index *)(buffer+sizeof(ondisk_record_header));
          // Is this index canonical?
          if(index->thisoffset==offset)
          {
            // It is valid? This will mmap the index if it's big enough. This code should scale
            // up to billions of entries easily.
            afio::off_t offsetdiff=sizeof(ondisk_record_header)+16;
            dense_hashmap<Policy> map(h, offset+offsetdiff, record->length.value()-offsetdiff);
            uint64 hash[2]={0, 0};
            SpookyHash::Hash128(buffer, (size_t) offsetdiff, hash, hash+1);
            auto raw_buffer(map.raw_buffer());
            SpookyHash::Hash128(raw_buffer.first, raw_buffer.second, hash, hash+1);
            // Is this record correct?
            if(!memcmp(hash, record->hash, sizeof(hash)))
            {
              // Store as latest good known
              ret.map=std::move(map);
              ret.offset=offset;
            }
          }
          offset+=record->length.value();
        }
        // End of file may be garbage, if so zero the lot
        if(linear_scanning)
          finish_linear_scanning();
        if(ret.offset)  // we have a valid most recent index so we're done
          done=true;
        else if(header.index_offset_begin>32)
        {
          // Looks like the end of the store got trashed.
          // Reparse from the very beginning
          offset=32;
          header.index_offset_begin=0;
        }
        else
        {
          // No viable records at all, or empty store.
          done=true;
        }
      } while(!done);
      return ret;
    }
    catch(...)
    {
      return std::current_exception();
    }
  }
//]
//[workshop_final3]
  // Loads the index from a store
  outcome<void> load(afio::handle_ptr h) noexcept
  {
    // If find_last_good_ondisk_index() returns error or exception, return those, else
    // initialise ondisk_index_info to monad.get()
    BOOST_OUTCOME_AUTO(ondisk_index_info, find_last_good_ondisk_index(h));
    current_index=std::move(ondisk_index_info.buffer);
    offset_loaded_from=ondisk_index_info.offset;
    return {};
  }
//]
//[workshop_final4]
  // Writes the index to the store
  outcome<void> store(afio::handle_ptr rwh, afio::handle_ptr appendh) noexcept
  {
    error_code ec;
    std::vector<ondisk_index_regions::ondisk_index_region> ondisk_regions;
    ondisk_regions.reserve(65536);
    for(auto &i : regions)
    {
      ondisk_index_regions::ondisk_index_region r;
      r.offset=i.offset;
      r.r.kind=i.kind;
      r.r.length=i.length;
      memcpy(r.r.hash, i.hash, sizeof(i.hash));
      ondisk_regions.push_back(r);
    }

    size_t bytes=0;
    for(auto &i : key_to_region)
      bytes+=8+i.first.size();
    std::vector<char> ondisk_key_values(bytes);
    ondisk_index_key_value *kv=(ondisk_index_key_value *) ondisk_key_values.data();
    for(auto &i : key_to_region)
    {
      kv->region_index=(unsigned int) i.second;
      kv->name_size=(unsigned int) i.first.size();
      memcpy(kv->name, i.first.c_str(), i.first.size());
      kv=(ondisk_index_key_value *)(((char *) kv) + 8 + i.first.size());
    }

    struct
    {
      ondisk_record_header header;
      ondisk_index_regions header2;
    } h;
    h.header.magic=0xad;
    h.header.kind=3;  // writing an index
    h.header._spare=0;
    h.header.length=sizeof(h.header)+sizeof(h.header2)+sizeof(ondisk_regions.front())*(regions.size()-1)+bytes;
    h.header.age=last_time_count;
    // hash calculated later
    // thisoffset calculated later
    h.header2.regions_size=(unsigned int) regions.size();
    // Pad zeros to 32 byte multiple
    std::vector<char> zeros((h.header.length+31)&~31ULL);
    h.header.length+=zeros.size();

    // Create the gather buffer sequence
    std::vector<asio::const_buffer> buffers(4);
    buffers[0]=asio::const_buffer(&h, 36);
    buffers[1]=asio::const_buffer(ondisk_regions.data(), sizeof(ondisk_regions.front())*ondisk_regions.size());
    buffers[2]=asio::const_buffer(ondisk_key_values.data(), ondisk_key_values.size());
    if(zeros.empty())
      buffers.pop_back();
    else
      buffers[3]=asio::const_buffer(zeros.data(), zeros.size());
    file_buffer_type reread(sizeof(h));
    ondisk_record_header *reread_header=(ondisk_record_header *) reread.data();
    bool success=false;
    do
    {
      // Is this index stale?
      BOOST_OUTCOME_AUTO(ondisk_index_info, find_last_good_ondisk_index(rwh));
      if(ondisk_index_info.offset!=offset_loaded_from)
      {
        // A better conflict resolution strategy might check to see if deltas
        // are compatible, but for the sake of brevity we always report conflict
        return error_code(EDEADLK, generic_category());
      }
      // Take the current length of the store file. Any index written will be at or after this.
      h.header2.thisoffset=appendh->lstat(afio::metadata_flags::size).st_size;
      memset(h.header.hash, 0, sizeof(h.header.hash));
      // Hash the end of the first gather buffer and all the remaining gather buffers
      SpookyHash::Hash128(asio::buffer_cast<const char *>(buffers[0])+24, asio::buffer_size(buffers[0])-24, h.header.hash, h.header.hash+1);
      SpookyHash::Hash128(asio::buffer_cast<const char *>(buffers[1]),    asio::buffer_size(buffers[1]),    h.header.hash, h.header.hash+1);
      SpookyHash::Hash128(asio::buffer_cast<const char *>(buffers[2]),    asio::buffer_size(buffers[2]),    h.header.hash, h.header.hash+1);
      if(buffers.size()>3)
        SpookyHash::Hash128(asio::buffer_cast<const char *>(buffers[3]),    asio::buffer_size(buffers[3]),    h.header.hash, h.header.hash+1);
      // Atomic append the record
      afio::write(ec, appendh, buffers, 0);
      if(ec) return ec;
      // Reread the record
      afio::read(ec, rwh, reread.data(), reread.size(), h.header2.thisoffset);
      if(ec) return ec;
      // If the record doesn't match it could be due to a lag in st_size between open handles,
      // so retry until success or stale index
    } while(memcmp(reread_header->hash, h.header.hash, sizeof(h.header.hash)));

    // New index has been successfully written. Update the hint at the front of the file.
    // This update is racy of course, but as it's merely a hint we don't care.
    ondisk_file_header file_header;
    afio::read(ec, rwh, file_header, 0);
    if(!ec && file_header.index_offset_begin<h.header2.thisoffset)
    {
      file_header.index_offset_begin=h.header2.thisoffset;
      file_header.time_count++;
      afio::write(ec, rwh, file_header, 0);
    }
    offset_loaded_from=h.header2.thisoffset;
    last_time_count=file_header.time_count;
    return {};
  }
//]
//[workshop_final5]
  // Reloads the index if needed
  outcome<void> refresh(afio::handle_ptr h) noexcept
  {
    static afio::off_t last_size;
    error_code ec;
    afio::off_t size=h->lstat(afio::metadata_flags::size).st_size;
    // Has the size changed? If so, need to check the hint
    if(size>last_size)
    {
      last_size=size;
      ondisk_file_header header;
      afio::read(ec, h, header, 0);
      if(ec) return ec;
      // If the hint is moved, we are stale
      if(header.index_offset_begin>offset_loaded_from)
        return load(h);
    }
    return {};
  }
};
//]

//[workshop_final_interface
namespace afio = BOOST_AFIO_V2_NAMESPACE;
namespace filesystem = BOOST_AFIO_V2_NAMESPACE::filesystem;
using BOOST_MONAD_V1_NAMESPACE::monad;
using BOOST_MONAD_V1_NAMESPACE::lightweight_futures::shared_future;

class data_store
{
  struct _ostream;
  friend struct _ostream;
  afio::dispatcher_ptr _dispatcher;
  // The small blob store keeps non-memory mappable blobs at 32 byte alignments
  afio::handle_ptr _small_blob_store, _small_blob_store_append;
  // The large blob store keeps memory mappable blobs at 4Kb alignments
  afio::handle_ptr _large_blob_store, _large_blob_store_append, _large_blob_store_ecc;
  // The index is where we keep the map of keys to blobs
  afio::handle_ptr _index_store, _index_store_append;
  // Our current indices for the index and blob store
  index<StringKeyPolicy> _index;
  index<BlobKeyPolicy> _blob_store_index;
public:
  // Type used for read streams
  using istream = std::shared_ptr<std::istream>;
  // Type used for write streams
  using ostream = std::shared_ptr<std::ostream>;
  // Type used for lookup
  using lookup_result_type = shared_future<istream>;
  // Type used for write
  using write_result_type = monad<ostream>;

  // Disposition flags
  static constexpr size_t writeable = (1<<0);

  // Open a data store at path
  data_store(size_t flags = 0, afio::path path = "store");
  
  // Look up item named name for reading, returning an istream for the item
  shared_future<istream> lookup(std::string name) noexcept;
  // Look up item named name for writing, returning an ostream for that item
  monad<ostream> write(std::string name) noexcept;
};
//]



//[workshop_final6]
namespace asio = BOOST_AFIO_V2_NAMESPACE::asio;
using BOOST_AFIO_V2_NAMESPACE::error_code;
using BOOST_AFIO_V2_NAMESPACE::generic_category;

// A special allocator of highly efficient file i/o memory
using file_buffer_type = std::vector<char, afio::utils::page_allocator<char>>;

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
//]
//[workshop_final7]
// An iostream which buffers all the output, then commits on destruct
struct data_store::_ostream : public std::ostream
{
  struct ostreambuf : public std::streambuf
  {
    using int_type = std::streambuf::int_type;
    using traits_type = std::streambuf::traits_type;
    data_store *ds;
    std::string name;
    file_buffer_type buffer;
    ostreambuf(data_store *_ds, std::string _name) : ds(_ds), name(std::move(_name)), buffer(afio::utils::page_sizes().front())
    {
      // Set the put buffer this streambuf is to use
      setp(buffer.data(), buffer.data() + buffer.size());
    }
    virtual ~ostreambuf() override
    {
      try
      {
        ondisk_index_regions::ondisk_index_region r;
        r.r.magic=0xad;
        r.r.kind=1;  // small blob
        r.r.length=sizeof(r.r)+buffer.size();
        r.r.length=(r.r.length+31)&~31ULL;  // pad to 32 byte multiple
        r.r.age=ds->_index->last_time_count;
        memset(r.r.hash, 0, sizeof(r.r.hash));
        SpookyHash::Hash128(buffer.data(), (size_t)(r.r.length-sizeof(r.r)), r.r.hash, r.r.hash+1);

        // Create the gather buffer sequence and atomic append the blob
        std::vector<asio::const_buffer> buffers(2);
        buffers[0]=asio::const_buffer((char *) &r.r, sizeof(r.r));
        buffers[1]=asio::const_buffer(buffer.data(), (size_t)(r.r.length-sizeof(r.r)));
        error_code ec;
        auto offset=ds->_small_blob_store_append->lstat(afio::metadata_flags::size).st_size;
        afio::write(ec, ds->_small_blob_store_append, buffers, 0);
        if(ec)
          abort();  // should really do something better here

        // Find out where my blob ended up
        ondisk_record_header header;
        do
        {
          afio::read(ec, ds->_small_blob_store_append, header, offset);
          if(ec) abort();
          if(header.kind==1 /*small blob*/ && !memcmp(header.hash, r.r.hash, sizeof(header.hash)))
          {
            r.offset=offset;
            break;
          }
          offset+=header.length;
        } while(offset<ds->_small_blob_store_append->lstat(afio::metadata_flags::size).st_size);

        for(;;)
        {
          // Add my blob to the regions
          ds->_index->regions.push_back(&r);
          // Add my key to the key index
          ds->_index->key_to_region[name]=ds->_index->regions.size()-1;
          // Commit the index, and if successful exit
          if(!ds->_index->store(ds->_index_store, ds->_index_store_append))
            return;
          // Reload the index and retry
          if(ds->_index->load(ds->_index_store))
            abort();
        }
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
      buffer.resize(buffer.size()*2);
      setp(buffer.data() + buffer.size()/2, buffer.data() + buffer.size());
      return 0;
    }
  };
  std::unique_ptr<ostreambuf> buf;
  _ostream(data_store *ds, std::string name) : std::ostream(new ostreambuf(ds, std::move(name))), buf(static_cast<ostreambuf *>(rdbuf()))
  {
  }
  virtual ~_ostream() override
  {
    // Reset the stream before deleting the buffer
    rdbuf(nullptr);
  }
};
//]

//[workshop_final8]
data_store::data_store(size_t flags, afio::path path)
{
  // Make a dispatcher for the local filesystem URI, masking out write flags on all operations if not writeable
  _dispatcher=afio::make_dispatcher("file:///", afio::file_flags::none, !(flags & writeable) ? afio::file_flags::write : afio::file_flags::none).get();
  // Set the dispatcher for this thread, and create/open a handle to the store directory
  afio::current_dispatcher_guard h(_dispatcher);
  auto dirh(afio::dir(std::move(path), afio::file_flags::create));  // throws if there was an error

  // The small blob store keeps non-memory mappable blobs at 32 byte alignments
  _small_blob_store_append=afio::file(dirh, "small_blob_store", afio::file_flags::create | afio::file_flags::append);  // throws if there was an error
  _small_blob_store=afio::file(dirh, "small_blob_store", afio::file_flags::read_write);          // throws if there was an error
  _small_blob_store_ecc=afio::file(dirh, "small_blob_store.ecc", afio::file_flags::create | afio::file_flags::read_write);  // throws if there was an error

  // The large blob store keeps memory mappable blobs at 4Kb alignments
  // TODO

  // The index is where we keep the map of keys to blobs
  _index_store_append=afio::file(dirh, "index", afio::file_flags::create | afio::file_flags::append);  // throws if there was an error
  _index_store=afio::file(dirh, "index", afio::file_flags::read_write);          // throws if there was an error
  _index_store_ecc=afio::file(dirh, "index.ecc", afio::file_flags::create | afio::file_flags::read_write);  // throws if there was an error
  // Is this store just created?
  if(!_index_store_append->lstat(afio::metadata_flags::size).st_size)
  {
    ondisk_file_header header;
    header.length=32;
    header.index_offset_begin=32;
    header.time_count=0;
    // This is racy, but the file format doesn't care
    afio::write(_index_store_append, header, 0);
  }
  _index.reset(new index);
}

shared_future<data_store::istream> data_store::lookup(std::string name) noexcept
{
  try
  {
    BOOST_OUTCOME_PROPAGATE(_index->refresh(_index_store));
    auto it=_index->key_to_region.find(name);
    if(_index->key_to_region.end()==it)
      return error_code(ENOENT, generic_category());  // not found
    auto &r=_index->regions[it->second];
    afio::off_t offset=r.offset+24, length=r.length-24;
    // Schedule the reading of the file into a buffer
    auto buffer=std::make_shared<file_buffer_type>((size_t) length);
    afio::future<> h(afio::async_read(_small_blob_store, buffer->data(), (size_t) length, offset));
    // When the read completes call this continuation
    return h.then([buffer, length](const afio::future<> &h) -> shared_future<data_store::istream> {
      // If read failed, return the error or exception immediately
      BOOST_OUTCOME_PROPAGATE(h);
      data_store::istream ret(std::make_shared<idirectstream>(h.get_handle(), buffer, (size_t) length));
      return ret;
    });
  }
  catch(...)
  {
    return std::current_exception();
  }
}

outcome<data_store::ostream> data_store::write(std::string name) noexcept
{
  try
  {
    return std::make_shared<_ostream>(this, std::move(name));
  }
  catch (...)
  {
    return std::current_exception();
  }
}
//]
#endif
