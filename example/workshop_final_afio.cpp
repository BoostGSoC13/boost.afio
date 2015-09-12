#include "test_functions.hpp"
#include "workshop_final_afio.ipp"

BOOST_AFIO_AUTO_TEST_CASE(workshop_blob_store_load, "Tests that one can store and find blobs", 5)
{
  using namespace BOOST_AFIO_V2_NAMESPACE;
  using namespace transactional_key_store;
  filesystem::remove_all("store");
  data_store ds(data_store::writeable);
  std::string a("Niall"), b("Douglas"), c("Clara");
  auto aref = ds.store_blob(hash_kind::fast, { {a.c_str(), a.size()} }).get();
  auto bref = ds.store_blob(hash_kind::fast, { {b.c_str(), b.size()} }).get();
  auto cref = ds.store_blob(hash_kind::fast, { {c.c_str(), c.size()} }).get();
  BOOST_CHECK(aref.size() == a.size());
  BOOST_CHECK(bref.size() == b.size());
  BOOST_CHECK(cref.size() == c.size());

  auto _aref = ds.find_blob(hash_kind::fast, aref.hash_value()).get();
  auto _bref = ds.find_blob(hash_kind::fast, bref.hash_value()).get();
  auto _cref = ds.find_blob(hash_kind::fast, cref.hash_value()).get();
  BOOST_CHECK(aref.hash_value() == _aref.hash_value());
  BOOST_CHECK(bref.hash_value() == _bref.hash_value());
  BOOST_CHECK(cref.hash_value() == _cref.hash_value());
}
