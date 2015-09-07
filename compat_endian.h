#ifndef COMPAT_ENDIAN_H_
#define COMPAT_ENDIAN_H_

#ifdef __APPLE__
  #include <libkern/OSByteOrder.h>
  #define be16toh(n) OSSwapBigToHostInt16(n)
  #define be32toh(n) OSSwapBigToHostInt32(n)
  #define be64toh(n) OSSwapBigToHostInt64(n)
  #define htobe16(n) OSSwapHostToBigInt16(n)
  #define htobe32(n) OSSwapHostToBigInt32(n)
  #define htobe64(n) OSSwapHostToBigInt64(n)
#else
  #include <endian.h>
#endif  // __APPLE__

#endif  // COMPAT_ENDIAN_H_
