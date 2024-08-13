//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//

#include "rocksdb/util/dbug.h"
#include <cassert>

#include "monitoring/perf_level_imp.h"

namespace ROCKSDB_NAMESPACE {

thread_local PerfLevel perf_level = kEnableCount;

void SetPerfLevel(PerfLevel level) {
  DBUG_TRACE;
  assert(level > kUninitialized);
  assert(level < kOutOfBounds);
  perf_level = level;
}

PerfLevel GetPerfLevel() { DBUG_TRACE; return perf_level; }

}  // namespace ROCKSDB_NAMESPACE