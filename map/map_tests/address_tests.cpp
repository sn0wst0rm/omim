#include "testing/testing.hpp"

#include "search/reverse_geocoder.hpp"

#include "indexer/classificator_loader.hpp"
#include "indexer/index.hpp"
#include "indexer/search_string_utils.hpp"


using namespace search;
using namespace platform;

namespace
{

void TestAddress(ReverseGeocoder & coder, ms::LatLon const & ll,
                 string const & stName, string const & hNumber)
{
  ReverseGeocoder::Address addr;
  coder.GetNearbyAddress(MercatorBounds::FromLatLon(ll), addr);

  string key;
  GetStreetNameAsKey(addr.m_street.m_name, key);

  TEST_EQUAL(stName, key, (addr));
  TEST_EQUAL(hNumber, addr.m_building.m_name, (addr));
}

} // namespace

UNIT_TEST(ReverseGeocoder_Smoke)
{
  classificator::Load();

  LocalCountryFile file = LocalCountryFile::MakeForTesting("minsk-pass");

  Index index;
  TEST_EQUAL(index.RegisterMap(file).second, MwmSet::RegResult::Success, ());

  ReverseGeocoder coder(index);

  TestAddress(coder, {53.89815, 27.54265}, "мясникова", "32");
  TestAddress(coder, {53.89953, 27.54189}, "немига", "42");
  TestAddress(coder, {53.89666, 27.54904}, "советская", "19");
  TestAddress(coder, {53.89724, 27.54983}, "независимости", "11");
  TestAddress(coder, {53.89745, 27.55835}, "карламаркса", "18А");
}
