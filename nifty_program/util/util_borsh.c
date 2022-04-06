
#ifndef UTIL_BORSH_C
#define UTIL_BORSH_C


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
#define BORSH_SIZE_BOOL                              1
#define BORSH_SIZE_U8                                1
#define BORSH_SIZE_U16                               2
#define BORSH_SIZE_U32                               4
#define BORSH_SIZE_U64                               8
#define BORSH_SIZE_STRING(max_length)               (4 + (max_length))
#define BORSH_SIZE_VEC_U8(max_count)                (4 + (max_count))
#define BORSH_SIZE_PUBKEY                            32
#define BORSH_SIZE_VEC(max_count, element_length)   (4 + (max_count) * (element_length))
#define BORSH_SIZE_ENUM(fields_length)              (1 + (fields_length))
#define BORSH_SIZE_OPTION(fields_length)            (1 + (fields_length))


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
static uint8_t *borsh_encode_Vec_u8(uint8_t *data, uint32_t length, uint8_t *vec)
{
    data = borsh_encode_u32(data, length);

    sol_memcpy(data, vec, length);

    return &(data[length]);
}


// Encode a zero terminated string of max length max_length into a buffer, returning the new end of buffer pointer
static uint8_t *borsh_encode_zero_terminated_string(uint8_t *data, uint32_t max_length, uint8_t *value)
{
    // Figure out the length
    uint32_t length = 0;
    uint8_t *v = value;
    while (*v && (length < max_length)) {
        length++, v++;
    }

    return borsh_encode_Vec_u8(data, length, value);
}


static uint8_t *borsh_encode_pubkey(uint8_t *data, SolPubkey *pubkey)
{
    sol_memcpy(data, pubkey, BORSH_SIZE_PUBKEY);

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

#endif // UTIL_BORSH_C
