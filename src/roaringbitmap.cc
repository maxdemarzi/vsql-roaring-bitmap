// Copyright (c) 2026 VillageSQL Contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, see <https://www.gnu.org/licenses/>.

#include <villagesql/extension.h>

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <roaring/roaring64map.hh>

using namespace villagesql::extension_builder;
using namespace villagesql::func_builder;
using namespace roaring;

// Maximum size for persisted roaring bitmap (in bytes)
constexpr int64_t kRoaring64MaxPersistedLength = 8 * 1024 * 1024;  // 8MB max

// Deserialize binary data to roaring64_bitmap_t
Roaring64Map deserialize_roaring64(const Span<const unsigned char>& data) {
  if (data.empty()) {
    return roaring::roaring64_bitmap_create();
  }
  return roaring::roaring64_bitmap_deserialize_internal(data.data(), data.size());
}

// Serialize roaring64_bitmap_t to binary
std::vector<unsigned char> serialize_roaring64(roaring64_bitmap_t* bitmap) {
  if (bitmap == nullptr) {
    return std::vector<unsigned char>();
  }
  size_t size = roaring64_bitmap_size_in_bytes(bitmap);
  if (size == 0) {
    return std::vector<unsigned char>();
  }
  std::vector<unsigned char> buf(size);
  roaring64_bitmap_serialize(bitmap, buf.data());
  return buf;
}

// from_string: "{1,5,10,255,1000}" -> binary roaring64 representation
void roaring64_from_string(std::string_view from, CustomResult out) {
  std::string input(from);
  const char* s = input.c_str();
  
  while (*s == ' ') s++;
  
  if (*s != '{') {
    out.warning("roaring64_from_string: missing '{'");
    return;
  }
  s++;

  roaring64_bitmap_t* bitmap = roaring64_bitmap_create();
  if (bitmap == nullptr) {
    out.error("roaring64_from_string: failed to create bitmap");
    return;
  }

  auto buf = out.buffer();
  
  while (*s != '\0') {
    while (*s == ' ') s++;
    if (*s == '}') break;

    char* endptr = nullptr;
    uint64_t val = strtoull(s, &endptr, 10);
    
    if (endptr == s) {
      out.warning("roaring64_from_string: parse error");
      roaring64_bitmap_free(bitmap);
      return;
    }
    
    roaring64_bitmap_add(bitmap, val);
    s = endptr;

    while (*s == ' ') s++;
    if (*s == ',') s++;
  }
  
  if (*s != '}') {
    out.warning("roaring64_from_string: missing '}'");
    roaring64_bitmap_free(bitmap);
    return;
  }

  // Serialize to binary format
  size_t serialize_size = roaring64_bitmap_size_in_bytes(bitmap);
  if (serialize_size > buf.size()) {
    out.error("roaring64_from_string: output buffer too small");
    roaring64_bitmap_free(bitmap);
    return;
  }

  roaring64_bitmap_serialize(bitmap, buf.data());
  out.set_length(serialize_size);
  roaring64_bitmap_free(bitmap);
}

// to_string: binary roaring64 representation -> "{1,5,10,255,1000}"
void roaring64_to_string(CustomArg in, StringResult out) {
  if (in.is_null()) {
    out.set_null();
    return;
  }

  auto data = in.value();
  if (data.empty()) {
    auto buf = out.buffer();
    if (buf.size() < 2) return;
    buf[0] = '{';
    buf[1] = '}';
    out.set_length(2);
    return;
  }

  roaring64_bitmap_t* bitmap = deserialize_roaring64(data);
  if (bitmap == nullptr) {
    out.warning("roaring64_to_string: deserialization failed");
    return;
  }

  auto buf = out.buffer();
  size_t pos = 0;
  
  if (pos >= buf.size()) {
    roaring64_bitmap_free(bitmap);
    return;
  }
  buf[pos++] = '{';

  roaring64_iterator_t iter;
  roaring64_iterator_init(bitmap, &iter);
  
  bool first = true;
  while (roaring64_iterator_has_value(&iter)) {
    uint64_t val = roaring64_iterator_value(&iter);
    
    if (!first) {
      if (pos >= buf.size()) {
        roaring64_bitmap_free(bitmap);
        return;
      }
      buf[pos++] = ',';
    }
    first = false;

    int written = snprintf(buf.data() + pos, buf.size() - pos, "% " PRIu64, val);
    if (written < 0 || pos + static_cast<size_t>(written) >= buf.size()) {
      roaring64_bitmap_free(bitmap);
      return;
    }
    pos += static_cast<size_t>(written);
    
    roaring64_iterator_advance(&iter);
  }

  if (pos >= buf.size()) {
    roaring64_bitmap_free(bitmap);
    return;
  }
  buf[pos++] = '}';

  out.set_length(pos);
  roaring64_bitmap_free(bitmap);
}

// compare: lexicographic comparison of serialized bitmaps
int roaring64_compare(CustomArg a, CustomArg b) {
  auto data_a = a.value();
  auto data_b = b.value();
  
  size_t min_size = (data_a.size() < data_b.size()) ? data_a.size() : data_b.size();
  int cmp = std::memcmp(data_a.data(), data_b.data(), min_size);
  if (cmp != 0) return cmp;
  
  if (data_a.size() < data_b.size()) return -1;
  if (data_a.size() > data_b.size()) return 1;
  return 0;
}

// Union: (ROARING64, ROARING64) -> ROARING64
void roaring64_union(CustomArg a, CustomArg b, CustomResult out) {
  if (a.is_null() || b.is_null()) {
    out.set_null();
    return;
  }

  roaring64_bitmap_t* bitmap_a = deserialize_roaring64(a.value());
  roaring64_bitmap_t* bitmap_b = deserialize_roaring64(b.value());
  
  if (bitmap_a == nullptr || bitmap_b == nullptr) {
    if (bitmap_a) roaring64_bitmap_free(bitmap_a);
    if (bitmap_b) roaring64_bitmap_free(bitmap_b);
    out.error("roaring64_union: deserialization failed");
    return;
  }

  roaring64_bitmap_t* result = roaring64_bitmap_copy(bitmap_a);
  if (result == nullptr) {
    roaring64_bitmap_free(bitmap_a);
    roaring64_bitmap_free(bitmap_b);
    out.error("roaring64_union: memory allocation failed");
    return;
  }

  roaring64_bitmap_or_inplace(result, bitmap_b);

  size_t serialize_size = roaring64_bitmap_size_in_bytes(result);
  auto buf = out.buffer();
  
  if (buf.size() < serialize_size) {
    out.error("roaring64_union: output buffer too small");
    roaring64_bitmap_free(bitmap_a);
    roaring64_bitmap_free(bitmap_b);
    roaring64_bitmap_free(result);
    return;
  }

  roaring64_bitmap_serialize(result, buf.data());
  out.set_length(serialize_size);

  roaring64_bitmap_free(bitmap_a);
  roaring64_bitmap_free(bitmap_b);
  roaring64_bitmap_free(result);
}

// Intersection: (ROARING64, ROARING64) -> ROARING64
void roaring64_intersection(CustomArg a, CustomArg b, CustomResult out) {
  if (a.is_null() || b.is_null()) {
    out.set_null();
    return;
  }

  roaring64_bitmap_t* bitmap_a = deserialize_roaring64(a.value());
  roaring64_bitmap_t* bitmap_b = deserialize_roaring64(b.value());
  
  if (bitmap_a == nullptr || bitmap_b == nullptr) {
    if (bitmap_a) roaring64_bitmap_free(bitmap_a);
    if (bitmap_b) roaring64_bitmap_free(bitmap_b);
    out.error("roaring64_intersection: deserialization failed");
    return;
  }

  roaring64_bitmap_t* result = roaring64_bitmap_and(bitmap_a, bitmap_b);
  
  if (result == nullptr) {
    roaring64_bitmap_free(bitmap_a);
    roaring64_bitmap_free(bitmap_b);
    out.error("roaring64_intersection: operation failed");
    return;
  }

  size_t serialize_size = roaring64_bitmap_size_in_bytes(result);
  auto buf = out.buffer();
  
  if (buf.size() < serialize_size) {
    out.error("roaring64_intersection: output buffer too small");
    roaring64_bitmap_free(bitmap_a);
    roaring64_bitmap_free(bitmap_b);
    roaring64_bitmap_free(result);
    return;
  }

  roaring64_bitmap_serialize(result, buf.data());
  out.set_length(serialize_size);

  roaring64_bitmap_free(bitmap_a);
  roaring64_bitmap_free(bitmap_b);
  roaring64_bitmap_free(result);
}

// Contains: (ROARING64, INT) -> INT (1 or 0)
void roaring64_contains(CustomArg bitmap, IntArg value, IntResult out) {
  if (bitmap.is_null() || value.is_null()) {
    out.set_null();
    return;
  }

  roaring64_bitmap_t* rb = deserialize_roaring64(bitmap.value());
  if (rb == nullptr) {
    out.error("roaring64_contains: deserialization failed");
    return;
  }

  bool result = roaring64_bitmap_contains(rb, static_cast<uint64_t>(value.value()));
  out.set(result ? 1 : 0);
  
  roaring64_bitmap_free(rb);
}

// Cardinality: ROARING64 -> INT
void roaring64_cardinality(CustomArg bitmap, IntResult out) {
  if (bitmap.is_null()) {
    out.set_null();
    return;
  }

  roaring64_bitmap_t* rb = deserialize_roaring64(bitmap.value());
  if (rb == nullptr) {
    out.error("roaring64_cardinality: deserialization failed");
    return;
  }

  uint64_t card = roaring64_bitmap_get_cardinality(rb);
  out.set(static_cast<long long>(card));
  
  roaring64_bitmap_free(rb);
}

static constexpr const char kRoaring64TypeName[] = "ROARING64";

constexpr auto ROARING64 = vsql::make_type<kRoaring64TypeName>()
    .persisted_length(-1)
    .max_decode_buffer_length(256)
    .max_persisted_length(kRoaring64MaxPersistedLength)
    .from_string<&roaring64_from_string>()
    .to_string<&roaring64_to_string>()
    .compare<&roaring64_compare>()
    .build();

VEF_GENERATE_ENTRY_POINTS(
    make_extension()
        .type(ROARING64)
        .func(make_func<&roaring64_union>("roaring64_union")
                  .returns(ROARING64)
                  .param(ROARING64)
                  .param(ROARING64)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_intersection>("roaring64_intersection")
                  .returns(ROARING64)
                  .param(ROARING64)
                  .param(ROARING64)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_contains>("roaring64_contains")
                  .returns(INT)
                  .param(ROARING64)
                  .param(INT)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_cardinality>("roaring64_cardinality")
                  .returns(INT)
                  .param(ROARING64)
                  .deterministic()
                  .build()))
