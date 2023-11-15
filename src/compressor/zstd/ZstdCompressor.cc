/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2023 Intel Corporation
 *
 * Author: Ding Yang <ding.yang@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 */

// -----------------------------------------------------------------------------
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#include <zstd_errors.h>

#include "common/debug.h"
#include "ZstdCompressor.h"
#include "osd/osd_types.h"
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
#define dout_context cct
#define dout_subsys ceph_subsys_compressor
#undef dout_prefix
#define dout_prefix _prefix(_dout)
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------

using std::ostream;

using ceph::bufferlist;
using ceph::bufferptr;

static ostream&
_prefix(std::ostream* _dout)
{
  return *_dout << "ZstdCompressor: ";
}

int ZstdCompressor::zstd_compress(const ceph::buffer::list &src, ceph::buffer::list &dst, std::optional<int32_t> &compressor_message) {
    ZSTD_CStream *s = ZSTD_createCStream();
    ZSTD_initCStream_srcSize(s, cct->_conf->compressor_zstd_level, src.length());
    auto p = src.begin();
    size_t left = src.length();

    size_t const out_max = ZSTD_compressBound(left);
    ceph::buffer::ptr outptr = ceph::buffer::create_small_page_aligned(out_max);
    ZSTD_outBuffer_s outbuf;
    outbuf.dst = outptr.c_str();
    outbuf.size = outptr.length();
    outbuf.pos = 0;

    while (left) {
      ceph_assert(!p.end());
      struct ZSTD_inBuffer_s inbuf;
      inbuf.pos = 0;
      inbuf.size = p.get_ptr_and_advance(left, (const char**)&inbuf.src);
      left -= inbuf.size;
      ZSTD_EndDirective const zed = (left==0) ? ZSTD_e_end : ZSTD_e_continue;
      size_t r = ZSTD_compressStream2(s, &outbuf, &inbuf, zed);
      if (ZSTD_isError(r)) {
	return -EINVAL;
      }
    }
    ceph_assert(p.end());

    ZSTD_freeCStream(s);

    // prefix with decompressed length
    ceph::encode((uint32_t)src.length(), dst);
    dst.append(outptr, 0, outbuf.pos);
    return 0;
  }

int ZstdCompressor::compress(const ceph::buffer::list &src, ceph::buffer::list &dst, std::optional<int32_t> &compressor_message) {
#ifdef HAVE_QATZSTD
    if (qatzstd_enabled)
        return qatzstd_accel.compress(src, dst, compressor_message);
    else
        return zstd_compress(src, dst, compressor_message);
#endif
    return zstd_compress(src, dst, compressor_message);
}

int ZstdCompressor::decompress(const ceph::buffer::list &src, ceph::buffer::list &dst, std::optional<int32_t> compressor_message) {
    auto i = std::cbegin(src);
    return decompress(i, src.length(), dst, compressor_message);
}

int ZstdCompressor::decompress(ceph::buffer::list::const_iterator &p,
	 size_t compressed_len,
	 ceph::buffer::list &dst,
	 std::optional<int32_t> compressor_message) {
    if (compressed_len < 4) {
      return -1;
    }
    compressed_len -= 4;
    uint32_t dst_len;
    ceph::decode(dst_len, p);

    ceph::buffer::ptr dstptr(dst_len);
    ZSTD_outBuffer_s outbuf;
    outbuf.dst = dstptr.c_str();
    outbuf.size = dstptr.length();
    outbuf.pos = 0;
    ZSTD_DStream *s = ZSTD_createDStream();
    ZSTD_initDStream(s);
    while (compressed_len > 0) {
      if (p.end()) {
	return -1;
      }
      ZSTD_inBuffer_s inbuf;
      inbuf.pos = 0;
      inbuf.size = p.get_ptr_and_advance(compressed_len,
					 (const char**)&inbuf.src);
      ZSTD_decompressStream(s, &outbuf, &inbuf);
      compressed_len -= inbuf.size;
    }
    ZSTD_freeDStream(s);

    dst.append(dstptr, 0, outbuf.pos);
    return 0;
}
