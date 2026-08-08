# ValArray Functions Quick Reference

## Executive Summary

```
Total Functions Analyzed:           82
Functions WITH Slice Checks:        35 (42.7%)
Functions WITHOUT Slice Checks:     47 (57.3%)
```

---

## Functions WITH Slice Support (35)

```
Size
Add
AddRange
GetRange
Contains
Equal
Copy
CopyElement
CopyTo
IndexOf
GetElement
EraseAt
RemoveRange
PopBack
Apply
ForEach
Mismatch
Sort
Reverse
Max
Min
Abs
Accumulate
Product
Fprintf
FillSequential
Inverse
SumTo
SumToScalar
SubtractFrom
SubtractScalarFrom
SubtractFromScalar
LeftShift
RightShift
Not
```

---

## Functions WITHOUT Slice Support (47)

### Architectural/Low-Level (9)
```
ResizeTo, grow, Resize, Clear, SetCapacity
GetFlags, SetFlags, GetAllocator, GetCapacity
```

### Insertion Operations (3)
```
InsertAt, Insert, InsertIn
```

### Single Element Operations (3)
```
Erase, PushBack, GetData
```

### Arithmetic Without Slice (9)
```
MultiplyWith, MultiplyWithScalar
DivideBy, DivideByScalar, DivideScalarBy
Mod, ModScalar
Compare, CompareEqualScalar
```

### Bitwise Without Slice (10)
```
Or, OrScalar, And, AndScalar
Xor, XorScalar
CompareEqual, FCompare, CompareScalar
```

### Array-Level Operations (4)
```
Append, RotateLeft, RotateRight
CreateSequence
```

### Slice Management (3)
```
SetSlice, GetSlice, ResetSlice
```

### Utility Functions (6)
```
NewIterator, InitIterator, Save
GetElementSize, SetDestructor, Memset
```

---

## Support Matrix by Operation Category

| Category | Total | With Slice | Without Slice | % Coverage |
|----------|-------|-----------|--------------|-----------|
| **Data Access** | 7 | 7 | 0 | 100% |
| **Arithmetic** | 14 | 8 | 6 | 57% |
| **Bitwise/Logic** | 13 | 3 | 10 | 23% |
| **Array Operations** | 6 | 2 | 4 | 33% |
| **Modification** | 11 | 3 | 8 | 27% |
| **Aggregation** | 5 | 5 | 0 | 100% |
| **Utility/Mgmt** | 26 | 7 | 19 | 27% |

---

## Critical Gaps

### High Priority (Impact: HIGH)
- **InsertAt**: Cannot insert into sliced array at arbitrary position
- **Insert**: Doesn't respect slice boundaries
- **InsertIn**: Bulk insert ignores slice constraints

### Medium Priority (Impact: MEDIUM)
- **Multiplication Operations**: Full array multiply not slice-aware
- **Division Operations**: Full array divide not slice-aware
- **Rotation Operations**: Cannot rotate within slice
- **Append**: Array concatenation ignores slices

### Low Priority (Impact: LOW)
- **Bitwise Operations**: Most are full-array only
- **Comparison Operations**: Mixed slice support
- **Creation Functions**: Slice not applicable to creation

---

## Usage Recommendations

### ? Safe Operations on Sliced Arrays
```
Size(sliced_array)           // Returns slice length
GetElement(sliced_array, i)  // Uses slice index mapping
Contains(sliced_array, val)  // Searches within slice
Add(sliced_array, val)       // Appends to slice end
Reverse(sliced_array)        // Reverses slice elements
Sort(sliced_array)           // Sorts slice elements
Copy(sliced_array)           // Copies slice only
Accumulate(sliced_array)     // Sums slice elements
```

### ?? Problematic Operations on Sliced Arrays
```
InsertAt(sliced_array, idx, val)  // Ignores slice
Append(sliced_array, other)       // Concatenates full arrays
RotateLeft(sliced_array, n)       // Rotates full array
MultiplyWith(sliced_array, other) // Multiplies full arrays
```

### ? Avoid on Sliced Arrays
```
ReplaceAt(sliced_array, idx)      // Uses full array index
Clear(sliced_array)                // Doesn't free properly
Resize(sliced_array, size)        // May conflict with slice
```

---

## Implementation Patterns

### Pattern A: Direct Slice Check (Simplest)
```c
if (AL->Slice)
    return AL->Slice->length;
return AL->count;
```
**Used by**: Size

### Pattern B: Index Mapping
```c
if (AL->Slice) {
    idx = AL->Slice->start + idx * AL->Slice->increment;
}
return AL->contents[idx];
```
**Used by**: GetElement, EraseAt, CopyElement

### Pattern C: Iteration Loop
```c
size_t start = 0, incr = 1, top = AL->count;
if (AL->Slice) {
    start = AL->Slice->start;
    incr = AL->Slice->increment;
    top = AL->Slice->length;
}
for (i = start, count = 0; count < top; i += incr, count++) {
    // process AL->contents[i]
}
```
**Used by**: Apply, ForEach, Accumulate, Max, Min, Fprintf

### Pattern D: Position Calculation
```c
size_t pos = AL->count;
if (AL->Slice) {
    pos = AL->Slice->start + AL->Slice->length * AL->Slice->increment;
}
```
**Used by**: Add

---

## Testing Checklist

### Slice-Aware Functions
- [ ] Test with start != 0
- [ ] Test with increment > 1
- [ ] Test with length < count
- [ ] Test boundary conditions (start + length*increment == count)
- [ ] Test after Add/Remove operations
- [ ] Verify Size() returns slice length, not count
- [ ] Verify no out-of-bounds access

### Slice-Unaware Functions
- [ ] Document behavior when slice is active
- [ ] Test full-array operation with slice set
- [ ] Verify doesn't corrupt slice structure
- [ ] Consider adding warnings in documentation

