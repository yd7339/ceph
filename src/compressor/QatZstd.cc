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

//#include "common/ceph_context.h"
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

QatZstd::QatZstd() {}

QatZstd::~QatZstd() {
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

int QatZstd::compress(const ceph::buffer::list &src, ceph::buffer::list &dst, std::optional<int32_t> &compressor_message, CephContext *cct){
    ZSTD_CStream *s = ZSTD_createCStream();
    void *sequenceProducerState = nullptr;
    dout(15) << "QAT intends to start" << dendl;
    int QATSTATUS=QZSTD_startQatDevice();
    dout(15) << "QZSTD status: " << QATSTATUS << dendl;
    sequenceProducerState = QZSTD_createSeqProdState();
  
    ZSTD_initCStream_srcSize(s, cct->_conf->compressor_zstd_level, src.length());

    // size_t status = ZSTD_CCtx_reset(s, ZSTD_reset_session_only);
    // dout(15) << "zstd reset status: "<< status << dendl;
    // status = ZSTD_CCtx_refCDict(s, NULL); // clear the dictionary (if any)
    // dout(15) << "zstd refCDict status: "<< status << dendl;
    // status = ZSTD_CCtx_setParameter(s, ZSTD_c_compressionLevel, cct->_conf->compressor_zstd_level);
    // dout(15) << "zstd set compression level status: "<< status << dendl;
    // status = ZSTD_CCtx_setPledgedSrcSize(s, src.length());
    // dout(15) << "zstd set PledgedSrcSize status: "<< status << dendl;

    auto p = src.begin();
    size_t left = src.length();

    size_t const out_max = ZSTD_compressBound(left);
    ceph::buffer::ptr outptr = ceph::buffer::create_small_page_aligned(out_max);
    ZSTD_outBuffer_s outbuf;
    outbuf.dst = outptr.c_str();
    outbuf.size = outptr.length();
    outbuf.pos = 0;
    
    //register qatSequenceProducer
    dout(15) << "begin to registerseqprod "<< dendl;
    ZSTD_registerSequenceProducer(
      s,
      sequenceProducerState,
      qatSequenceProducer
    );
    dout(15) << "s= "<< s << dendl;

//      dout(15) << "CCtx ZSTD_c_enableSeqProducerFallback: "<< s->appliedParams->ZSTD_c_enableSeqProducerFallback << dendl;
    size_t res = ZSTD_CCtx_setParameter(s, ZSTD_c_enableSeqProducerFallback, 1);
    dout(15) << "res status: "<< res << dendl;
    if ((int)res <= 0) {
      dout(15) << "Failed to set fallback" << dendl;
      printf("Failed to set fallback\n");
      return -1;
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
	      return -EINVAL;
      }
    }
    ceph_assert(p.end());
    
    ZSTD_freeCStream(s);
    QZSTD_freeSeqProdState(sequenceProducerState);
    dout(15) << "freeseq" <<dendl;
    // prefix with decompressed length
    ceph::encode((uint32_t)src.length(), dst);
    dst.append(outptr, 0, outbuf.pos);
    return 0;
}