# Complete Function-by-Function Analysis

## FUNCTIONS WITH SLICE SUPPORT (35 Total)

### 1. **Size** (Line 59)
- **Signature**: `static size_t Size(const ValArray *AL)`
- **Slice Check**: `if(AL->Slice) return AL->Slice->length; return AL->count;`
- **Impact**: CRITICAL - Returns logical vs physical size
- **Test**: Verified in tests

### 2. **Add** (Line 112)
- **Signature**: `static int Add(ValArray *AL, ElementType newval)`
- **Slice Check**: `if(AL->Slice) { pos = AL->Slice->start + AL->Slice->length * AL->Slice->increment; }`
- **Impact**: HIGH - Appends to slice end using formula
- **Test**: test_add_with_slice

### 3. **AddRange** (Line 147)
- **Signature**: `static int AddRange(ValArray *AL, size_t n, const ElementType *data)`
- **Slice Check**: `if(AL->Slice) sliceIncrement = AL->Slice->increment;`
- **Impact**: MEDIUM - Handles spaced insertions
- **Test**: Needs comprehensive testing

### 4. **GetRange** (Line 195)
- **Signature**: `static ValArray *GetRange(const ValArray *AL, size_t start, size_t end)`
- **Slice Check**: `if(AL->Slice) end >= AL->Slice->length` and boundary adjustment
- **Impact**: HIGH - Extracts sub-range respecting slice
- **Test**: test_get_range with slices

### 5. **Contains** (Line 251)
- **Signature**: `static int Contains(const ValArray *AL, ElementType data)`
- **Slice Check**: `if(AL->Slice) { start = AL->Slice->start; top = AL->Slice->length; incr = AL->Slice->increment; }`
- **Impact**: HIGH - Searches only within slice
- **Test**: test_contains_with_slice

### 6. **Equal** (Line 267)
- **Signature**: `static int Equal(const ValArray *AL1, const ValArray *AL2)`
- **Slice Check**: Multiple slice comparison checks
- **Impact**: MEDIUM - Compares slice contents correctly
- **Test**: Needs slice comparison test

### 7. **Copy** (Line 306)
- **Signature**: `static ValArray *Copy(const ValArray *AL)`
- **Slice Check**: `startsize = (AL->Slice == NULL) ? AL->count : AL->Slice->length;`
- **Impact**: HIGH - Copies only slice elements
- **Test**: test_copy_with_slice

### 8. **CopyElement** (Line 348)
- **Signature**: `static int CopyElement(const ValArray *AL, size_t idx, ElementType *outbuf)`
- **Slice Check**: `idx = AL->Slice->start + idx * AL->Slice->increment;`
- **Impact**: MEDIUM - Maps logical to physical index
- **Test**: test_copy_element

### 9. **CopyTo** (Line 359)
- **Signature**: `static ElementType *CopyTo(ValArray *AL)`
- **Slice Check**: Iteration loop with slice parameters
- **Impact**: MEDIUM - Copies slice to new buffer
- **Test**: test_copy_to

### 10. **IndexOf** (Line 380)
- **Signature**: `static int IndexOf(ValArray *AL, ElementType data, size_t *result)`
- **Slice Check**: `if(AL->Slice) { start = ...; top = AL->Slice->length; incr = ...; }`
- **Impact**: MEDIUM - Finds element within slice
- **Test**: Needs slice variant test

### 11. **GetElement** (Line 401)
- **Signature**: `static ElementType GetElement(const ValArray *AL, size_t idx)`
- **Slice Check**: `idx = start + idx*incr;`
- **Impact**: CRITICAL - All element access uses this
- **Test**: test_get_element_with_slice

### 12. **EraseAt** (Line 538)
- **Signature**: `static int EraseAt(ValArray *AL, size_t idx)`
- **Slice Check**: Complex logic for slice removal
- **Impact**: HIGH - Erases from slice maintaining integrity
- **Test**: Comprehensive slice erase tests

### 13. **RemoveRange** (Line 566)
- **Signature**: `static int RemoveRange(ValArray *AL, size_t start, size_t end)`
- **Slice Check**: Slice boundary adjustment before removal
- **Impact**: MEDIUM - Removes range within slice
- **Test**: test_remove_range

### 14. **PopBack** (Line 730)
- **Signature**: `static int PopBack(ValArray *AL, ElementType *result)`
- **Slice Check**: `idx = AL->Slice->start + (AL->Slice->length-1)*AL->Slice->increment;`
- **Impact**: MEDIUM - Pops from slice end
- **Test**: test_popback_with_slice

### 15. **Apply** (Line 778)
- **Signature**: `static int Apply(ValArray *AL, int (*Applyfn)(ElementType, void *), void *arg)`
- **Slice Check**: Iteration pattern with slice parameters
- **Impact**: MEDIUM - Applies function to slice elements
- **Test**: test_apply_with_slice

### 16. **ForEach** (Line 792)
- **Signature**: `static int ForEach(ValArray *AL, ElementType (*ApplyFn)(ElementType))`
- **Slice Check**: Iteration pattern with transformation
- **Impact**: MEDIUM - Transforms slice elements
- **Test**: test_foreach_with_slice

### 17. **Mismatch** (Line 735)
- **Signature**: `static int Mismatch(const ValArray *a1, const ValArray *a2, size_t *mismatch)`
- **Slice Check**: Dual slice handling for comparison
- **Impact**: MEDIUM - Finds first difference in slices
- **Test**: test_mismatch

### 18. **Sort** (Line 822)
- **Signature**: `static int Sort(ValArray *AL)`
- **Slice Check**: Copies slice to temp buffer, sorts, copies back
- **Impact**: HIGH - Sorts only slice elements
- **Test**: test_sort (may not test with slice)

### 19. **Reverse** (Line 860)
- **Signature**: `static int Reverse(ValArray *AL)`
- **Slice Check**: `p = AL->contents + AL->Slice->start; q = p + (s*AL->Slice->increment-1);`
- **Impact**: HIGH - Reverses slice in-place
- **Test**: test_reverse_with_slice

### 20. **Max** (Line 2011)
- **Signature**: `static ElementType Max(const ValArray *src)`
- **Slice Check**: `if(src->Slice) { start = ...; incr = ...; length = src->Slice->length; }`
- **Impact**: MEDIUM - Finds max in slice
- **Test**: test_min_max

### 21. **Min** (Line 2089)
- **Signature**: `static ElementType Min(const ValArray *src)`
- **Slice Check**: Same pattern as Max
- **Impact**: MEDIUM - Finds min in slice
- **Test**: test_min_max

### 22. **Abs** (Line 2032)
- **Signature**: `static int Abs(ValArray *src)`
- **Slice Check**: Iteration pattern
- **Impact**: LOW - Absolute value of slice elements
- **Test**: test_abs

### 23. **Accumulate** (Line 2051)
- **Signature**: `static ElementType Accumulate(const ValArray *src)`
- **Slice Check**: Iteration pattern
- **Impact**: MEDIUM - Sums slice elements
- **Test**: test_accumulate

### 24. **Product** (Line 2070)
- **Signature**: `static ElementType Product(const ValArray *src)`
- **Slice Check**: Iteration pattern
- **Impact**: MEDIUM - Product of slice elements
- **Test**: test_product

### 25. **Fprintf** (Line 2114)
- **Signature**: `static int Fprintf(const ValArray *src, FILE *out, const char *fmt)`
- **Slice Check**: Iteration pattern
- **Impact**: LOW - Outputs slice elements
- **Test**: test_fprintf

### 26. **FillSequential** (Line 1811)
- **Signature**: `static int FillSequential(ValArray *dst, size_t length, ElementType start, ElementType increment)`
- **Slice Check**: Special handling for slice fill
- **Impact**: MEDIUM - Fills slice with sequence
- **Test**: test_fill_sequential_with_slice

### 27. **Inverse** (Line 1747)
- **Signature**: `static int Inverse(ValArray *s)`
- **Slice Check**: Iteration pattern
- **Impact**: LOW - Inverts slice elements
- **Test**: Needs test

### 28. **SumTo** (Line 1383)
- **Signature**: `static int SumTo(ValArray *left, const ValArray *right)`
- **Slice Check**: Dual slice index calculation
- **Impact**: MEDIUM - Adds right slice to left
- **Test**: test_sum_to

### 29. **SumToScalar** (Line 1411)
- **Signature**: `static int SumToScalar(ValArray *left, ElementType right)`
- **Slice Check**: Iteration pattern
- **Impact**: MEDIUM - Adds scalar to slice
- **Test**: test_sum_scalar_to

### 30. **SubtractFrom** (Line 1429)
- **Signature**: `static int SubtractFrom(ValArray *left, const ValArray *right)`
- **Slice Check**: Dual slice index calculation
- **Impact**: MEDIUM - Subtracts right from left
- **Test**: test_subtract_from

### 31. **SubtractScalarFrom** (Line 1456)
- **Signature**: `static int SubtractScalarFrom(ValArray *left, ElementType right)`
- **Slice Check**: Iteration pattern
- **Impact**: MEDIUM - Subtracts scalar from slice
- **Test**: test_subtract_scalar_from

### 32. **SubtractFromScalar** (Line 1474)
- **Signature**: `static int SubtractFromScalar(ElementType left, ValArray *right)`
- **Slice Check**: Iteration pattern
- **Impact**: MEDIUM - Scalar minus slice
- **Test**: test_subtract (partial)

### 33. **LeftShift** (Line 1923)
- **Signature**: `static int LeftShift(ValArray *data, int shift)`
- **Slice Check**: Iteration pattern
- **Impact**: LOW - Bit shift left on slice
- **Test**: Needs test

### 34. **RightShift** (Line 1940)
- **Signature**: `static int RightShift(ValArray *data, int shift)`
- **Slice Check**: Iteration pattern
- **Impact**: LOW - Bit shift right on slice
- **Test**: Needs test

### 35. **Not** (Line 1905)
- **Signature**: `static int Not(ValArray *left)`
- **Slice Check**: Iteration pattern with increment bug (s += incr not i += incr)
- **Impact**: LOW - Bitwise NOT on slice
- **Test**: Needs test (CHECK FOR BUG)

---

## FUNCTIONS WITHOUT SLICE SUPPORT (47 Total)

### Low-Level Memory Functions (5)
1. **ResizeTo** - Raw capacity resize
2. **grow** - Growth helper
3. **Resize** - Size adjustment
4. **SetCapacity** - Capacity setting
5. **Clear** - Clears array, frees slice

### Insertion Functions (3)
1. **InsertAt** - Index-based insertion (ISSUE: ignores slice)
2. **Insert** - Front insertion (ISSUE: ignores slice)
3. **InsertIn** - Bulk insertion (ISSUE: ignores slice)

### Removal Functions (2)
1. **Erase** - Element-based removal (calls EraseAt which handles slice)
2. **PushBack** - Append helper (calls InsertAt)

### Full Array Arithmetic (12)
1. **MultiplyWith** - Element-wise multiply
2. **MultiplyWithScalar** - Scalar multiply
3. **DivideBy** - Element-wise divide
4. **DivideByScalar** - Scalar divide
5. **DivideScalarBy** - Scalar / array
6. **Mod** - Modulo operation
7. **ModScalar** - Scalar modulo
8. **Or** - Bitwise OR
9. **OrScalar** - OR with scalar
10. **And** - Bitwise AND
11. **AndScalar** - AND with scalar
12. **Xor** - Bitwise XOR
13. **XorScalar** - XOR with scalar

### Comparison Functions (4)
1. **CompareEqual** - Has slice checks (mixed)
2. **CompareEqualScalar** - Full array compare
3. **Compare** - Full array compare
4. **CompareScalar** - Full array compare
5. **FCompare** - Float compare (has some slice handling)

### Array Operations (4)
1. **Append** - Array concatenation
2. **RotateLeft** - Full array rotation
3. **RotateRight** - Full array rotation
4. **CreateSequence** - Creation only

### Slice Management (3)
1. **SetSlice** - Slice configuration
2. **GetSlice** - Slice retrieval
3. **ResetSlice** - Slice deallocation

### Utility/Infrastructure (16)
1. **GetFlags** - Flag access
2. **SetFlags** - Flag setting
3. **GetAllocator** - Allocator access
4. **GetCapacity** - Capacity query
5. **GetData** - Direct data pointer
6. **Back** - Last element
7. **Front** - First element
8. **GetElementSize** - Size query
9. **SetDestructor** - No-op
10. **SetCompareFunction** - No-op
11. **Equal** (const variant) - Some slice support
12. **Sizeof** - Size calculation
13. **NewIterator** - Iterator creation
14. **InitIterator** - Iterator init
15. **DeleteIterator** - Iterator cleanup
16. **Save** - Serialization
17. **GetRange** - Has slice support
18. **Init** - Array initialization
19. **InitializeWith** - Initialized creation
20. **CreateWithAllocator** - Allocator creation
21. **Create** - Basic creation
22. **Load** - Deserialization
23. **Memset** - Fill with constant

---

## Summary Statistics

| Category | Count | Slice Support | % |
|----------|-------|---|---|
| Data Access (Get/Copy) | 7 | 7 | 100% |
| Aggregation (Min/Max/Sum) | 5 | 5 | 100% |
| Iteration (Apply/ForEach) | 3 | 3 | 100% |
| Removal (Erase/Remove/Pop) | 4 | 3 | 75% |
| Arithmetic (+/-/*//) | 14 | 8 | 57% |
| Bitwise (&/\|/^/~) | 9 | 1 | 11% |
| Array Ops (Append/Rotate) | 4 | 0 | 0% |
| Insertion (Insert/InsertAt) | 3 | 0 | 0% |
| Memory/Infrastructure | 29 | 0 | 0% |
| **TOTAL** | **82** | **35** | **42.7%** |

