#ifndef ZFP_ARRAY_H
#define ZFP_ARRAY_H

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstring>
#include <string>

#include "zfp.h"
#include "zfp/exception.h"
#include "zfp/memory.h"

// #undef at end of file
#define ZFP_HEADER_SIZE_BITS (ZFP_MAGIC_BITS + ZFP_META_BITS + ZFP_MODE_SHORT_BITS)
// assuming largest wordsize=64, 96 -> 128 bits
#define ZFP_HEADER_PADDED_SIZE_BYTES 16

#define unused_(x) ((void)(x))

namespace zfp {

// abstract base class for compressed array of scalars
class array {
public:
  typedef struct {
    uchar buffer[12];
  } header;

  static zfp::array* construct(const zfp::array::header& header, const uchar* buffer = 0, size_t bufferSize = 0);

protected:
  // default constructor
  array() :
    dims(0), type(zfp_type_none),
    nx(0), ny(0), nz(0),
    bx(0), by(0), bz(0),
    blocks(0), blkbits(0),
    bytes(0), data(0),
    zfp(0),
    shape(0)
  {}

  // generic array with 'dims' dimensions and scalar type 'type'
  array(uint dims, zfp_type type) :
    dims(dims), type(type),
    nx(0), ny(0), nz(0),
    bx(0), by(0), bz(0),
    blocks(0), blkbits(0),
    bytes(0), data(0),
    zfp(zfp_stream_open(0)),
    shape(0)
  {}

  // constructor, from previously-serialized compressed array
  array(uint dims, zfp_type type, const zfp::array::header& h, size_t bufferSize) :
    dims(dims), type(type),
    nx(0), ny(0), nz(0),
    bx(0), by(0), bz(0),
    blocks(0), blkbits(0),
    bytes(0), data(0),
    zfp(zfp_stream_open(0)),
    shape(0)
  {
    // read header to populate member variables associated with zfp_stream
    try {
      read_header(h);
    } catch (zfp::header_exception const & e) {
      unused_(e);
      zfp_stream_close(zfp);
      throw;
    }

    if (bufferSize != 0) {
      // verify buffer is large enough, with what header describes
      uint mx = ((std::max(nx, 1u)) + 3) / 4;
      uint my = ((std::max(ny, 1u)) + 3) / 4;
      uint mz = ((std::max(nz, 1u)) + 3) / 4;
      size_t blocks = (size_t)mx * (size_t)my * (size_t)mz;
      size_t describedSize = ((blocks * zfp->maxbits + stream_word_bits - 1) & ~(stream_word_bits - 1)) / CHAR_BIT;
      if (bufferSize < describedSize) {
        zfp_stream_close(zfp);
        throw zfp::header_exception("ZFP header expects a longer buffer than what was passed in.");
      }
    }

    // everything is valid
    // set remaining class variables, allocate space, copy entire buffer into place

    // set_rate() residual behavior (rate itself was set on zfp_stream* in zfp_read_header())
    blkbits = zfp->maxbits;

    // resize() called in sub-class constructor, followed by memcpy()
  }

  // copy constructor--performs a deep copy
  array(const array& a) :
    data(0),
    zfp(0),
    shape(0)
  {
    deep_copy(a);
  }

  // assignment operator--performs a deep copy
  array& operator=(const array& a)
  {
    deep_copy(a);
    return *this;
  }
 
public:
  // public virtual destructor (can delete array through base class pointer)
  virtual ~array()
  {
    free();
    zfp_stream_close(zfp);
  }

  // rate in bits per value
  double rate() const { return double(blkbits) / block_size(); }

  // set compression rate in bits per value
  double set_rate(double rate)
  {
    rate = zfp_stream_set_rate(zfp, rate, type, dims, 1);
    blkbits = zfp->maxbits;
    alloc();
    return rate;
  }

  // empty cache without compressing modified cached blocks
  virtual void clear_cache() const = 0;

  // flush cache by compressing all modified cached blocks
  virtual void flush_cache() const = 0;

  // number of bytes of compressed data
  size_t compressed_size() const { return bytes; }

  // pointer to compressed data for read or write access
  uchar* compressed_data() const
  {
    // first write back any modified cached data
    flush_cache();
    return data;
  }

  // dimensionality
  uint dimensionality() const { return dims; }

  // underlying scalar type
  zfp_type scalar_type() const { return type; }

  // write header with latest metadata
  void write_header(zfp::array::header& h) const
  {
    // intermediate buffer needed (bitstream accesses multiples of wordsize)
    uchar buffer[ZFP_HEADER_PADDED_SIZE_BYTES];

    DualBitstreamHandle dbh(zfp, buffer, ZFP_HEADER_PADDED_SIZE_BYTES);
    ZfpFieldHandle zfh(type, nx, ny, nz);

    // write header
    zfp_write_header(zfp, zfh.field, ZFP_HEADER_FULL);
    stream_flush(zfp->stream);

    // copy buffer to zfp::array::header
    memcpy(h.buffer, buffer, ZFP_HEADER_SIZE_BITS / CHAR_BIT);
  }

protected:
  // "Handle" classes useful when throwing exceptions in read_header()

  // redirect zfp_stream->bitstream to header while object remains in scope
  class DualBitstreamHandle {
    public:
      bitstream* oldBs;
      bitstream* newBs;
      zfp_stream* zfp;

      DualBitstreamHandle(zfp_stream* zfp, uchar* buffer, size_t bufferSizeBytes) :
        zfp(zfp)
      {
        oldBs = zfp_stream_bit_stream(zfp);
        newBs = stream_open(buffer, bufferSizeBytes);

        stream_rewind(newBs);
        zfp_stream_set_bit_stream(zfp, newBs);
      }

      ~DualBitstreamHandle() {
        zfp_stream_set_bit_stream(zfp, oldBs);
        stream_close(newBs);
      }
  };

  class ZfpFieldHandle {
    public:
      zfp_field* field;

      ZfpFieldHandle() {
        field = zfp_field_alloc();
      }

      ZfpFieldHandle(zfp_type type, int nx, int ny, int nz) {
        field = zfp_field_3d(0, type, nx, ny, nz);
      }

      ~ZfpFieldHandle() {
        zfp_field_free(field);
      }
  };

  // number of values per block
  uint block_size() const { return 1u << (2 * dims); }

  // allocate memory for compressed data
  void alloc(bool clear = true)
  {
    bytes = blocks * blkbits / CHAR_BIT;
    zfp::reallocate_aligned(data, bytes, 0x100u);
    if (clear)
      std::fill(data, data + bytes, 0);
    stream_close(zfp->stream);
    zfp_stream_set_bit_stream(zfp, stream_open(data, bytes));
    clear_cache();
  }

  // free memory associated with compressed data
  void free()
  {
    nx = ny = nz = 0;
    bx = by = bz = 0;
    blocks = 0;
    stream_close(zfp->stream);
    zfp_stream_set_bit_stream(zfp, 0);
    bytes = 0;
    zfp::deallocate_aligned(data);
    data = 0;
    zfp::deallocate(shape);
    shape = 0;
  }

  // perform a deep copy
  void deep_copy(const array& a)
  {
    // copy metadata
    dims = a.dims;
    type = a.type;
    nx = a.nx;
    ny = a.ny;
    nz = a.nz;
    bx = a.bx;
    by = a.by;
    bz = a.bz;
    blocks = a.blocks;
    blkbits = a.blkbits;
    bytes = a.bytes;

    // copy dynamically allocated data
    zfp::clone_aligned(data, a.data, bytes, 0x100u);
    if (zfp) {
      if (zfp->stream)
        stream_close(zfp->stream);
      zfp_stream_close(zfp);
    }
    zfp = zfp_stream_open(0);
    *zfp = *a.zfp;
    zfp_stream_set_bit_stream(zfp, stream_open(data, bytes));
    zfp::clone(shape, a.shape, blocks);
  }

  // returns if dimension lengths consistent with dimensionality
  bool is_valid_dims(uint nx, uint ny, uint nz) const
  {
    bool result = true;
    switch(dims) {
      case 3:
        result &= nz;
      case 2:
        result &= ny;
      case 1:
        result &= nx;
        break;

      default:
        return false;
    }
    return result;
  }

  // attempt reading header from zfp::array::header
  // and verify header contents (throws exceptions upon failure)
  void read_header(const zfp::array::header& h)
  {
    // copy header into padded buffer (bitstream requires multiples of words)
    uchar buffer[ZFP_HEADER_PADDED_SIZE_BYTES];

    size_t headerSizeBytes = ZFP_HEADER_SIZE_BITS / CHAR_BIT;
    memcpy(buffer, h.buffer, headerSizeBytes);
    memset(buffer + headerSizeBytes, 0, ZFP_HEADER_PADDED_SIZE_BYTES - headerSizeBytes);

    DualBitstreamHandle dbh(zfp, buffer, ZFP_HEADER_PADDED_SIZE_BYTES);
    ZfpFieldHandle zfh;

    // read header to populate member variables associated with zfp_stream
    if (zfp_read_header(zfp, zfh.field, ZFP_HEADER_FULL) != ZFP_HEADER_SIZE_BITS) {
      throw zfp::header_exception("Invalid ZFP header.");
    }

    // verify read-header contents
    std::string errMsg = "";
    if (type != zfh.field->type) {
      errMsg += "ZFP header specified an underlying scalar type different than that for this object.";
    }

    if (!is_valid_dims(zfh.field->nx, zfh.field->ny, zfh.field->nz)) {
      if (!errMsg.empty()) {
        errMsg += " ";
      }
      errMsg += "ZFP header specified a dimensionality different than that for this object.";
    }

    if (zfp_stream_compression_mode(zfp) != zfp_mode_fixed_rate) {
      if (!errMsg.empty()) {
        errMsg += " ";
      }
      errMsg += "ZFP header specified a non fixed-rate mode, unsupported by this object.";
    }

    if (!errMsg.empty()) {
      throw zfp::header_exception(errMsg);
    }

    // set class variables
    nx = zfh.field->nx;
    ny = zfh.field->ny;
    nz = zfh.field->nz;
    type = zfh.field->type;
  }

  uint dims;           // array dimensionality (1, 2, or 3)
  zfp_type type;       // scalar type
  uint nx, ny, nz;     // array dimensions
  uint bx, by, bz;     // array dimensions in number of blocks
  uint blocks;         // number of blocks
  size_t blkbits;      // number of bits per compressed block
  size_t bytes;        // total bytes of compressed data
  mutable uchar* data; // pointer to compressed data
  zfp_stream* zfp;     // compressed stream of blocks
  uchar* shape;        // precomputed block dimensions (or null if uniform)
};

#undef unused_
#undef ZFP_HEADER_SIZE_BITS
#undef ZFP_HEADER_PADDED_SIZE_BYTES

}

#endif
