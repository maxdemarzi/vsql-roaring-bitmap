# VillageSQL Roaring Bitmap Extension

A VillageSQL extension providing a `ROARING64` custom data type for efficient 64-bit set operations using the [CRoaring](https://github.com/RoaringBitmap/CRoaring) library.

## Features

- **64-bit Integer Sets**: Store and manipulate sets of 64-bit integers efficiently
- **Bitmap Operations**: Union, intersection, and membership testing
- **Compression**: Roaring Bitmaps automatically optimize between array and bitmap storage
- **Type-Safe**: Custom ROARING64 type integrates seamlessly with VillageSQL
- **Scalable**: Handles bitmaps with millions of elements efficiently

## Building

### Prerequisites

- VillageSQL build directory (with completed build)
- CMake 3.16 or higher
- C++ compiler with C++17 support
- OpenSSL development libraries
- CRoaring library (automatically fetched if not found)

### Development Branch trick

Switch out the include directory for the include-dev directory.

```
cd ~/build/villagesql/villagesql-extension-sdk-0.0.4-dev/
mv include include-old
mv include-dev include
```

### Build Instructions

1. Create a build directory and configure:

   **Linux:**
   ```bash
   mkdir build
   cd build
   cmake .. -DVillageSQL_BUILD_DIR=$HOME/build/villagesql
   ```

   **macOS:**
   ```bash
   mkdir build
   cd build
   cmake .. -DVillageSQL_BUILD_DIR=~/build/villagesql
   ```

2. Build the extension:

   ```bash
   make -j $(($(getconf _NPROCESSORS_ONLN) - 2))
   ```

   This creates the `vsql_roaring_bitmap.veb` package in the build directory.

3. Install the VEB (optional):

   ```bash
   make install
   ```

## Usage

After building the VEB file, load the extension in VillageSQL:

```sql
INSTALL EXTENSION vsql_roaring_bitmap;
```

### Creating Roaring Bitmaps

Use `ROARING64::from_string('{...}')` to construct literals for the custom `ROARING64` type. The `CAST(... AS ROARING64)` syntax is not supported for this type.

```sql
-- Create a bitmap from a set of integers
SELECT ROARING64::from_string('{1,5,10,255,1000}') AS my_bitmap;

-- Insert into a table
CREATE TABLE bitmap_data (id INT PRIMARY KEY, bitmap ROARING64);
INSERT INTO bitmap_data VALUES (1, ROARING64::from_string('{1,2,3,4,5}'));
INSERT INTO bitmap_data VALUES (2, ROARING64::from_string('{3,4,5,6,7}'));
```

### Supported Operations

#### Union (OR)
```sql
-- Combine two bitmaps
SELECT roaring64_union(b1.bitmap, b2.bitmap) 
FROM bitmap_data b1, bitmap_data b2 
WHERE b1.id = 1 AND b2.id = 2;
-- Result: {1,2,3,4,5,6,7}
```

#### Intersection (AND)
```sql
-- Find common elements
SELECT roaring64_intersection(b1.bitmap, b2.bitmap) 
FROM bitmap_data b1, bitmap_data b2 
WHERE b1.id = 1 AND b2.id = 2;
-- Result: {3,4,5}
```

#### Membership Test
```sql
-- Check if element is in bitmap
SELECT roaring64_contains(bitmap, 5) 
FROM bitmap_data 
WHERE id = 1;
-- Result: 1 (true)
```

#### Cardinality
```sql
-- Count elements in bitmap
SELECT roaring64_cardinality(bitmap) 
FROM bitmap_data;
-- Result: 5 for id=1, 5 for id=2
```

#### Difference (A \ B)
```sql
-- Remove elements from the first bitmap that appear in the second
SELECT roaring64_difference(
  ROARING64::from_string('{1,2,3,4}'),
  ROARING64::from_string('{3,4,5}')
) AS difference_bitmap;
-- Result: {1,2}
```

#### Symmetric Difference (XOR)
```sql
-- Elements in either bitmap but not both
SELECT roaring64_symmetric_difference(
  ROARING64::from_string('{1,2,3,4}'),
  ROARING64::from_string('{3,4,5,6}')
) AS symmetric_difference_bitmap;
-- Result: {1,2,5,6}
```

## Type Format

### String Representation

Bitmaps are represented as comma-separated integers within braces:

```
{1,5,10,255,1000}
```

Whitespace is allowed:
```
{ 1 , 5 , 10 }
```

### Binary Format

Internally, bitmaps are stored in CRoaring's portable serialization format, which provides:
- Optimal compression for sparse sets
- Support for 64-bit integer values (0 to 2^64-1)
- Cross-platform binary compatibility

## Functions

| Function | Parameters | Returns | Description |
|----------|-----------|---------|-------------|
| `roaring64_union` | ROARING64, ROARING64 | ROARING64 | Union of two bitmaps (OR operation) |
| `roaring64_intersection` | ROARING64, ROARING64 | ROARING64 | Intersection of two bitmaps (AND operation) |
| `roaring64_difference` | ROARING64, ROARING64 | ROARING64 | Difference of two bitmaps (A \ B) |
| `roaring64_symmetric_difference` | ROARING64, ROARING64 | ROARING64 | Symmetric difference of two bitmaps (XOR) |
| `roaring64_contains` | ROARING64, INT | INT | Test membership (returns 1 or 0) |
| `roaring64_cardinality` | ROARING64 | INT | Count elements in bitmap |

## Performance Characteristics

- **Sparse Sets**: For cardinality < ~4096 per 64K range, uses compressed arrays
- **Dense Sets**: For higher cardinality, uses standard 8KB bitmaps
- **Memory**: Typically 2-8 bytes per element depending on distribution
- **Query Performance**: All operations are highly optimized C implementations

## Limitations

- Maximum serialized size: 8MB
- Supports 64-bit unsigned integers (0 to 2^64-1)
- Set operations require both operands to be ROARING64 type
- Supports union, intersection, difference, and symmetric difference

## Examples

### Analyzing User Segments

```sql
-- Store user IDs by segment
CREATE TABLE user_segments (
  segment_name VARCHAR(255),
  user_ids ROARING64
);

INSERT INTO user_segments VALUES 
  ('premium', ROARING64::from_string('{101,102,105,110,201,202}')),
  ('active_30d', ROARING64::from_string('{101,103,105,201,202,301}')),
  ('churned', ROARING64::from_string('{104,106,107,200,302,303}'));

-- Find users who are both premium and active
SELECT roaring64_intersection(
  (SELECT user_ids FROM user_segments WHERE segment_name = 'premium'),
  (SELECT user_ids FROM user_segments WHERE segment_name = 'active_30d')
) AS premium_active_users;
-- Result: {101,102,105,201,202}

-- Count premium users
SELECT roaring64_cardinality(user_ids) as count
FROM user_segments 
WHERE segment_name = 'premium';
-- Result: 6
```

### Permissions Tracking

```sql
-- Track which users have which permissions using bitmaps
CREATE TABLE role_permissions (
  role_id INT,
  permission_ids ROARING64
);

INSERT INTO role_permissions VALUES
  (1, ROARING64::from_string('{1,2,3,4}')),  -- admin: all permissions
  (2, ROARING64::from_string('{1,2}')),       -- editor: read, write
  (3, ROARING64::from_string('{1}'));         -- viewer: read only

-- Combine permissions from multiple roles
SELECT roaring64_union(
  (SELECT permission_ids FROM role_permissions WHERE role_id = 2),
  (SELECT permission_ids FROM role_permissions WHERE role_id = 3)
) AS combined_permissions;
-- Result: {1,2}
```

## Building from Source

### With Pre-installed CRoaring

If CRoaring is already installed on your system:

```bash
mkdir build && cd build
cmake .. \
  -DVillageSQL_BUILD_DIR=$HOME/build/villagesql \
  -DCMAKE_PREFIX_PATH=/usr/local  # or wherever CRoaring is installed
make
```

### Troubleshooting

**Build fails: "CRoaring not found"**

The extension will attempt to automatically fetch CRoaring v2.1.1 from GitHub. If this fails due to network issues:

```bash
# Manually pre-install CRoaring
git clone https://github.com/RoaringBitmap/CRoaring.git
cd CRoaring && mkdir build && cd build
cmake .. && make && sudo make install
```

**VillageSQL SDK not found:**

```bash
cmake .. -DVillageSQL_BUILD_DIR=$HOME/build/villagesql
```

## Testing

The extension can be tested with:

```sql
-- Load the extension
INSTALL EXTENSION vsql_roaring_bitmap;

-- Basic type test
SELECT ROARING64::from_string('{1,2,3}');

-- Function tests
SELECT roaring64_cardinality(ROARING64::from_string('{1,2,3}'));  -- 3
SELECT roaring64_contains(ROARING64::from_string('{1,2,3}'), 2);  -- 1
SELECT roaring64_contains(ROARING64::from_string('{1,2,3}'), 5);  -- 0
```

## References

- [CRoaring GitHub](https://github.com/RoaringBitmap/CRoaring)
- [Roaring Bitmap Paper](https://arxiv.org/abs/1603.06549)
- [VillageSQL Documentation](https://villagesql.com/docs)
- [VillageSQL Extension Framework](https://villagesql.com/docs)

## License

GPL-2.0 - See LICENSE file for details

## Contributing

Contributions are welcome! Please ensure your code follows the GPL-2.0 license and includes appropriate copyright notices.
