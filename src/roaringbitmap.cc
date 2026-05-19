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

#include <villagesql/vsql.h>

#include <charconv>
#include <cctype>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <roaring/roaring64map.hh>

using namespace roaring;
using namespace vsql;

static constexpr const char kROARING64[] = "ROARING64";

namespace {

bool parseRoaring64String(std::string_view in, Roaring64Map &bitmap,
                          std::string &error_msg) {
  bitmap = Roaring64Map();
  size_t pos = 0;

  auto skip_ws = [&]() {
    while (pos < in.size() && std::isspace(static_cast<unsigned char>(in[pos]))) {
      ++pos;
    }
  };

  skip_ws();
  if (pos >= in.size() || in[pos] != '{') {
    error_msg = "ROARING64: expected '{' at beginning";
    return false;
  }
  ++pos;
  skip_ws();

  if (pos < in.size() && in[pos] == '}') {
    ++pos;
    skip_ws();
    if (pos != in.size()) {
      error_msg = "ROARING64: unexpected trailing characters after '}'";
      return false;
    }
    return true;
  }

  bool first_element = true;
  while (pos < in.size()) {
    skip_ws();
    if (!first_element) {
      if (pos >= in.size() || in[pos] != ',') {
        error_msg = "ROARING64: expected ',' between elements";
        return false;
      }
      ++pos;
      skip_ws();
    }
    first_element = false;

    if (pos >= in.size() ||
        (!std::isdigit(static_cast<unsigned char>(in[pos])) && in[pos] != '+')) {
      error_msg = "ROARING64: expected unsigned integer value";
      return false;
    }

    size_t start = pos;
    if (in[pos] == '+') {
      ++pos;
      if (pos >= in.size() || !std::isdigit(static_cast<unsigned char>(in[pos]))) {
        error_msg = "ROARING64: invalid integer literal";
        return false;
      }
    }
    while (pos < in.size() && std::isdigit(static_cast<unsigned char>(in[pos]))) {
      ++pos;
    }

    std::string_view token = in.substr(start, pos - start);
    unsigned long long value = 0;
    auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    if (result.ec != std::errc() || result.ptr != token.data() + token.size()) {
      error_msg = "ROARING64: invalid integer literal";
      return false;
    }
    bitmap.add(static_cast<uint64_t>(value));

    skip_ws();
    if (pos < in.size() && in[pos] == '}') {
      ++pos;
      skip_ws();
      if (pos != in.size()) {
        error_msg = "ROARING64: unexpected trailing characters after '}'";
        return false;
      }
      return true;
    }
  }

  error_msg = "ROARING64: missing closing '}'";
  return false;
}

std::string roaring64ToString(const Roaring64Map &bitmap) {
  std::string out = "{";
  bool first = true;
  for (auto it = bitmap.begin(); it != bitmap.end(); ++it) {
    if (!first) {
      out.push_back(',');
    }
    first = false;
    out.append(std::to_string(*it));
  }
  out.push_back('}');
  return out;
}

int roaring64CompareMaps(const Roaring64Map &a, const Roaring64Map &b) {
  if (a == b) {
    return 0;
  }

  auto ai = a.begin();
  auto bi = b.begin();
  auto aend = a.end();
  auto bend = b.end();

  while (ai != aend && bi != bend) {
    uint64_t av = *ai;
    uint64_t bv = *bi;
    if (av < bv) {
      return -1;
    }
    if (av > bv) {
      return 1;
    }
    ++ai;
    ++bi;
  }

  if (ai == aend) {
    return -1;
  }
  return 1;
}

size_t roaring64HashMap(const Roaring64Map &bitmap) {
  size_t hash = 1469598103934665603ULL;
  for (auto it = bitmap.begin(); it != bitmap.end(); ++it) {
    uint64_t value = *it;
    for (size_t i = 0; i < sizeof(value); ++i) {
      hash ^= static_cast<unsigned char>((value >> (i * 8)) & 0xff);
      hash *= 1099511628211ULL;
    }
  }
  return hash;
}

bool serializeRoaring64Map(const Roaring64Map &bitmap, CustomResult out,
                           std::string_view context,
                           std::string &error_msg) {
  size_t bytes = bitmap.getSizeInBytes(true);
  auto buf = out.buffer();
  if (bytes > buf.size()) {
    error_msg = std::string(context) + ": output buffer too small";
    return false;
  }
  bitmap.write(reinterpret_cast<char *>(buf.data()), true);
  out.set_length(bytes);
  return true;
}

bool deserializeRoaring64Map(CustomArg in, Roaring64Map &bitmap,
                             std::string &error_msg) {
  try {
    bitmap = Roaring64Map::read(reinterpret_cast<const char *>(in.value().data()),
                                in.value().size());
  } catch (const std::exception &e) {
    error_msg = std::string("ROARING64: failed to deserialize bitmap: ") +
                e.what();
    return false;
  }
  return true;
}

}  // namespace

void roaring64_from_string(std::string_view in, CustomResult out) {
  Roaring64Map bitmap;
  std::string error_msg;
  if (!parseRoaring64String(in, bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }
  if (!serializeRoaring64Map(bitmap, out, "ROARING64", error_msg)) {
    out.error(error_msg);
  }
}

void roaring64_to_string(CustomArg in, StringResult out) {
  if (in.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in, bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  std::string value = roaring64ToString(bitmap);
  auto buf = out.buffer();
  size_t len = value.size();
  if (len > buf.size()) {
    out.error("ROARING64: output buffer too small");
    return;
  }
  memcpy(buf.data(), value.data(), len);
  out.set_length(static_cast<size_t>(len));
}

int roaring64_compare(CustomArg a, CustomArg b) {
  Roaring64Map left;
  Roaring64Map right;
  std::string error_msg;
  if (!deserializeRoaring64Map(a, left, error_msg)) {
    return -1;
  }
  if (!deserializeRoaring64Map(b, right, error_msg)) {
    return 1;
  }
  return roaring64CompareMaps(left, right);
}

size_t roaring64_hash(CustomArg in) {
  Roaring64Map bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in, bitmap, error_msg)) {
    return 0;
  }
  return roaring64HashMap(bitmap);
}

void roaring64_add(CustomArg in, IntArg value, CustomResult out) {
  if (in.is_null() || value.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in, bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  bitmap.add(static_cast<uint64_t>(value.value()));
  if (!serializeRoaring64Map(bitmap, out, "ROARING64::add", error_msg)) {
    out.error(error_msg);
  }
}

void roaring64_remove(CustomArg in, IntArg value, CustomResult out) {
  if (in.is_null() || value.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in, bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  bitmap.remove(static_cast<uint64_t>(value.value()));
  if (!serializeRoaring64Map(bitmap, out, "ROARING64::remove", error_msg)) {
    out.error(error_msg);
  }
}

void roaring64_contains(CustomArg in, IntArg value, IntResult out) {
  if (in.is_null() || value.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in, bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  out.set(bitmap.contains(static_cast<uint64_t>(value.value())) ? 1 : 0);
}

void roaring64_cardinality(CustomArg in, IntResult out) {
  if (in.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in, bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  out.set(static_cast<int64_t>(bitmap.cardinality()));
}

void roaring64_is_empty(CustomArg in, IntResult out) {
  if (in.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in, bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  out.set(bitmap.isEmpty() ? 1 : 0);
}

void roaring64_clear(CustomArg in, CustomResult out) {
  if (in.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in, bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  if (!serializeRoaring64Map(Roaring64Map(), out, "ROARING64::clear", error_msg)) {
    out.error(error_msg);
  }
}

void roaring64_run_optimize(CustomArg in, CustomResult out) {
  if (in.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in, bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  bitmap.runOptimize();
  if (!serializeRoaring64Map(bitmap, out, "ROARING64::run_optimize", error_msg)) {
    out.error(error_msg);
  }
}

void roaring64_equals(CustomArg in_l, CustomArg in_r, IntResult out) {
  if (in_l.is_null() || in_r.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map left_bitmap;
  Roaring64Map right_bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in_l, left_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }
  if (!deserializeRoaring64Map(in_r, right_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  out.set(left_bitmap == right_bitmap ? 1 : 0);
}

void roaring64_swap(CustomArg in_l, CustomArg in_r, CustomResult out) {
  if (in_l.is_null() || in_r.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map left_bitmap;
  Roaring64Map right_bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in_l, left_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }
  if (!deserializeRoaring64Map(in_r, right_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  std::swap(left_bitmap, right_bitmap);
  if (!serializeRoaring64Map(left_bitmap, out, "ROARING64::swap", error_msg)) {
    out.error(error_msg);
  }
}

void roaring64_union(CustomArg in_l, CustomArg in_r, CustomResult out) {
  if (in_l.is_null() || in_r.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map left_bitmap;
  Roaring64Map right_bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in_l, left_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }
  if (!deserializeRoaring64Map(in_r, right_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  Roaring64Map result = left_bitmap;
  result |= right_bitmap;
  if (!serializeRoaring64Map(result, out, "ROARING64::union", error_msg)) {
    out.error(error_msg);
  }
}

void roaring64_intersection(CustomArg in_l, CustomArg in_r, CustomResult out) {
  if (in_l.is_null() || in_r.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map left_bitmap;
  Roaring64Map right_bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in_l, left_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }
  if (!deserializeRoaring64Map(in_r, right_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  Roaring64Map result = left_bitmap;
  result &= right_bitmap;
  if (!serializeRoaring64Map(result, out, "ROARING64::intersection", error_msg)) {
    out.error(error_msg);
  }
}

void roaring64_difference(CustomArg in_l, CustomArg in_r, CustomResult out) {
  if (in_l.is_null() || in_r.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map left_bitmap;
  Roaring64Map right_bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in_l, left_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }
  if (!deserializeRoaring64Map(in_r, right_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  Roaring64Map result = left_bitmap;
  result -= right_bitmap;
  if (!serializeRoaring64Map(result, out, "ROARING64::difference", error_msg)) {
    out.error(error_msg);
  }
}

void roaring64_symmetric_difference(CustomArg in_l, CustomArg in_r, CustomResult out) {
  if (in_l.is_null() || in_r.is_null()) {
    out.set_null();
    return;
  }

  Roaring64Map left_bitmap;
  Roaring64Map right_bitmap;
  std::string error_msg;
  if (!deserializeRoaring64Map(in_l, left_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }
  if (!deserializeRoaring64Map(in_r, right_bitmap, error_msg)) {
    out.error(error_msg);
    return;
  }

  Roaring64Map result = left_bitmap;
  result ^= right_bitmap;
  if (!serializeRoaring64Map(result, out, "ROARING64::symmetric_difference", error_msg)) {
    out.error(error_msg);
  }
}

constexpr auto ROARING64_TYPE =
    make_type<kROARING64>()
        .persisted_length(-1)
        .max_decode_buffer_length(65536)
        .intrinsic_default_str("{}")
        .from_string<&roaring64_from_string>()
        .to_string<&roaring64_to_string>()
        .compare<&roaring64_compare>()
        .intrinsic_default_str("{}")
        .hash<&roaring64_hash>()
        .build();

VEF_GENERATE_ENTRY_POINTS(
    make_extension()
        .type(ROARING64_TYPE)
        .func(make_func<&roaring64_add>("roaring64_add")
                  .returns(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .param(INT)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_remove>("roaring64_remove")
                  .returns(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .param(INT)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_contains>("roaring64_contains")
                  .returns(INT)
                  .param(ROARING64_TYPE)
                  .param(INT)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_cardinality>("roaring64_cardinality")
                  .returns(INT)
                  .param(ROARING64_TYPE)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_is_empty>("roaring64_is_empty")
                  .returns(INT)
                  .param(ROARING64_TYPE)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_clear>("roaring64_clear")
                  .returns(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_run_optimize>("roaring64_run_optimize")
                  .returns(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_equals>("roaring64_equals")
                  .returns(INT)
                  .param(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_swap>("roaring64_swap")
                  .returns(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_union>("roaring64_union")
                  .returns(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_intersection>("roaring64_intersection")
                  .returns(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_difference>("roaring64_difference")
                  .returns(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .deterministic()
                  .build())
        .func(make_func<&roaring64_symmetric_difference>("roaring64_symmetric_difference")
                  .returns(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .param(ROARING64_TYPE)
                  .deterministic()
                  .build()))

