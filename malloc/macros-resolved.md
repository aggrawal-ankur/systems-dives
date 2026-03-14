# Macros Resolved

| Macro | Definition | x86 | x64 | INTERNAL_SIZE_T(4) (ptr:8 bytes) |
| :---- | :--------- | :-- | :-- | :------------------------------- |
| INTERNAL_SIZE_T | `size_t`                  | 4 bytes | 8 bytes  | 4 bytes |
| SIZE_SZ         | `sizeof(INTERNAL_SIZE_T)` | 4 bytes | 8 bytes  | 4 bytes |
| CHUNK_HDR_SZ    | `(2 * SIZE_SZ)`           | 8 bytes | 16 bytes | 8 bytes |
| MIN_CHUNK_SIZE  | `(offsetof(struct malloc_chunk, fd_nextsize))` | 16 bytes | 32 bytes | 24 bytes |
| MALLOC_ALIGNMENT  | `2 * SIZE_SZ` | 8 bytes | 16 bytes | 16 bytes |
| MALLOC_ALIGN_MASK | `(MALLOC_ALIGNMENT - 1)` | 7 bytes | 15 bytes | 7 bytes |
| MINSIZE | | 16 bytes | 32 bytes |
| NBINS | 128 |
| NSMALLBINS | 64 |
| SMALLBIN_WIDTH      | MALLOC_ALIGNMENT | 8 | 16 |
| SMALLBIN_CORRECTION | (MALLOC_ALIGNMENT > CHUNK_HDR_SZ) | 0 | 0 | 1 |
| MIN_LARGE_SIZE      | ((NSMALLBINS-SMALLBIN_CORRECTION) * SMALLBIN_WIDTH) | 64*8 = 512 bytes | 64*16 = 1024 bytes |







#define MALLOC_ALIGNMENT    (2 * SIZE_SZ < __alignof__ (long double) ? __alignof__ (long double) : 2 * SIZE_SZ)
  x86 -> 2*4 < 4  ? 4  : 2*4 -> 8
  x64 -> 2*8 < 16 ? 16 : 2*8 -> 16
  INTERNAL_SIZE_T=4 -> 2*4 < 16 ? 16 : 2*4 -> 16


#define CHUNK_HDR_SZ (2 * SIZE_SZ)
  x86 -> 2*4 = 8
  x64 -> 2*8 = 16
  INTERNAL_SIZE_T=4 -> 2*4 = 8


#define NBINS                 128
#define NSMALLBINS            64
#define SMALLBIN_WIDTH        MALLOC_ALIGNMENT
#define SMALLBIN_CORRECTION  (MALLOC_ALIGNMENT > CHUNK_HDR_SZ)
  x86 -> 8 > 8 => 0
  x64 -> 16 > 16 => 0
  INTERNAL_SIZE_T -> 16 > 8 => 1


#define MIN_LARGE_SIZE      ((NSMALLBINS - SMALLBIN_CORRECTION) * SMALLBIN_WIDTH)
  x86 -> (64-0)*8  => 64*8  => 512
  x64 -> (64-0)*16 => 64*16 => 1024
  INTERNAL_SIZE_T -> (64-1)*16 => 63*16 => 1008

