// Compile-only tests of the actual navigation, validation and cache-ownership rules.
#include "station_rules.hpp"
#include <array>
using namespace p4radio::station_rules;
constexpr std::array<bool, 6> enabled{true, false, false, true, false, true};
static_assert(adjacent(enabled, 0, 1) == 3);
static_assert(adjacent(enabled, 0, -1) == 5);
static_assert(adjacent(enabled, 5, 1) == 0);
static_assert(adjacent(enabled, 3, -1) == 0);
static_assert(adjacent(enabled, -1, 1) == 0);
static_assert(adjacent(enabled, -1, -1) == 5);
static_assert(adjacent(std::array<bool, 3>{false, false, false}, 1, 1) == -1);
static_assert(adjacent(std::array<bool, 3>{false, true, false}, 1, 1) == 1);
static_assert(adjacent(std::array<bool, 0>{}, -1, 1) == -1);
static_assert(cache_owner(1, true, 0, false, 0)); // Existing BBC cache migrates.
static_assert(!cache_owner(9, false, 5, false, 0));
static_assert(cache_owner(9, false, 5, true, 9));
static_assert(!cache_owner(10, false, 5, true, 9)); // Deleted/reused slot.
static_assert(!cache_owner(0, false, 5, true, 0));
static_assert(valid_name("BBC Radio 1"));
static_assert(!valid_name(""));
static_assert(!valid_name("  "));
static_assert(!valid_name("name\nwith control"));
static_assert(valid_url("https://example.com/live.m3u8?key=value"));
static_assert(valid_url("http://example.com:8000/live"));
static_assert(!valid_url("javascript:alert(1)"));
static_assert(!valid_url("https:///missing-host"));
static_assert(!valid_url("https://user:password@example.com/live"));
static_assert(!valid_url("https://example.com/has space"));
static_assert(!valid_url("https://example.com\\elsewhere"));
static_assert(valid_uuid("12345678-abcd-1234-abcd-123456789abc"));
static_assert(!valid_uuid("bad-uuid"));
static_assert(!valid_uuid("12345678-abcd-1234-abcd-123456789abz"));
static_assert(bounded_json(R"({"name":"braces { inside a string","array":[1,2]})"));
static_assert(!bounded_json("[[[[[[[[[]]]]]]]]]"));
static_assert(!bounded_json(R"({"name":"unfinished})"));
static_assert(!bounded_json("}{}"));
