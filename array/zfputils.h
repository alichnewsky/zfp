#ifndef ZFP_UTILS_H
#define ZFP_UTILS_H

#include <cstddef>

#define unused_(x) ((void)(x))

static int read_header_contents(const zfp::array::header& header, uint& dims, zfp_type& type)
{
  bitstream* bs = stream_open((void *)header.buffer, 96 / CHAR_BIT);
  zfp_stream* stream = zfp_stream_open(bs);
  zfp_field* field = zfp_field_alloc();

  int returnVal;
  if (!zfp_read_header(stream, field, ZFP_HEADER_FULL)) {
    // error reading header
    returnVal = 0;
  } else {
    dims = zfp_field_dimensionality(field);
    type = zfp_field_type(field);

    returnVal = 1;
  }

  zfp_field_free(field);
  zfp_stream_close(stream);
  stream_close(bs);

  return returnVal;
}

zfp::array* zfp::array::construct(const zfp::array::header& header, const uchar* buffer, size_t bufferSize)
{
  uint dims = 0;
  zfp_type type = zfp_type_none;
  if (!read_header_contents(header, dims, type)) {
    // non ZFP header
    return 0;
  }

  try {

    switch (dims) {
      case 3:
#ifdef ZFP_ARRAY3_H
        switch (type) {
          case zfp_type_double:
            return new zfp::array3d(header, buffer, bufferSize);
          case zfp_type_float:
            return new zfp::array3f(header, buffer, bufferSize);
          default:
            break;
        }
#endif
        break;

      case 2:
#ifdef ZFP_ARRAY2_H
        switch (type) {
          case zfp_type_double:
            return new zfp::array2d(header, buffer, bufferSize);
          case zfp_type_float:
            return new zfp::array2f(header, buffer, bufferSize);
          default:
            break;
        }
#endif
        break;

      case 1:
#ifdef ZFP_ARRAY1_H
        switch (type) {
          case zfp_type_double:
            return new zfp::array1d(header, buffer, bufferSize);
          case zfp_type_float:
            return new zfp::array1f(header, buffer, bufferSize);
          default:
            break;
        }
#endif
        break;

      default:
        break;
    }

  } catch (const zfp::header_exception& e) {
    unused_(e);
  }

  return 0;
}

#undef unused_

#endif
