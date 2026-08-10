#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "containers.h"
#include "ccl_internal.h"

#ifndef CHUNK_SIZE
#define CHUNK_SIZE 256
#endif

/* Bit zero is the least significant bit of byte zero.  Print and parse
 * present the logical sequence most-significant bit first. */
static const unsigned char bit_mask[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

static int null_error(const char *name)
{
    char message[128];
    snprintf(message, sizeof(message), "iBitString.%s", name);
    iError.RaiseError(message, CONTAINER_ERROR_BADARG);
    return CONTAINER_ERROR_BADARG;
}

static int error_for(const char *name, int code, BitString *b)
{
    char message[128];
    snprintf(message, sizeof(message), "iBitString.%s", name);
    iError.RaiseError(message, code, b);
    return code;
}

static int read_only_error(const char *name, BitString *b)
{
    return error_for(name, CONTAINER_ERROR_READONLY, b);
}

static size_t bytes_for_bits(size_t bits)
{
    /* Keep an addressable byte for an empty object. */
    if (bits == 0)
        return 1;
    if (bits > SIZE_MAX - (CHAR_BIT - 1))
        return SIZE_MAX;
    return (bits + (CHAR_BIT - 1)) / CHAR_BIT;
}

static unsigned char final_mask(size_t bits)
{
    size_t remainder = bits & (CHAR_BIT - 1);
    if (remainder == 0)
        return (unsigned char)~0u;
    return (unsigned char)((1u << remainder) - 1u);
}

static void canonicalize(BitString *b)
{
    size_t bytes;
    if (b == NULL || b->contents == NULL)
        return;
    bytes = bytes_for_bits(b->count);
    if (b->count != 0)
        b->contents[bytes - 1] &= final_mask(b->count);
    if (b->capacity > bytes)
        memset(b->contents + bytes, 0, b->capacity - bytes);
}

static int checked_capacity(size_t bits, size_t *bytes)
{
    size_t value = bytes_for_bits(bits);
    if (value == SIZE_MAX)
        return CONTAINER_ERROR_BADARG;
    *bytes = value;
    return 1;
}

static int ensure_capacity(BitString *b, size_t bits)
{
    size_t needed, capacity, copy;
    unsigned char *contents;

    if (checked_capacity(bits, &needed) < 0)
        return CONTAINER_ERROR_BADARG;
    if (needed <= b->capacity)
        return 1;

    capacity = b->capacity ? b->capacity : 1;
    while (capacity < needed) {
        size_t next = capacity + (capacity > CHUNK_SIZE ? capacity : CHUNK_SIZE);
        if (next <= capacity || next > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity = next;
    }
    contents = b->Allocator->realloc(b->contents, capacity);
    if (contents == NULL)
        return CONTAINER_ERROR_NOMEMORY;
    copy = b->capacity;
    b->contents = contents;
    if (capacity > copy)
        memset(contents + copy, 0, capacity - copy);
    b->capacity = capacity;
    return 1;
}

static int SetCapacity(BitString *b, size_t bitlen)
{
    size_t bytes, copy;
    unsigned char *contents;

    if (b == NULL)
        return null_error("SetCapacity");
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error("SetCapacity", b);
    if (checked_capacity(bitlen, &bytes) < 0)
        return error_for("SetCapacity", CONTAINER_ERROR_BADARG, b);
    if (bytes < bytes_for_bits(b->count))
        return error_for("SetCapacity", CONTAINER_ERROR_BADARG, b);
    if (bytes == b->capacity)
        return 1;

    contents = b->Allocator->malloc(bytes);
    if (contents == NULL)
        return CONTAINER_ERROR_NOMEMORY;
    memset(contents, 0, bytes);
    copy = b->capacity < bytes ? b->capacity : bytes;
    if (copy != 0)
        memcpy(contents, b->contents, copy);
    b->Allocator->free(b->contents);
    b->contents = contents;
    b->capacity = bytes;
    canonicalize(b);
    b->timestamp++;
    return 1;
}

static size_t GetCapacity(BitString *b)
{
    return b == NULL ? (size_t)null_error("GetCapacity") : b->capacity;
}

static size_t GetCount(BitString *b)
{
    return b == NULL ? 0 : b->count;
}

static int Clear(BitString *b)
{
    if (b == NULL)
        return null_error("Clear");
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error("Clear", b);
    if (b->count != 0) {
        b->count = 0;
        memset(b->contents, 0, b->capacity);
        b->timestamp++;
    }
    return 1;
}

static int Finalize(BitString *b)
{
    if (b == NULL)
        return CONTAINER_ERROR_BADARG;
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error("Finalize", b);
    b->Allocator->free(b->contents);
    b->Allocator->free(b);
    return 1;
}

static BitString *create_with_allocator(size_t bits,
                                        const ContainerAllocator *allocator);

static BitString *Copy(BitString *b)
{
    BitString *result;
    size_t bytes;

    if (b == NULL) {
        null_error("Copy");
        return NULL;
    }
    result = create_with_allocator(b->count, b->Allocator);
    if (result == NULL)
        return NULL;
    result->count = b->count;
    result->Flags = b->Flags;
    bytes = bytes_for_bits(b->count);
    memcpy(result->contents, b->contents, bytes);
    canonicalize(result);
    return result;
}

static int SetElement(BitString *b, size_t position, int value)
{
    if (b == NULL)
        return null_error("SetElement");
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error("SetElement", b);
    if (position >= b->count)
        return error_for("SetElement", CONTAINER_ERROR_INDEX, b);
    if (value)
        b->contents[position / CHAR_BIT] |= bit_mask[position & (CHAR_BIT - 1)];
    else
        b->contents[position / CHAR_BIT] &=
            (unsigned char)~bit_mask[position & (CHAR_BIT - 1)];
    b->timestamp++;
    return 1;
}

static int GetElement(BitString *b, size_t position)
{
    if (b == NULL) {
        null_error("GetElement");
        return 0;
    }
    if (position >= b->count)
        return 0;
    return (b->contents[position / CHAR_BIT] &
            bit_mask[position & (CHAR_BIT - 1)]) != 0;
}

static int shift_bits(BitString *b, size_t shift, int left)
{
    size_t i, old_count;

    if (b == NULL)
        return null_error(left ? "BitLeftShift" : "BitRightShift");
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error(left ? "BitLeftShift" : "BitRightShift", b);
    old_count = b->count;
    if (shift >= old_count) {
        memset(b->contents, 0, bytes_for_bits(old_count));
    } else if (shift != 0) {
        if (left) {
            for (i = old_count; i-- > 0;) {
                int value = i >= shift ? GetElement(b, i - shift) : 0;
                if (value)
                    b->contents[i / CHAR_BIT] |= bit_mask[i & (CHAR_BIT - 1)];
                else
                    b->contents[i / CHAR_BIT] &=
                        (unsigned char)~bit_mask[i & (CHAR_BIT - 1)];
            }
        } else {
            for (i = 0; i < old_count; ++i) {
                int value = i + shift < old_count ? GetElement(b, i + shift) : 0;
                if (value)
                    b->contents[i / CHAR_BIT] |= bit_mask[i & (CHAR_BIT - 1)];
                else
                    b->contents[i / CHAR_BIT] &=
                        (unsigned char)~bit_mask[i & (CHAR_BIT - 1)];
            }
        }
    }
    canonicalize(b);
    if (shift != 0)
        b->timestamp++;
    return 1;
}

static int BitRightShift(BitString *b, size_t shift)
{
    return shift_bits(b, shift, 0);
}

static int BitLeftShift(BitString *b, size_t shift)
{
    return shift_bits(b, shift, 1);
}

static BitString *GetRange(BitString *b, size_t start, size_t end)
{
    BitString *result;
    size_t i;

    if (b == NULL) {
        null_error("GetRange");
        return NULL;
    }
    if (start > end || start > b->count || end > b->count) {
        error_for("GetRange", CONTAINER_ERROR_INDEX, b);
        return NULL;
    }
    result = create_with_allocator(end - start, b->Allocator);
    if (result == NULL)
        return NULL;
    result->count = end - start;
    for (i = 0; i < result->count; ++i) {
        if (GetElement(b, start + i))
            result->contents[i / CHAR_BIT] |= bit_mask[i & (CHAR_BIT - 1)];
    }
    canonicalize(result);
    return result;
}

static int LessEqual(BitString *left, BitString *right)
{
    size_t i, count;
    if (left == NULL || right == NULL)
        return null_error("LessEqual");
    count = left->count > right->count ? left->count : right->count;
    for (i = 0; i < count; ++i)
        if (GetElement(left, i) && !GetElement(right, i))
            return 0;
    return 1;
}

static int Equal(BitString *left, BitString *right)
{
    size_t i;
    if (left == right)
        return left != NULL;
    if (left == NULL || right == NULL)
        return 0;
    if (left->Flags != right->Flags || left->count != right->count)
        return 0;
    for (i = 0; i < left->count; ++i)
        if (GetElement(left, i) != GetElement(right, i))
            return 0;
    return 1;
}

static int algebra_args(BitString *left, BitString *right, const char *name)
{
    if (left == NULL || right == NULL)
        return null_error(name);
    return 1;
}

enum algebra_op { OP_OR, OP_AND, OP_NAND, OP_XOR };

static int algebra_value(enum algebra_op op, int left, int right)
{
    switch (op) {
    case OP_OR: return left | right;
    case OP_AND: return left & right;
    case OP_NAND: return !(left & right);
    default: return left ^ right;
    }
}

static BitString *algebra(BitString *left, BitString *right, enum algebra_op op,
                          const char *name)
{
    BitString *result;
    size_t i, count;
    if (algebra_args(left, right, name) < 0)
        return NULL;
    count = left->count > right->count ? left->count : right->count;
    result = create_with_allocator(count, left->Allocator);
    if (result == NULL)
        return NULL;
    result->count = count;
    for (i = 0; i < count; ++i)
        if (algebra_value(op, GetElement(left, i), GetElement(right, i)))
            result->contents[i / CHAR_BIT] |= bit_mask[i & (CHAR_BIT - 1)];
    canonicalize(result);
    return result;
}

static int algebra_assign(BitString *left, BitString *right,
                          enum algebra_op op, const char *name)
{
    size_t i;
    if (algebra_args(left, right, name) < 0)
        return CONTAINER_ERROR_BADARG;
    if (left->Flags & CONTAINER_READONLY)
        return read_only_error(name, left);
    for (i = 0; i < left->count; ++i) {
        int value = algebra_value(op, GetElement(left, i), GetElement(right, i));
        if (value)
            left->contents[i / CHAR_BIT] |= bit_mask[i & (CHAR_BIT - 1)];
        else
            left->contents[i / CHAR_BIT] &=
                (unsigned char)~bit_mask[i & (CHAR_BIT - 1)];
    }
    canonicalize(left);
    left->timestamp++;
    return 1;
}

static BitString *Or(BitString *left, BitString *right)
{ return algebra(left, right, OP_OR, "Or"); }

static int OrAssign(BitString *left, BitString *right)
{ return algebra_assign(left, right, OP_OR, "OrAssign"); }

static BitString *And(BitString *left, BitString *right)
{ return algebra(left, right, OP_AND, "And"); }

static int AndAssign(BitString *left, BitString *right)
{ return algebra_assign(left, right, OP_AND, "AndAssign"); }

static BitString *NotAnd(BitString *left, BitString *right)
{ return algebra(left, right, OP_NAND, "Nand"); }

static int NotAndAssign(BitString *left, BitString *right)
{ return algebra_assign(left, right, OP_NAND, "NandAssign"); }

static BitString *Xor(BitString *left, BitString *right)
{ return algebra(left, right, OP_XOR, "Xor"); }

static int XorAssign(BitString *left, BitString *right)
{ return algebra_assign(left, right, OP_XOR, "XorAssign"); }

static BitString *Not(BitString *b)
{
    BitString *result;
    size_t i;
    if (b == NULL) {
        null_error("Not");
        return NULL;
    }
    result = create_with_allocator(b->count, b->Allocator);
    if (result == NULL)
        return NULL;
    result->count = b->count;
    for (i = 0; i < b->count; ++i)
        if (!GetElement(b, i))
            result->contents[i / CHAR_BIT] |= bit_mask[i & (CHAR_BIT - 1)];
    canonicalize(result);
    return result;
}

static int NotAssign(BitString *b)
{
    size_t i;
    if (b == NULL)
        return null_error("NotAssign");
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error("NotAssign", b);
    for (i = 0; i < b->count; ++i) {
        if (GetElement(b, i))
            b->contents[i / CHAR_BIT] &=
                (unsigned char)~bit_mask[i & (CHAR_BIT - 1)];
        else
            b->contents[i / CHAR_BIT] |= bit_mask[i & (CHAR_BIT - 1)];
    }
    canonicalize(b);
    b->timestamp++;
    return 1;
}

static BitString *InitializeWith(size_t size, void *data)
{
    BitString *result;
    if (data == NULL && size != 0) {
        null_error("InitializeWith");
        return NULL;
    }
    if (size > SIZE_MAX / CHAR_BIT)
        return NULL;
    result = create_with_allocator(size * CHAR_BIT, CurrentAllocator);
    if (result == NULL)
        return NULL;
    if (size != 0)
        memcpy(result->contents, data, size);
    result->count = size * CHAR_BIT;
    canonicalize(result);
    return result;
}

static int AddRange(BitString *b, size_t bit_size, void *data)
{
    const unsigned char *source = (const unsigned char *)data;
    unsigned char *snapshot = NULL;
    size_t source_bytes, old_count, i;
    uintptr_t source_start, source_end, contents_start, contents_end;

    if (b == NULL)
        return null_error("AddRange");
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error("AddRange", b);
    if (bit_size == 0)
        return 1;
    if (data == NULL)
        return null_error("AddRange");
    if (b->count > SIZE_MAX - bit_size)
        return error_for("AddRange", CONTAINER_ERROR_BADARG, b);
    source_bytes = bytes_for_bits(bit_size);
    contents_start = (uintptr_t)b->contents;
    contents_end = contents_start + b->capacity;
    source_start = (uintptr_t)data;
    source_end = source_start + source_bytes;
    if (source_start >= contents_start && source_start < contents_end &&
        source_end >= source_start && source_end <= contents_end) {
        snapshot = b->Allocator->malloc(source_bytes);
        if (snapshot == NULL)
            return CONTAINER_ERROR_NOMEMORY;
        memcpy(snapshot, data, source_bytes);
        source = snapshot;
    }
    old_count = b->count;
    if (ensure_capacity(b, old_count + bit_size) < 0) {
        if (snapshot != NULL)
            b->Allocator->free(snapshot);
        return CONTAINER_ERROR_NOMEMORY;
    }
    for (i = 0; i < bit_size; ++i) {
        size_t position = old_count + i;
        if (source[i / CHAR_BIT] & bit_mask[i & (CHAR_BIT - 1)])
            b->contents[position / CHAR_BIT] |= bit_mask[position & (CHAR_BIT - 1)];
    }
    b->count = old_count + bit_size;
    canonicalize(b);
    b->timestamp++;
    if (snapshot != NULL)
        b->Allocator->free(snapshot);
    return 1;
}

static BitString *StringToBitString(unsigned char *text)
{
    BitString *result;
    size_t count = 0, position;
    const unsigned char *p;

    if (text == NULL)
        return NULL;
    p = text;
    if (p[0] == '0') {
        const unsigned char *prefix = p + 1;
        if (*prefix == 'b' || *prefix == 'B')
            p += 2;
    }
    for (; *p != 0; ++p) {
        if (*p == ' ' || *p == '\t' || *p == ',')
            continue;
        if (*p != '0' && *p != '1')
            return NULL;
        if (count == SIZE_MAX)
            return NULL;
        ++count;
    }
    if (count == 0)
        return NULL;
    result = create_with_allocator(count, CurrentAllocator);
    if (result == NULL)
        return NULL;
    result->count = count;
    position = count;
    for (p = text; *p != 0;) {
        if (p == text && p[0] == '0') {
            const unsigned char *prefix = p + 1;
            if (*prefix == 'b' || *prefix == 'B') {
                p += 2;
                continue;
            }
        }
        if (*p == ' ' || *p == '\t' || *p == ',') {
            ++p;
            continue;
        }
        --position;
        if (*p++ == '1')
            result->contents[position / CHAR_BIT] |= bit_mask[position & (CHAR_BIT - 1)];
    }
    canonicalize(result);
    return result;
}

static size_t Print(BitString *b, size_t bufsiz, unsigned char *out)
{
    size_t i, remaining, needed = 1, written = 0, emitted = 0;
    if (b == NULL)
        return 0;
    remaining = b->count;
    for (i = b->count; i != 0; --i) {
        if (emitted != 0 && remaining % 4 == 0)
            ++needed;
        if (emitted != 0 && remaining % 8 == 0)
            ++needed;
        ++needed;
        ++emitted;
        --remaining;
    }
    remaining = b->count;
    for (i = b->count; i != 0; --i) {
        size_t index = i - 1;
        if (written != 0 && remaining % 4 == 0) {
            if (out != NULL && written + 1 < bufsiz)
                out[written] = ' ';
            ++written;
        }
        if (written != 0 && remaining % 8 == 0) {
            if (out != NULL && written + 1 < bufsiz)
                out[written] = ' ';
            ++written;
        }
        if (out != NULL && written + 1 < bufsiz)
            out[written] = GetElement(b, index) ? '1' : '0';
        ++written;
        --remaining;
    }
    if (out != NULL && bufsiz != 0)
        out[written < bufsiz ? written : bufsiz - 1] = 0;
    return needed;
}

static int Append(BitString *left, BitString *right)
{
    unsigned char *snapshot = NULL;
    const unsigned char *source;
    size_t source_bytes, old_count, i;
    uintptr_t source_start, source_end, contents_start, contents_end;

    if (left == NULL || right == NULL)
        return null_error("Append");
    if (left->Flags & CONTAINER_READONLY)
        return read_only_error("Append", left);
    if (right->count == 0)
        return 1;
    if (left->count > SIZE_MAX - right->count)
        return error_for("Append", CONTAINER_ERROR_BADARG, left);
    source_bytes = bytes_for_bits(right->count);
    source = right->contents;
    contents_start = (uintptr_t)left->contents;
    contents_end = contents_start + left->capacity;
    source_start = (uintptr_t)source;
    source_end = source_start + source_bytes;
    if (source_start >= contents_start && source_start < contents_end &&
        source_end >= source_start && source_end <= contents_end) {
        snapshot = left->Allocator->malloc(source_bytes);
        if (snapshot == NULL)
            return CONTAINER_ERROR_NOMEMORY;
        memcpy(snapshot, source, source_bytes);
        source = snapshot;
    }
    old_count = left->count;
    if (ensure_capacity(left, old_count + right->count) < 0) {
        if (snapshot != NULL)
            left->Allocator->free(snapshot);
        return CONTAINER_ERROR_NOMEMORY;
    }
    for (i = 0; i < right->count; ++i) {
        size_t position = old_count + i;
        if (source[i / CHAR_BIT] & bit_mask[i & (CHAR_BIT - 1)])
            left->contents[position / CHAR_BIT] |= bit_mask[position & (CHAR_BIT - 1)];
        else
            left->contents[position / CHAR_BIT] &=
                (unsigned char)~bit_mask[position & (CHAR_BIT - 1)];
    }
    left->count += right->count;
    canonicalize(left);
    left->timestamp++;
    if (snapshot != NULL)
        left->Allocator->free(snapshot);
    return 1;
}

static BitString *Reverse(BitString *b)
{
    BitString *result;
    size_t i;
    if (b == NULL)
        return NULL;
    result = create_with_allocator(b->count, b->Allocator);
    if (result == NULL)
        return NULL;
    result->count = b->count;
    for (i = 0; i < b->count; ++i)
        if (GetElement(b, b->count - i - 1))
            result->contents[i / CHAR_BIT] |= bit_mask[i & (CHAR_BIT - 1)];
    canonicalize(result);
    return result;
}

static size_t Sizeof(BitString *b)
{
    return sizeof(BitString) + (b == NULL ? 0 : b->capacity);
}

static unsigned GetFlags(BitString *b)
{
    return b == NULL ? 0 : b->Flags;
}

static unsigned SetFlags(BitString *b, unsigned value)
{
    unsigned old;
    if (b == NULL)
        return 0;
    old = b->Flags;
    b->Flags = value;
    if (old != value)
        b->timestamp++;
    return old;
}

static int Add(BitString *b, int value)
{
    size_t position;
    if (b == NULL)
        return null_error("Add");
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error("Add", b);
    if (b->count == SIZE_MAX)
        return error_for("Add", CONTAINER_ERROR_BADARG, b);
    if (ensure_capacity(b, b->count + 1) < 0)
        return CONTAINER_ERROR_NOMEMORY;
    position = b->count++;
    if (value)
        b->contents[position / CHAR_BIT] |= bit_mask[position & (CHAR_BIT - 1)];
    else
        b->contents[position / CHAR_BIT] &=
            (unsigned char)~bit_mask[position & (CHAR_BIT - 1)];
    canonicalize(b);
    b->timestamp++;
    return 1;
}

static int ReplaceAt(BitString *b, size_t index, int value)
{
    if (b == NULL)
        return null_error("ReplaceAt");
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error("ReplaceAt", b);
    if (index >= b->count)
        return error_for("ReplaceAt", CONTAINER_ERROR_INDEX, b);
    if (value)
        b->contents[index / CHAR_BIT] |= bit_mask[index & (CHAR_BIT - 1)];
    else
        b->contents[index / CHAR_BIT] &=
            (unsigned char)~bit_mask[index & (CHAR_BIT - 1)];
    b->timestamp++;
    return 1;
}

static int PopBack(BitString *b)
{
    size_t index;
    int result;
    if (b == NULL)
        return null_error("PopBack");
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error("PopBack", b);
    if (b->count == 0)
        return error_for("PopBack", CONTAINER_ERROR_INDEX, b);
    index = b->count - 1;
    result = GetElement(b, index);
    --b->count;
    b->contents[index / CHAR_BIT] &=
        (unsigned char)~bit_mask[index & (CHAR_BIT - 1)];
    canonicalize(b);
    b->timestamp++;
    return result;
}

static int Apply(BitString *b, int (*fn)(int, void *), void *arg)
{
    size_t i;
    if (b == NULL)
        return null_error("Apply");
    if (fn == NULL)
        return null_error("Apply");
    for (i = 0; i < b->count; ++i)
        fn(GetElement(b, i), arg);
    return 1;
}

static size_t InsertAt(BitString *b, size_t index, int value)
{
    size_t i, old_count;
    if (b == NULL)
        return (size_t)null_error("InsertAt");
    if (b->Flags & CONTAINER_READONLY) {
        read_only_error("InsertAt", b);
        return 0;
    }
    if (index > b->count) {
        error_for("InsertAt", CONTAINER_ERROR_INDEX, b);
        return 0;
    }
    old_count = b->count;
    if (old_count == SIZE_MAX || ensure_capacity(b, old_count + 1) < 0)
        return 0;
    b->count = old_count + 1;
    for (i = old_count; i > index; --i) {
        int bit = GetElement(b, i - 1);
        if (bit)
            b->contents[i / CHAR_BIT] |= bit_mask[i & (CHAR_BIT - 1)];
        else
            b->contents[i / CHAR_BIT] &=
                (unsigned char)~bit_mask[i & (CHAR_BIT - 1)];
    }
    if (value)
        b->contents[index / CHAR_BIT] |= bit_mask[index & (CHAR_BIT - 1)];
    else
        b->contents[index / CHAR_BIT] &=
            (unsigned char)~bit_mask[index & (CHAR_BIT - 1)];
    canonicalize(b);
    b->timestamp++;
    return b->count;
}

static size_t Insert(BitString *b, int value)
{
    return InsertAt(b, 0, value);
}

static int EraseAt(BitString *b, size_t index)
{
    size_t i;
    if (b == NULL)
        return null_error("EraseAt");
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error("EraseAt", b);
    if (index >= b->count)
        return error_for("EraseAt", CONTAINER_ERROR_INDEX, b);
    for (i = index; i + 1 < b->count; ++i) {
        int bit = GetElement(b, i + 1);
        if (bit)
            b->contents[i / CHAR_BIT] |= bit_mask[i & (CHAR_BIT - 1)];
        else
            b->contents[i / CHAR_BIT] &=
                (unsigned char)~bit_mask[i & (CHAR_BIT - 1)];
    }
    --b->count;
    b->contents[b->count / CHAR_BIT] &=
        (unsigned char)~bit_mask[b->count & (CHAR_BIT - 1)];
    canonicalize(b);
    b->timestamp++;
    return 1;
}

static int IndexOf(BitString *b, int bit, void *extra, size_t *result)
{
    size_t i;
    (void)extra;
    if (b == NULL)
        return null_error("IndexOf");
    if (result == NULL)
        return null_error("IndexOf");
    for (i = 0; i < b->count; ++i)
        if (GetElement(b, i) == !!bit) {
            *result = i;
            return 1;
        }
    return CONTAINER_ERROR_NOTFOUND;
}

static int Erase(BitString *b, int value)
{
    size_t index;
    int result = IndexOf(b, value, NULL, &index);
    if (result < 0)
        return result;
    return EraseAt(b, index);
}

static int Memset(BitString *b, size_t start, size_t stop, int value)
{
    size_t i;
    if (b == NULL)
        return null_error("Memset");
    if (b->Flags & CONTAINER_READONLY)
        return read_only_error("Memset", b);
    /* The historical API uses an inclusive stop index. */
    if (start >= b->count || stop < start)
        return error_for("Memset", CONTAINER_ERROR_INDEX, b);
    if (stop >= b->count)
        stop = b->count - 1;
    for (i = start; i <= stop; ++i) {
        if (value)
            b->contents[i / CHAR_BIT] |= bit_mask[i & (CHAR_BIT - 1)];
        else
            b->contents[i / CHAR_BIT] &=
                (unsigned char)~bit_mask[i & (CHAR_BIT - 1)];
    }
    canonicalize(b);
    b->timestamp++;
    return 1;
}

static uintmax_t PopulationCount(BitString *b)
{
    uintmax_t result = 0;
    size_t i;
    if (b == NULL)
        return 0;
    for (i = 0; i < b->count; ++i)
        result += GetElement(b, i) != 0;
    return result;
}

static uintmax_t BitBlockCount(BitString *b)
{
    uintmax_t result = 0;
    size_t i;
    int previous = 0;
    if (b == NULL)
        return 0;
    for (i = 0; i < b->count; ++i) {
        int current = GetElement(b, i);
        if (current && !previous)
            ++result;
        previous = current;
    }
    return result;
}

static int bitBitstr(BitString *text, BitString *pattern)
{
    size_t start, offset;
    if (text == NULL || pattern == NULL)
        return null_error("Contains");
    if (pattern->count == 0)
        return 1;
    if (pattern->count > text->count)
        return 0;
    for (start = 0; start + pattern->count <= text->count; ++start) {
        for (offset = 0; offset < pattern->count; ++offset)
            if (GetElement(text, start + offset) != GetElement(pattern, offset))
                break;
        if (offset == pattern->count)
            return 1;
    }
    return 0;
}

static int Contains(BitString *text, BitString *pattern, void *args)
{
    (void)args;
    return bitBitstr(text, pattern);
}

struct BitstringIterator {
    Iterator it;
    BitString *bits;
    size_t index;
    size_t timestamp;
    int bit;
    int owns_storage;
};

static void *GetCurrent(Iterator *it);

static struct BitstringIterator *as_bit_iterator(Iterator *it, const char *name)
{
    if (it == NULL) {
        null_error(name);
        return NULL;
    }
    return (struct BitstringIterator *)it;
}

static int iterator_valid(struct BitstringIterator *iterator)
{
    if (iterator->bits == NULL)
        return 0;
    if (iterator->timestamp != iterator->bits->timestamp) {
        iError.RaiseError("iBitString.Iterator", CONTAINER_ERROR_OBJECT_CHANGED,
                          iterator->bits);
        return 0;
    }
    return 1;
}

static void *GetNext(Iterator *it)
{
    struct BitstringIterator *iterator = as_bit_iterator(it, "GetNext");
    if (iterator == NULL || !iterator_valid(iterator) ||
        iterator->index >= iterator->bits->count)
        return NULL;
    iterator->bit = GetElement(iterator->bits, iterator->index++);
    return &iterator->bit;
}

static void *GetPrevious(Iterator *it)
{
    struct BitstringIterator *iterator = as_bit_iterator(it, "GetPrevious");
    if (iterator == NULL || !iterator_valid(iterator) || iterator->index == 0)
        return NULL;
    iterator->bit = GetElement(iterator->bits, --iterator->index);
    return &iterator->bit;
}

static void *GetFirst(Iterator *it)
{
    struct BitstringIterator *iterator = as_bit_iterator(it, "GetFirst");
    if (iterator == NULL || !iterator_valid(iterator))
        return NULL;
    iterator->index = 0;
    return GetNext(it);
}

static void *GetLast(Iterator *it)
{
    struct BitstringIterator *iterator = as_bit_iterator(it, "GetLast");
    if (iterator == NULL || !iterator_valid(iterator) || iterator->bits->count == 0)
        return NULL;
    iterator->index = iterator->bits->count;
    return GetPrevious(it);
}

static void *Seek(Iterator *it, size_t position)
{
    struct BitstringIterator *iterator = as_bit_iterator(it, "Seek");
    if (iterator == NULL || !iterator_valid(iterator) ||
        position >= iterator->bits->count)
        return NULL;
    iterator->index = position + 1;
    iterator->bit = GetElement(iterator->bits, position);
    return &iterator->bit;
}

static size_t GetPosition(Iterator *it)
{
    struct BitstringIterator *iterator = as_bit_iterator(it, "GetPosition");
    return iterator == NULL ? 0 : iterator->index;
}

static int IteratorReplace(Iterator *it, void *data, int direction)
{
    struct BitstringIterator *iterator = as_bit_iterator(it, "Replace");
    (void)direction;
    if (iterator == NULL || data == NULL || iterator->index == 0)
        return CONTAINER_ERROR_BADARG;
    return SetElement(iterator->bits, iterator->index - 1, *(int *)data);
}

static Iterator *NewIterator(BitString *b)
{
    struct BitstringIterator *iterator;
    if (b == NULL) {
        null_error("NewIterator");
        return NULL;
    }
    iterator = b->Allocator->malloc(sizeof(*iterator));
    if (iterator == NULL)
        return NULL;
    memset(iterator, 0, sizeof(*iterator));
    iterator->owns_storage = 1;
    iterator->bits = b;
    iterator->timestamp = b->timestamp;
    iterator->it.GetNext = GetNext;
    iterator->it.GetPrevious = GetPrevious;
    iterator->it.GetFirst = GetFirst;
    iterator->it.GetCurrent = GetCurrent;
    iterator->it.GetLast = GetLast;
    iterator->it.Seek = Seek;
    iterator->it.GetPosition = GetPosition;
    iterator->it.Replace = IteratorReplace;
    return &iterator->it;
}

static void *GetCurrent(Iterator *it)
{
    struct BitstringIterator *iterator = as_bit_iterator(it, "GetCurrent");
    if (iterator == NULL || !iterator_valid(iterator))
        return NULL;
    return &iterator->bit;
}

static int InitIterator(BitString *b, void *storage)
{
    struct BitstringIterator *iterator = storage;
    if (b == NULL || storage == NULL)
        return null_error("InitIterator");
    memset(iterator, 0, sizeof(*iterator));
    iterator->bits = b;
    iterator->timestamp = b->timestamp;
    iterator->it.GetNext = GetNext;
    iterator->it.GetPrevious = GetPrevious;
    iterator->it.GetFirst = GetFirst;
    iterator->it.GetCurrent = GetCurrent;
    iterator->it.GetLast = GetLast;
    iterator->it.Seek = Seek;
    iterator->it.GetPosition = GetPosition;
    iterator->it.Replace = IteratorReplace;
    return 1;
}

static int DeleteIterator(Iterator *it)
{
    struct BitstringIterator *iterator = as_bit_iterator(it, "DeleteIterator");
    if (iterator == NULL)
        return CONTAINER_ERROR_BADARG;
    if (iterator->owns_storage)
        iterator->bits->Allocator->free(iterator);
    return 1;
}

static int Save(const BitString *b, FILE *stream, SaveFunction save_fn, void *arg)
{
    size_t bytes;
    (void)save_fn;
    (void)arg;
    if (b == NULL || stream == NULL)
        return null_error("Save");
    bytes = bytes_for_bits(b->count);
    if (fwrite(&b->count, sizeof(b->count), 1, stream) != 1 ||
        fwrite(&b->Flags, sizeof(b->Flags), 1, stream) != 1 ||
        fwrite(b->contents, 1, bytes, stream) != bytes)
        return CONTAINER_ERROR_FILE_WRITE;
    return 0;
}

static BitString *Load(FILE *stream, ReadFunction read_fn, void *arg)
{
    size_t count, bytes;
    unsigned flags;
    BitString *result;
    (void)read_fn;
    (void)arg;
    if (stream == NULL) {
        null_error("Load");
        return NULL;
    }
    if (fread(&count, sizeof(count), 1, stream) != 1 ||
        fread(&flags, sizeof(flags), 1, stream) != 1)
        return NULL;
    if (bytes_for_bits(count) == SIZE_MAX)
        return NULL;
    result = create_with_allocator(count, CurrentAllocator);
    if (result == NULL)
        return NULL;
    bytes = bytes_for_bits(count);
    if (fread(result->contents, 1, bytes, stream) != bytes) {
        result->Allocator->free(result->contents);
        result->Allocator->free(result);
        iError.RaiseError("iBitString.Load", CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    result->count = count;
    result->Flags = flags;
    canonicalize(result);
    return result;
}

static ErrorFunction *SetErrorFunction(BitString *b, ErrorFunction fn)
{
    (void)b;
    (void)fn;
    return NULL;
}

static size_t GetElementSize(BitString *b)
{
    (void)b;
    return 1;
}

static unsigned char *GetData(BitString *b)
{
    if (b == NULL) {
        null_error("GetData");
        return NULL;
    }
    return b->contents;
}

static int CopyBits(BitString *b, void *buffer)
{
    size_t bytes;
    if (b == NULL)
        return null_error("CopyBits");
    if (buffer == NULL)
        return error_for("CopyBits", CONTAINER_ERROR_BADARG, b);
    bytes = bytes_for_bits(b->count);
    memcpy(buffer, b->contents, bytes);
    return 1;
}

static BitString *Init(BitString *storage, size_t bitlen)
{
    size_t bytes;
    if (storage == NULL) {
        null_error("Init");
        return NULL;
    }
    if (checked_capacity(bitlen, &bytes) < 0)
        return NULL;
    memset(storage, 0, sizeof(*storage));
    storage->contents = CurrentAllocator->malloc(bytes);
    if (storage->contents == NULL)
        return NULL;
    memset(storage->contents, 0, bytes);
    storage->VTable = &iBitString;
    storage->Allocator = CurrentAllocator;
    storage->capacity = bytes;
    return storage;
}

static const ContainerAllocator *GetAllocator(const BitString *b)
{
    return b == NULL ? NULL : b->Allocator;
}

static BitString *create_with_allocator(size_t bits,
                                        const ContainerAllocator *allocator)
{
    BitString *result;
    size_t bytes;
    if (allocator == NULL)
        allocator = CurrentAllocator;
    if (allocator == NULL || allocator->malloc == NULL ||
        allocator->free == NULL)
        return NULL;
    if (checked_capacity(bits, &bytes) < 0)
        return NULL;
    result = allocator->malloc(sizeof(*result));
    if (result == NULL)
        return NULL;
    memset(result, 0, sizeof(*result));
    result->contents = allocator->malloc(bytes);
    if (result->contents == NULL) {
        allocator->free(result);
        return NULL;
    }
    memset(result->contents, 0, bytes);
    result->VTable = &iBitString;
    result->Allocator = allocator;
    result->capacity = bytes;
    return result;
}

static BitString *CreateWithAllocator(size_t bitlen,
                                      const ContainerAllocator *allocator)
{
    return create_with_allocator(bitlen, allocator);
}

static BitString *Create(size_t bitlen)
{
    return create_with_allocator(bitlen, CurrentAllocator);
}

BitStringInterface iBitString = {
    GetCount, GetFlags, SetFlags, Clear, Contains, Erase, Finalize, Apply,
    Equal, Copy, SetErrorFunction, Sizeof, NewIterator, InitIterator,
    DeleteIterator, Save, Load, GetElementSize,
    Add, GetElement, Add, PopBack, InsertAt, EraseAt, ReplaceAt, IndexOf,
    Insert, SetElement, GetCapacity, SetCapacity,
    Or, OrAssign, And, AndAssign, NotAnd, NotAndAssign, Xor, XorAssign,
    Not, NotAssign, PopulationCount, BitBlockCount, LessEqual, Reverse,
    GetRange, StringToBitString, InitializeWith, BitLeftShift, BitRightShift,
    Print, Append, Memset, CreateWithAllocator, Create, Init, GetData,
    CopyBits, AddRange, GetAllocator
};
