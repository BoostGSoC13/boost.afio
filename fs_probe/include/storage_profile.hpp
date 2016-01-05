/* storage_profile.hpp
A profile of an OS and filing system
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

#ifndef BOOST_AFIO_STORAGE_PROFILE_H
#define BOOST_AFIO_STORAGE_PROFILE_H

#include "handle.hpp"

BOOST_AFIO_V2_NAMESPACE_BEGIN

namespace storage_profile
{
  //! Types potentially storable in a storage profile
  enum class storage_types
  {
    unknown,
    extent_type,
    unsigned_int,
    float_,
    string
  };
  struct storage_profile;

  //! Returns the enum matching type T
  template<class T> constexpr storage_types map_to_storage_type() { static_assert(0 == sizeof(T), "Unsupported storage_type"); return storage_types::unknown; }
  //! Specialise for a different default value for T
  template<class T> constexpr T default_value() { return T{}; }

  template<> constexpr storage_types map_to_storage_type<io_service::extent_type>() { return storage_types::extent_type; }
  template<> constexpr io_service::extent_type default_value<io_service::extent_type>() { return (io_service::extent_type) - 1; }
  template<> constexpr storage_types map_to_storage_type<unsigned int>() { return storage_types::unsigned_int; }
  template<> constexpr storage_types map_to_storage_type<float>() { return storage_types::float_; }
  template<> constexpr storage_types map_to_storage_type<std::string>() { return storage_types::string; }

  //! Common base class for items
  struct item_base
  {
    static constexpr size_t item_size = 128;

    const char *name;         //!< The name of the item in colon delimited category format
    const char *description;  //!< Some description of the item
    storage_types type;       //!< The type of the value
  protected:
    constexpr item_base(const char *_name, const char *_desc, storage_types _type) : name(_name), description(_desc), type(_type)
    {
    }
  };
  //! A tag-value item in the storage profile where T is the type of value stored.
  template<class T> struct item : public item_base
  {
    static constexpr size_t item_size = item_base::item_size;
    using callable = outcome<void>(*)(storage_profile &sp, handle &h);

    callable impl;
    T value;                  //!< The storage of the item
    char _padding[item_size - sizeof(item_base) - sizeof(callable) - sizeof(T)];
    constexpr item(const char *_name, callable c, const char *_desc = nullptr, T _value = default_value<T>()) : item_base(_name, _desc, map_to_storage_type<T>()), impl(c), value(_value), _padding{ 0 }
    {
      static_assert(sizeof(*this) == item_size, "");
    }
    //! Clear this item, returning value to default
    void clear() { value = default_value<T>(); }
    //! Set this item if its value is default
    outcome<void> operator()(storage_profile &sp) const
    {
      if (value != default_value<T>())
        return make_outcome<void>();
      if (!sp.handle())
        return make_errored_outcome<void>(EINVAL);
      return impl(sp, *sp.handle());
    }
  };
  //! A type erased tag-value item
  struct item_erased : public item_base
  {
    item_erased() = delete;
    ~item_erased() = delete;
    item_erased(const item_erased &) = delete;
    item_erased(item_erased &&) = delete;
    item_erased &operator=(const item_erased &) = delete;
    item_erased &operator=(item_erased &&) = delete;
    //! Call the callable with the unerased type
    template<class U> auto invoke(U &&f) const
    {
      switch (type)
      {
      case storage_types::extent_type:
        return f(*static_cast<const item<io_service::extent_type> *>(static_cast<const item_base *>(this)));
      case storage_types::unsigned_int:
        return f(*static_cast<const item<unsigned int> *>(static_cast<const item_base *>(this)));
      case storage_types::float_:
        return f(*static_cast<const item<float> *>(static_cast<const item_base *>(this)));
      case storage_types::string:
        return f(*static_cast<const item<std::string> *>(static_cast<const item_base *>(this)));
      case storage_types::unknown:
        break;
      }
      throw std::invalid_argument("No type set in item");
    }
    //! Set this item if its value is default
    outcome<void> operator()(storage_profile &sp) const
    {
      return invoke([&sp](auto &item) {
        return item(sp);
      });
    }
  };

  namespace system
  {
    // OS name, version
    BOOST_AFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> os(storage_profile &sp, handle &h) noexcept;
    // CPU name, architecture, physical cores
    BOOST_AFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> cpu(storage_profile &sp, handle &h) noexcept;
    // System memory quantity, in use, max and min bandwidth
    BOOST_AFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> mem(storage_profile &sp, handle &h) noexcept;
#ifdef WIN32
    namespace windows {
#else
    namespace posix {
#endif
      BOOST_AFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> _mem(storage_profile &sp, handle &h) noexcept;
    }
  }
  namespace storage
  {
    // Device name, size, min i/o size
    BOOST_AFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> device(storage_profile &sp, handle &h) noexcept;
    // FS name, config, size, in use
    BOOST_AFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> fs(storage_profile &sp, handle &h) noexcept;
  }
  namespace concurrency
  {
    BOOST_AFIO_HEADERS_ONLY_FUNC_SPEC outcome<void> atomic_write_quantum(storage_profile &sp, handle &h) noexcept;
  }

  //! A (possibly incomplet) profile of storage
  struct BOOST_AFIO_DECL storage_profile
  {
    using size_type = size_t;
    using handle_type = handle;
  private:
    size_type _size;
    handle *_h;
  public:
    constexpr storage_profile() : _size(0), _h(nullptr) { }
    /*! Constructs a profile of the storage referred to by the handle.
    Note that the handle needs to not move in memory until all profiling
    is done.
    */
    constexpr storage_profile(handle &h) : _size(0), _h(&h) {}

    //! Returns a pointer to the handle used by this profile for profiling
    handle_type *handle() const noexcept { return _h; }
    //! Sets the pointer to the handle used by this profile for profiling
    handle_type *handle(handle_type *n) noexcept { handle_type *o = _h; _h = n; return o; }

    //! Value type
    using value_type = item_erased &;
    //! Reference type
    using reference = item_erased &;
    //! Const reference type
    using const_reference = const item_erased &;
    //! Iterator type
    using iterator = item_erased *;
    //! Const iterator type
    using const_iterator = const item_erased *;

    //! True if this storage profile is empty
    bool empty() const noexcept { return _size == 0; }
    //! Items in this storage profile
    size_type size() const noexcept { return _size; }
    //! Potential items in this storage profile
    size_type max_size() const noexcept { return (sizeof(*this) - sizeof(_size)) / item_base::item_size; }
    //! Returns an iterator to the first item
    iterator begin() noexcept { return static_cast<item_erased *>(static_cast<item_base *>(&os_name)); }
    //! Returns an iterator to the last item
    iterator end() noexcept { return begin()+max_size(); }
    //! Returns an iterator to the first item
    const_iterator begin() const noexcept { return static_cast<const item_erased *>(static_cast<const item_base *>(&os_name)); }
    //! Returns an iterator to the last item
    const_iterator end() const noexcept { return begin() + max_size(); }

    //! Read the storage profile from in as YAML
    void read(std::istream &in);
    //! Write the storage profile as YAML to out with the given indentation
    void write(std::ostream &out, size_t _indent = 0) const;

    // System characteristics
    item<std::string> os_name = { "system:os:name", &system::os };                            // e.g. Microsoft Windows NT
    item<std::string> os_ver = { "system:os:ver", &system::os };                              // e.g. 10.0.10240
    item<std::string> cpu_name = { "system:cpu:name", &system::cpu };                         // e.g. Intel Haswell
    item<std::string> cpu_architecture = { "system:cpu:architecture", &system::cpu };         // e.g. x64
    item<unsigned> cpu_physical_cores = { "system:cpu:physical_cores", &system::cpu };
    item<unsigned> mem_quantity = { "system:mem:quantity", &system::mem };
    item<float> mem_in_use = { "system:mem:in_use", &system::mem };                           // not including caches etc.
    item<unsigned> mem_max_bandwidth = { "system:mem:max_bandwidth", &system::mem };          // of main memory (sequentially accessed)
    item<unsigned> mem_min_bandwidth = { "system:mem:min_bandwidth", &system::mem };          // of main memory (randomly accessed in 4Kb chunks, not sequentially)

    // Storage characteristics
    item<std::string> device_name = { "storage:device:name", &storage::device };              // e.g. WDC WD30EFRX-68EUZN0
    item<unsigned> device_min_io_size = { "storage:device:min_io_size", &storage::device };   // e.g. 4096
    item<io_service::extent_type> device_size = { "storage:device:size", &storage::device };

    // Filing system characteristics
    item<std::string> fs_name = { "storage:fs:name", &storage::fs };
    item<std::string> fs_config = { "storage:fs:config", &storage::fs };  // POSIX mount options, ZFS pool properties etc
//        item<std::string> fs_ffeatures = { "storage:fs:features" };  // Standardised features???
    item<io_service::extent_type> fs_size = { "storage:fs:size", &storage::fs };
    item<float> fs_in_use = { "storage:fs:in_use", &storage::fs };

    // Test results on this filing system, storage and system
    item<io_service::extent_type> atomic_write_quantum = {
      "concurrency:atomic_write_quantum",
      concurrency::atomic_write_quantum,
      "The i/o write quantum guaranteed to be atomically visible to readers irrespective of write quantity"
    };
    item<io_service::extent_type> max_atomic_write = {
      "concurrency::max_atomic_write",
      concurrency::atomic_write_quantum,
      "The maximum single i/o write quantity atomically visible to readers"
    };
  };
}

BOOST_AFIO_V2_NAMESPACE_END

#if BOOST_AFIO_HEADERS_ONLY == 1 && !defined(DOXYGEN_SHOULD_SKIP_THIS)
#define BOOST_AFIO_INCLUDED_BY_HEADER 1
#include "detail/impl/storage_profile.ipp"
#undef BOOST_AFIO_INCLUDED_BY_HEADER
#endif

#endif
