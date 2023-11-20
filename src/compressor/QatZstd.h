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

#ifndef CEPH_QATZSTD_H
#define CEPH_QATZSTD_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "include/buffer.h"
//#include "common/ceph_context.h"

extern "C" struct QZSTD_Session_T;
struct QZSTDSessionDeleter {
  void operator() (struct QZSTD_Session_T *session);
};

class QatZstd {

 public:
  using session_ptr = std::unique_ptr<struct QZSTD_Session_T, QZSTDSessionDeleter>;
  QatZstd();
  ~QatZstd();

  bool init(const std::string &alg);
  int compress(const ceph::buffer::list &src, ceph::buffer::list &dst, std::optional<int32_t> &compressor_message);
  //int compress(const bufferlist &in, bufferlist &out, std::optional<int32_t> &compressor_message);

 private:
  session_ptr get_session();
  friend struct cached_session_t;
  std::vector<session_ptr> sessions;
  std::mutex mutex;
  std::string alg_name;
  //void *sequenceProducerState;
};

#endif
