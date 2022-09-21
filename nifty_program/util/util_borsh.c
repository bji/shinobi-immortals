#pragma once

// Utilities for Rust Borsh serialization/deserialization which unfortunately alot of on-chain programs use


// These are the individual data types that Borsh can serialize/deserialize.  A Rust struct to be
// serialized/deserialized with Borsh is a sequence of these.

typedef enum
{
    BorshDataType_u8,

    BorshDataType_u16,

    BorshDataType_u32,

    BorshDataType_u64,

    BorshDataType_Vec_u8,

    BorshDataType_String,

    BorshDataType_Pubkey

} BorshDataType;


// Macros that will give the in-memory maximum size of a field of the given data type.  These are used to
// ensure that enough memory is allocated (typically on the stack) to hold a fully serialized version of
// the data type
#define BORSH_SIZE_BOOL                              1UL
#define BORSH_SIZE_U8                                1UL
#define BORSH_SIZE_U16                               2UL
#define BORSH_SIZE_U32                               4UL
#define BORSH_SIZE_U64                               8UL
#define BORSH_SIZE_STRING(max_length)               (4UL + (max_length))
#define BORSH_SIZE_VEC_U8(max_count)                (4UL + (max_count))
#define BORSH_SIZE_PUBKEY                            32UL
#define BORSH_SIZE_VEC(max_count, element_length)   (4UL + (max_count) * (element_length))
#define BORSH_SIZE_ENUM(fields_length)              (1UL + (fields_length))
#define BORSH_SIZE_OPTION(fields_length)            (1UL + (fields_length))


// Encode a bool into a buffer, returning the new end of buffer pointer
static uint8_t *borsh_encode_bool(uint8_t *data, bool value)
{
    *data = value ? 1 : 0;

    return &(data[BORSH_SIZE_BOOL]);
}


// Encode a u8 into a buffer, returning the new end of buffer pointer
static uint8_t *borsh_encode_u8(uint8_t *data, uint8_t value)
{
    *data = value;

    return &(data[BORSH_SIZE_U8]);
}


// Encode a u16 into a buffer, returning the new end of buffer pointer
static uint8_t *borsh_encode_u16(uint8_t *data, uint16_t value)
{
    * (uint16_t *) data = value;

    return &(data[BORSH_SIZE_U16]);
}


// Encode a u32 into a buffer, returning the new end of buffer pointer
static uint8_t *borsh_encode_u32(uint8_t *data, uint32_t value)
{
    * (uint32_t *) data = value;

    return &(data[BORSH_SIZE_U32]);
}


// Encode a u64 into a buffer, returning the new end of buffer pointer
static uint8_t *borsh_encode_u64(uint8_t *data, uint64_t value)
{
    * (uint64_t *) data = value;

    return &(data[BORSH_SIZE_U64]);
}


// Encode a Vec<u8> of length length into a buffer, returning the new end of the buffer pointer
static uint8_t *borsh_encode_Vec_u8(uint8_t *data, uint32_t length, const uint8_t *vec)
{
    data = borsh_encode_u32(data, length);

    sol_memcpy(data, vec, length);

    return &(data[length]);
}


// Encode a zero terminated string of max length max_length into a buffer, returning the new end of buffer pointer
static uint8_t *borsh_encode_zero_terminated_string(uint8_t *data, uint32_t max_length, const uint8_t *value)
{
    // Figure out the length
    uint32_t length = 0;
    const uint8_t *v = value;
    while (*v && (length < max_length)) {
        length++, v++;
    }

    return borsh_encode_Vec_u8(data, length, value);
}


static uint8_t *borsh_encode_pubkey(uint8_t *data, SolPubkey *pubkey)
{
    * (SolPubkey *) data = *pubkey;

    return &(data[BORSH_SIZE_PUBKEY]);
}


static uint8_t *borsh_encode_vec_count(uint8_t *data, uint32_t count)
{
    return borsh_encode_u32(data, count);
}


static uint8_t *borsh_encode_option_none(uint8_t *data)
{
    return borsh_encode_u8(data, 0);
}


static uint8_t *borsh_encode_option_some(uint8_t *data)
{
    return borsh_encode_u8(data, 1);
}


// Returns the number of bytes used in decode, or zero on failure.
// Fills in [ret]
static const uint32_t borsh_decode_bool(const uint8_t *data, uint32_t data_len, bool *ret)
{
    if (data_len < BORSH_SIZE_BOOL) {
        return 0;
    }

    *ret = (bool) *data;

    return BORSH_SIZE_BOOL;
}


// Returns the number of bytes used in decode, or zero on failure.
// Fills in [ret]
static const uint32_t borsh_decode_u8(const uint8_t *data, uint32_t data_len, uint8_t *ret)
{
    if (data_len < BORSH_SIZE_U8) {
        return 0;
    }

    *ret = *data;

    return BORSH_SIZE_U8;
}


// Returns the number of bytes used in decode, or zero on failure.
// Fills in [ret]
static const uint32_t borsh_decode_u16(const uint8_t *data, uint32_t data_len, uint16_t *ret)
{
    if (data_len < BORSH_SIZE_U16) {
        return 0;
    }

    *ret = * (uint16_t *) data;

    return BORSH_SIZE_U16;
}


// Returns the number of bytes used in decode, or zero on failure.
// Fills in [ret]
static const uint32_t borsh_decode_u32(const uint8_t *data, uint32_t data_len, uint32_t *ret)
{
    if (data_len < BORSH_SIZE_U32) {
        return 0;
    }

    *ret = * (uint32_t *) data;

    return BORSH_SIZE_U32;
}


// Returns the number of bytes used in decode, or zero on failure.
// Fills in [ret]
static const uint32_t borsh_decode_u64(const uint8_t *data, uint32_t data_len, uint64_t *ret)
{
    if (data_len < BORSH_SIZE_U64) {
        return 0;
    }

    *ret = * (uint64_t *) data;

    return BORSH_SIZE_U64;
}


// Decodes a u8 elements vector of max length max_length into a buffer, returning the new end of input buffer pointer.
// Returns the number of bytes used in decode, or zero on failure.
// Fills in [value], which must have at least [max_length] bytes available, and [length], with the number of elements
// that were read
static const uint32_t borsh_decode_Vec_u8(const uint8_t *data, uint32_t data_len, uint32_t max_length,
                                          uint8_t *value, uint32_t *length)
{
    uint32_t ret = borsh_decode_u32(data, data_len, length);
    if (!ret) {
        return 0;
    }

    if ((*length > max_length) || (*length > (data_len - ret))) {
        return 0;
    }

    sol_memcpy(value, &(data[ret]), *length);

    return ret + *length;
}

// Decodes a zero terminated string of max length max_length into a buffer, returning the new end of buffer pointer.
// The resulting string will be zero-terminated, and thus [value] must contain at least [max_length + 1] characters.
// Returns the number of bytes used in decode, or zero on failure.
// Fills in [value]
static const uint32_t borsh_decode_string(const uint8_t *data, uint32_t data_len, uint32_t max_length,
                                          uint8_t *value)
{
    uint32_t length;
    uint32_t ret = borsh_decode_Vec_u8(data, data_len, max_length, value, &length);

    if (ret) {
        value[length] = 0;
    }

    return ret;
}


// Returns the number of bytes used in decode, or zero on failure.
// Fills in [pubkey]
static const uint32_t borsh_decode_pubkey(const uint8_t *data, uint32_t data_len, SolPubkey *pubkey)
{
    if (data_len < BORSH_SIZE_PUBKEY) {
        return 0;
    }

    *pubkey = * (SolPubkey *) data;

    return BORSH_SIZE_PUBKEY;
}


// Returns the number of bytes used in decode, or zero on failure.
// Fills in [vec_count], which is the number of elements in the vector; the elements follow in sequence
static uint32_t borsh_decode_vec_count(const uint8_t *data, uint32_t data_len, uint32_t *vec_count)
{
    return borsh_decode_u32(data, data_len, vec_count);
}


// Returns the number of bytes used in decode, or zero on failure.
// Returns true in [option] if the optional value following this option marker is present, returns false in [option]
// if the optional value following this option marker is not present.
static const uint32_t borsh_decode_option(const uint8_t *data, uint32_t data_len, bool *option)
{
    return borsh_decode_bool(data, data_len, option);
}
