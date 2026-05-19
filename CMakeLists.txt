# Copyright (c) 2026 VillageSQL Contributors
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.16)
project(vsql_roaring_bitmap)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EXTENSION_NAME "vsql_roaring_bitmap")

# Find VillageSQL SDK. Pass -DVillageSQL_BUILD_DIR=... for a local server
# build tree, or -DVillageSQL_SDK_DIR=... for an explicit SDK install.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(VillageSQL REQUIRED)

message(STATUS "VillageSQL SDK found at: ${VillageSQL_PREFIX}")
message(STATUS "VillageSQL include dir: ${VillageSQL_INCLUDE_DIR}")

# Find or build CRoaring library
find_package(roaring QUIET)

if(NOT roaring_FOUND)
  message(STATUS "CRoaring not found, attempting to fetch from GitHub...")
  include(FetchContent)

  # Build CRoaring as PIC so the static library can be linked into our
  # shared extension object.
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)

  FetchContent_Declare(
    CRoaring
    GIT_REPOSITORY https://github.com/RoaringBitmap/CRoaring.git
    GIT_TAG v4.7.0
  )
  
  set(ENABLE_ROARING_TESTS OFF CACHE BOOL "Disable CRoaring tests")
  set(ENABLE_ROARING_DOCS OFF CACHE BOOL "Disable CRoaring docs")
  FetchContent_MakeAvailable(CRoaring)
  
  set(ROARING_INCLUDE_DIR "${CMAKE_BINARY_DIR}/_deps/croaring-src/include")
  set(ROARING_LIB roaring)
else()
  set(ROARING_LIB roaring::roaring)
endif()

# Create the roaring bitmap shared library
add_library(roaring_bitmap SHARED
    src/roaringbitmap.cc
)

# Include directories
target_include_directories(roaring_bitmap PRIVATE
    ${VillageSQL_INCLUDE_DIR}
    ${ROARING_INCLUDE_DIR}
)

# Link against CRoaring
target_link_libraries(roaring_bitmap PRIVATE ${ROARING_LIB})

# Create the VEB package
VEF_CREATE_VEB(
    NAME ${EXTENSION_NAME}
    LIBRARY_TARGET roaring_bitmap
    MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
)
