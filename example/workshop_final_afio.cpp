#include "test_functions.hpp"
#include "workshop_final_afio.ipp"

struct dump
{
  template<class T> dump(std::pair<const transactional_key_store::ondisk::dense_hashmap<T> *, size_t> d)
  {
    auto printable = [](char c) { if (c >= 32 && c < 127) return c; else return '.'; };
    char *c = (char *)d.first;
    unsigned *p = (unsigned *)d.first;
    std::cout << d.second << " bytes at " << d.first << ":" << std::endl;
    for (size_t n = 0; n < d.second; n += 4)
    {
      for (size_t m = 0; m < 4; m++)
        printf("%c", printable(c[n + m]));
      printf("  %0.8x\n", p[n / 4]);
    }
  }
};

BOOST_AFIO_AUTO_TEST_CASE(workshop_dense_hash_map_works, "Tests that the direct from disk dense hash map works", 5)
{
  using namespace BOOST_AFIO_V2_NAMESPACE;
  using namespace transactional_key_store;

  dense_hashmap<ondisk::StringKeyPolicy> string_map;
  string_map.insert(std::vector<std::pair<const char *, size_t>>{ {"cat", 5}, { "dog", 6}, {"horse", 7}, {"pig", 8}, {"sheep", 9} });
  dump(string_map.raw_buffer());
  BOOST_CHECK(string_map.find("cat")->value == 5);
  BOOST_CHECK(string_map.find("dog")->value == 6);
  BOOST_CHECK(string_map.find("horse")->value == 7);
  BOOST_CHECK(string_map.find("pig")->value == 8);
  BOOST_CHECK(string_map.find("sheep")->value == 9);
  BOOST_CHECK(string_map.find("niall") == nullptr);
  string_map.erase(string_map.find("cat"));
  string_map.erase(string_map.find("pig"));
  BOOST_CHECK(string_map.find("cat") == nullptr);
  BOOST_CHECK(string_map.find("dog")->value == 6);
  BOOST_CHECK(string_map.find("horse")->value == 7);
  BOOST_CHECK(string_map.find("pig") == nullptr);
  BOOST_CHECK(string_map.find("sheep")->value == 9);
  BOOST_CHECK(string_map.find("niall") == nullptr);
  string_map.insert(std::vector<std::pair<const char *, size_t>>{ {"niall", 1} });
  BOOST_CHECK(string_map.find("cat") == nullptr);
  BOOST_CHECK(string_map.find("dog")->value == 6);
  BOOST_CHECK(string_map.find("horse")->value == 7);
  BOOST_CHECK(string_map.find("pig") == nullptr);
  BOOST_CHECK(string_map.find("sheep")->value == 9);
  BOOST_CHECK(string_map.find("niall")->value == 1);
  dump(string_map.raw_buffer());

  dense_hashmap<ondisk::BlobKeyPolicy> blob_map;
  blob_map.insert(std::vector<std::pair<unsigned, afio::off_t>>{ {0, 1}, { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 5 } });
  dump(blob_map.raw_buffer());
  BOOST_CHECK(blob_map.find(0)->value == 1);
  BOOST_CHECK(blob_map.find(1)->value == 2);
  BOOST_CHECK(blob_map.find(2)->value == 3);
  BOOST_CHECK(blob_map.find(3)->value == 4);
  BOOST_CHECK(blob_map.find(4)->value == 5);
  BOOST_CHECK(blob_map.find(5) == nullptr);
  blob_map.erase(blob_map.find(0));
  blob_map.erase(blob_map.find(3));
  BOOST_CHECK(blob_map.find(0) == nullptr);
  BOOST_CHECK(blob_map.find(1)->value == 2);
  BOOST_CHECK(blob_map.find(2)->value == 3);
  BOOST_CHECK(blob_map.find(3) == nullptr);
  BOOST_CHECK(blob_map.find(4)->value == 5);
  BOOST_CHECK(blob_map.find(5) == nullptr);
  blob_map.insert(std::vector<std::pair<unsigned, afio::off_t>>{ {5, 6} });
  BOOST_CHECK(blob_map.find(0) == nullptr);
  BOOST_CHECK(blob_map.find(1)->value == 2);
  BOOST_CHECK(blob_map.find(2)->value == 3);
  BOOST_CHECK(blob_map.find(3) == nullptr);
  BOOST_CHECK(blob_map.find(4)->value == 5);
  BOOST_CHECK(blob_map.find(5)->value == 6);
  dump(blob_map.raw_buffer());
}

#if 0
BOOST_AFIO_AUTO_TEST_CASE(workshop_blob_store_load, "Tests that one can store and find blobs", 5)
{
  using namespace BOOST_AFIO_V2_NAMESPACE;
  using namespace transactional_key_store;
  filesystem::remove_all("store");
  data_store ds(data_store::writeable);
  std::string a("Niall"), b("Douglas"), c("Clara");
  auto aref = ds.store_blob(hash_kind_type::fast, { {a.c_str(), a.size()} }).get();
  auto bref = ds.store_blob(hash_kind_type::fast, { {b.c_str(), b.size()} }).get();
  auto cref = ds.store_blob(hash_kind_type::fast, { {c.c_str(), c.size()} }).get();
  BOOST_CHECK(aref.size() == a.size());
  BOOST_CHECK(bref.size() == b.size());
  BOOST_CHECK(cref.size() == c.size());

  auto _aref = ds.find_blob(hash_kind_type::fast, aref.hash_value()).get();
  auto _bref = ds.find_blob(hash_kind_type::fast, bref.hash_value()).get();
  auto _cref = ds.find_blob(hash_kind_type::fast, cref.hash_value()).get();
  BOOST_CHECK(aref.hash_value() == _aref.hash_value());
  BOOST_CHECK(bref.hash_value() == _bref.hash_value());
  BOOST_CHECK(cref.hash_value() == _cref.hash_value());
}
#endif

#if 0
BOOST_AFIO_AUTO_TEST_CASE(workshop_transaction, "Tests that one can issue transactions", 5)
{
  using namespace BOOST_AFIO_V2_NAMESPACE;
  using namespace transactional_key_store;
  filesystem::remove_all("store");

  data_store ds(data_store::writeable);
  std::string a("Niall"), b("Douglas"), c("Clara");
  blob_reference aref = ds.store_blob(hash_kind_type::fast, { { a.c_str(), a.size() } }).get();
  blob_reference bref = ds.store_blob(hash_kind_type::fast, { { b.c_str(), b.size() } }).get();
  blob_reference cref = ds.store_blob(hash_kind_type::fast, { { c.c_str(), c.size() } }).get();

  // Write our values as a single transaction
  transaction t1(ds);
  t1.add("niall", aref);
  t1.add("douglas", bref);
  t1.add("clara", aref);
  BOOST_REQUIRE(transaction_status::success==t1.commit().get());

  // Read modify write as a transaction
  transaction t2(ds);
  blob_reference dref = t2.lookup("niall");
  BOOST_CHECK(dref == aref);
  t2.add("clara", cref);
  BOOST_REQUIRE(transaction_status::success == t2.commit().get());

  // Make sure state is as it should be
  transaction t3(ds);
  BOOST_CHECK(t3.lookup("niall") == aref);
  BOOST_CHECK(t3.lookup("douglas") == bref);
  BOOST_CHECK(t3.lookup("clara") == cref);
}
#endif
