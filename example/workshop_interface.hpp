//[workshop_interface
namespace transactional_key_store
{
  namespace afio = BOOST_AFIO_V2_NAMESPACE;
  namespace filesystem = BOOST_AFIO_V2_NAMESPACE::filesystem;
  using BOOST_OUTCOME_V1_NAMESPACE::outcome;
  using BOOST_OUTCOME_V1_NAMESPACE::lightweight_futures::future;

  class data_store;
  //! The type of the hash
  struct hash_value_type
  {
    union
    {
      unsigned char _uint8[16];
      unsigned long long int _uint64[2];
    };
    constexpr hash_value_type() noexcept : _uint64{ 0, 0 } {}
    constexpr bool operator==(const hash_value_type &o) const noexcept { return _uint64[0] == o._uint64[0] && _uint64[1] == o._uint64[1]; }
    constexpr bool operator!=(const hash_value_type &o) const noexcept { return _uint64[0] != o._uint64[0] || _uint64[1] != o._uint64[1]; }
  };
  //! The kind of hash used
  enum class hash_kind : unsigned char
  {
    unknown = 0,
    fast,        //!< We are using SpookyHash (0.3 cycles/byte)
    quality      //!< We are using Blake2b      (3 cycles/byte)
  };
  //! Scatter buffers type
  using buffers_type = std::vector<std::pair<char *, size_t>>;
  //! Gather buffers type
  using const_buffers_type = std::vector<std::pair<const char *, size_t>>;

  //! By-hash reference to a blob
  class blob_reference
  {
    friend class data_store;
    data_store &_ds;
    hash_kind _hash_kind;
    hash_value_type _hash;
    size_t _length;
    std::shared_ptr<const_buffers_type> _mapping;
    constexpr blob_reference(data_store &ds) noexcept : _ds(ds), _hash_kind(hash_kind::unknown), _length(0) { }
    constexpr blob_reference(data_store &ds, hash_kind hash_type, hash_value_type hash, size_t length) noexcept : _ds(ds), _hash_kind(hash_type), _hash(hash), _length(length) { }
  public:
    ~blob_reference();

    //! True if reference is valid
    explicit operator bool() const noexcept { return _hash_kind != hash_kind::unknown; }
    //! True if reference is not valid
    bool operator !() const noexcept { return _hash_kind == hash_kind::unknown; }

    //! Kind of hash used
    constexpr hash_kind hash_kind() const noexcept { return _hash_kind; }
    //! Hash value
    constexpr hash_value_type hash_value() const noexcept { return _hash; }
    //! Length of blob
    constexpr size_t size() const noexcept { return _length; }

    //! Reads the blob into the supplied scatter buffers. Throws invalid_argument if all the buffers sizes are less than blob size.
    future<void> load(buffers_type buffers);

    //! Maps the blob into memory, returning a shared set of scattered buffers
    future<std::shared_ptr<const_buffers_type>> map() noexcept;
  };

  class transaction;
  //! Implements a late durable ACID key-value blob store
  class data_store
  {
    struct data_store_private;
    std::unique_ptr<data_store_private> p;
  public:
    //! This store is to be modifiable
    static constexpr size_t writeable = (1 << 0);
    //! Index updates should not complete until on physical storage
    static constexpr size_t immediately_durable_index = (1 << 1);
    //! Blob stores should not complete until on physical storage
    static constexpr size_t immediately_durable_blob = (1 << 2);

    //! Open a data store at path with disposition flags
    data_store(size_t flags = 0, filesystem::path path = "store");

    //! Store a blob
    future<blob_reference> store_blob(hash_kind hash_type, const_buffers_type buffers) noexcept;

    //! Find a blob
    future<blob_reference> find_blob(hash_kind hash_type, hash_value_type hash) noexcept;
  private:
    friend class transaction;
    struct index_base
    {
      virtual ~index_base() {}
      virtual blob_reference lookup(std::string key)=0;
    };
    using index_ptr = std::shared_ptr<index_base>;
    index_ptr latest_index();
  };

  //! Potentional transaction commit outcomes
  enum class transaction_status
  {
    success = 0,  //!< The transaction was successfully committed
    conflict,   //!< This transaction conflicted with another transaction (index is stale)
    stale       //!< One or more blob references could not be found (perhaps blob is too old)
  };
  //! A transaction object
  class transaction
  {
    data_store &_ds;
    data_store::index_ptr index_ptr;
    std::vector<std::tuple<std::string, blob_reference, bool>> _operations;
  public:
    //! Begins a transaction with the data store
    constexpr transaction(data_store &ds) : _ds(ds) { }

    //! Look up a key
    blob_reference lookup(std::string key)
    {
      if (!index_ptr)
        index_ptr = _ds.latest_index();
      blob_reference ret = index_ptr->lookup(key);
      if (!ret)
        return ret;
      _operations.push_back(std::make_tuple(std::move(key), ret, false));
      return ret;
    }

    //! Adds an update to be part of the transaction
    void add(std::string key, blob_reference value)
    {
      if (!index_ptr)
        index_ptr = _ds.latest_index();
      _operations.push_back(std::make_tuple(std::move(key), std::move(value), true));
    }

    //! Commits the transaction, returning outcome
    future<transaction_status> commit() noexcept;
  };
}
//]
