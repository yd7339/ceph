// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2015 Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_ZSTDCOMPRESSOR_H
#define CEPH_ZSTDCOMPRESSOR_H

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd/lib/zstd.h"
#include "qatseqprod.h"

//#include "zstd.h"
#include "include/buffer.h"
#include "include/encoding.h"
#include "compressor/Compressor.h"
// #include "osd/osd_types.h"
// #include "common/ceph_context.h"
// #include "common/common_init.h"
// #include "common/debug.h"
// #include "common/dout.h"
// #include "common/errno.h"

//-----------------------------------------------------------------------------
// #define dout_context g_ceph_context
// #define dout_subsys ceph_subsys_compressor
// #undef dout_prefix
// #define dout_prefix _prefix(_dout)

// using std::ostream;

// static std::ostream& _prefix(std::ostream* _dout)
// {
//   return *_dout << "QZstdCompressor: ";
// }

class ZstdCompressor : public Compressor {
 public:
  ZstdCompressor(CephContext *cct) : Compressor(COMP_ALG_ZSTD, "zstd"), cct(cct) {
#ifdef HAVE_QATZSTD
      if (cct->_conf->qat_compressor_enabled)
        qatzstd_enabled = true;
      else
        qatzstd_enabled = false;
#endif
  }
  int compress(const ceph::buffer::list &src, ceph::buffer::list &dst, std::optional<int32_t> &compressor_message) override {
    ZSTD_CStream *s = ZSTD_createCStream();
  #ifdef HAVE_QATZSTD
    void *sequenceProducerState = nullptr;
//    dout(15) << "QAT intends to start" << dendl;
    if (qatzstd_enabled) {
      int QATSTATUS = QZSTD_startQatDevice();
//      dout(15) << "QZSTD status: " << QATSTATUS << dendl;
      sequenceProducerState = QZSTD_createSeqProdState();
    }
  #endif
//    dout(15) << "compressor zstd level: "<<cct->_conf->compressor_zstd_level  << dendl;
//    dout(15) << "compression src length: "<<src.length()  << dendl;
//    dout(15) << "stream:  "<< s << dendl;
    ZSTD_initCStream_srcSize(s, cct->_conf->compressor_zstd_level, src.length()); //deprecated
    auto p = src.begin();
    size_t left = src.length();
//    dout(15) << "left: "<< left  << dendl;
    size_t const out_max = ZSTD_compressBound(left);  //max compressed output length in the worst occasion
//    dout(15) << "out_max: "<< out_max  << dendl;
    ceph::buffer::ptr outptr = ceph::buffer::create_small_page_aligned(out_max);
    ZSTD_outBuffer_s outbuf;
    outbuf.dst = outptr.c_str();
    outbuf.size = outptr.length();
    outbuf.pos = 0;
    
    //register qatSequenceProducer
  #ifdef HAVE_QATZSTD  
    if (qatzstd_enabled) {
//      dout(15) << "begin to registerseqprod "<< dendl;
      ZSTD_registerSequenceProducer(
        s,
        sequenceProducerState,
        qatSequenceProducer
      );

      size_t res = ZSTD_CCtx_setParameter(s, ZSTD_c_enableSeqProducerFallback, 1);
//      dout(15) << "res status: "<< res << dendl;
      if ((int)res <= 0) {
        printf("Failed to set fallback\n");
      return -1;
      }
    }
  #endif

    while (left) {
      ceph_assert(!p.end());
      struct ZSTD_inBuffer_s inbuf;
      inbuf.pos = 0;
      inbuf.size = p.get_ptr_and_advance(left, (const char**)&inbuf.src);
      left -= inbuf.size;
      ZSTD_EndDirective const zed = (left==0) ? ZSTD_e_end : ZSTD_e_continue;
      size_t r = ZSTD_compressStream2(s, &outbuf, &inbuf, zed);
//      dout(15) << "source size: "<< left << dendl;
//      dout(15) << "compressed size: "<< r << dendl;
      if (ZSTD_isError(r)) {
	return -EINVAL;
      }
    }
    ceph_assert(p.end());

    ZSTD_freeCStream(s);

    // free resources and shutdown QAT device
  #ifdef HAVE_QATZSTD  
    if (qatzstd_enabled) {
      QZSTD_freeSeqProdState(sequenceProducerState);
      QZSTD_stopQatDevice();
    }
  #endif

    // prefix with decompressed length
    ceph::encode((uint32_t)src.length(), dst);
    dst.append(outptr, 0, outbuf.pos);
    return 0;
  }

  int decompress(const ceph::buffer::list &src, ceph::buffer::list &dst, std::optional<int32_t> compressor_message) override {
    auto i = std::cbegin(src);
    return decompress(i, src.length(), dst, compressor_message);
  }

  int decompress(ceph::buffer::list::const_iterator &p,
		 size_t compressed_len,
		 ceph::buffer::list &dst,
		 std::optional<int32_t> compressor_message) override {
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
 private:
  CephContext *const cct;
};

#endif
