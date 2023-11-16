/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2023 Intel Corporation
 *
 * Author: Ding Yang <ding.yang@intel.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */
#include <qatseqprod.h>

#define ZSTD_STATIC_LINKING_ONLY
//#include "zstd/lib/zstd.h"
//#include "zstd/lib/zstd_errors.h"

#include <zstd.h>
#include <zstd_errors.h>
#include "common/common_init.h"
#include "common/debug.h"
#include "common/dout.h"
#include "common/errno.h"
#include "QatZstd.h"

// -----------------------------------------------------------------------------
#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_compressor
#undef dout_prefix
#define dout_prefix _prefix(_dout)

static std::ostream& _prefix(std::ostream* _dout)
{
  return *_dout << "QatZstdCompressor: ";
}

QatZstd::QatZstd() {
    int QATSTATUS = QZSTD_startQatDevice();
    void *sequenceProducerState = QZSTD_createSeqProdState();
}

QatZstd::~QatZstd() {
    QZSTD_freeSeqProdState(sequenceProducerState);
    QZSTD_stopQatDevice();
}

bool QatZstd::init(const std::string &alg) {
  std::scoped_lock lock(mutex);
  if (!alg_name.empty()) {
    return true;
  }

  dout(15) << "First use for QAT compressor" << dendl;
  if (alg != "zstd") {
    return false;
  }

  alg_name = alg;
  return true;
}

int QatZstd::compress(const ceph::buffer::list &src, ceph::buffer::list &dst, std::optional<int32_t> &compressor_message){
    ZSTD_CStream *s = ZSTD_createCStream();
//    void *sequenceProducerState = nullptr;
//    int QATSTATUS=QZSTD_startQatDevice();
//    sequenceProducerState = QZSTD_createSeqProdState();
  
//    ZSTD_initCStream_srcSize(s, cct->_conf->compressor_zstd_level, src.length());

    size_t status = ZSTD_CCtx_reset(s, ZSTD_reset_session_only);
    status = ZSTD_CCtx_refCDict(s, NULL); // clear the dictionary (if any)
    status = ZSTD_CCtx_setParameter(s, ZSTD_c_compressionLevel, g_ceph_context->_conf->compressor_zstd_level);
    dout(15) << "zstd compression level" << g_ceph_context->_conf->compressor_zstd_level << dendl;
    status = ZSTD_CCtx_setPledgedSrcSize(s, src.length());

    auto p = src.begin();
    size_t left = src.length();

    size_t const out_max = ZSTD_compressBound(left);
    ceph::buffer::ptr outptr = ceph::buffer::create_small_page_aligned(out_max);
    ZSTD_outBuffer_s outbuf;
    outbuf.dst = outptr.c_str();
    outbuf.size = outptr.length();
    outbuf.pos = 0;
    
    //register qatSequenceProducer
    ZSTD_registerSequenceProducer(
      s,
      sequenceProducerState,
      qatSequenceProducer
    );

    size_t res = ZSTD_CCtx_setParameter(s, ZSTD_c_enableSeqProducerFallback, 1);
    if ((int)res <= 0) {
      dout(15) << "Failed to set fallback" << dendl;
      return -EINVAL;
    }

    while (left) {
      ceph_assert(!p.end());
      struct ZSTD_inBuffer_s inbuf;
      inbuf.pos = 0;
      inbuf.size = p.get_ptr_and_advance(left, (const char**)&inbuf.src);
      left -= inbuf.size;
      ZSTD_EndDirective const zed = (left==0) ? ZSTD_e_end : ZSTD_e_continue;
      size_t r = ZSTD_compressStream2(s, &outbuf, &inbuf, zed);
      if (ZSTD_isError(r)) {
        dout(15) << "zstd compress error" << dendl;
	      return -EINVAL;
      }
    }
    ceph_assert(p.end());
    
    ZSTD_freeCStream(s);
//    QZSTD_freeSeqProdState(sequenceProducerState);
    // prefix with decompressed length
    ceph::encode((uint32_t)src.length(), dst);
    dst.append(outptr, 0, outbuf.pos);
    return 0;
}