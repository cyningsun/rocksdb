//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "rocksdb/util/dbug.h"
#ifndef NDEBUG

#include "db/column_family.h"
#include "db/db_impl/db_impl.h"
#include "db/error_handler.h"
#include "db/periodic_task_scheduler.h"
#include "monitoring/thread_status_updater.h"
#include "util/cast_util.h"

namespace ROCKSDB_NAMESPACE {
uint64_t DBImpl::TEST_GetLevel0TotalSize() {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);
  return default_cf_handle_->cfd()->current()->storage_info()->NumLevelBytes(0);
}

Status DBImpl::TEST_SwitchWAL() {
  DBUG_TRACE;
  WriteContext write_context;
  InstrumentedMutexLock l(&mutex_);
  void* writer = TEST_BeginWrite();
  auto s = SwitchWAL(&write_context);
  TEST_EndWrite(writer);
  return s;
}

uint64_t DBImpl::TEST_MaxNextLevelOverlappingBytes(
    ColumnFamilyHandle* column_family) {
  DBUG_TRACE;
  ColumnFamilyData* cfd;
  if (column_family == nullptr) {
    cfd = default_cf_handle_->cfd();
  } else {
    auto cfh = static_cast_with_check<ColumnFamilyHandleImpl>(column_family);
    cfd = cfh->cfd();
  }
  InstrumentedMutexLock l(&mutex_);
  return cfd->current()->storage_info()->MaxNextLevelOverlappingBytes();
}

void DBImpl::TEST_GetFilesMetaData(
    ColumnFamilyHandle* column_family,
    std::vector<std::vector<FileMetaData>>* metadata,
    std::vector<std::shared_ptr<BlobFileMetaData>>* blob_metadata) {
  DBUG_TRACE;
  assert(metadata);

  auto cfh = static_cast_with_check<ColumnFamilyHandleImpl>(column_family);
  assert(cfh);

  auto cfd = cfh->cfd();
  assert(cfd);

  InstrumentedMutexLock l(&mutex_);

  const auto* current = cfd->current();
  assert(current);

  const auto* vstorage = current->storage_info();
  assert(vstorage);

  metadata->resize(NumberLevels());

  for (int level = 0; level < NumberLevels(); ++level) {
    const std::vector<FileMetaData*>& files = vstorage->LevelFiles(level);

    (*metadata)[level].clear();
    (*metadata)[level].reserve(files.size());

    for (const auto& f : files) {
      (*metadata)[level].push_back(*f);
    }
  }

  if (blob_metadata) {
    *blob_metadata = vstorage->GetBlobFiles();
  }
}

uint64_t DBImpl::TEST_Current_Manifest_FileNo() {
  DBUG_TRACE;
  return versions_->manifest_file_number();
}

uint64_t DBImpl::TEST_Current_Next_FileNo() {
  DBUG_TRACE;
  return versions_->current_next_file_number();
}

Status DBImpl::TEST_CompactRange(int level, const Slice* begin,
                                 const Slice* end,
                                 ColumnFamilyHandle* column_family,
                                 bool disallow_trivial_move) {
  DBUG_TRACE;
  ColumnFamilyData* cfd;
  if (column_family == nullptr) {
    cfd = default_cf_handle_->cfd();
  } else {
    auto cfh = static_cast_with_check<ColumnFamilyHandleImpl>(column_family);
    cfd = cfh->cfd();
  }
  int output_level =
      (cfd->ioptions()->compaction_style == kCompactionStyleUniversal ||
       cfd->ioptions()->compaction_style == kCompactionStyleFIFO)
          ? level
          : level + 1;
  return RunManualCompaction(
      cfd, level, output_level, CompactRangeOptions(), begin, end, true,
      disallow_trivial_move,
      std::numeric_limits<uint64_t>::max() /*max_file_num_to_ignore*/,
      "" /*trim_ts*/);
}

Status DBImpl::TEST_SwitchMemtable(ColumnFamilyData* cfd) {
  DBUG_TRACE;
  WriteContext write_context;
  InstrumentedMutexLock l(&mutex_);
  if (cfd == nullptr) {
    cfd = default_cf_handle_->cfd();
  }

  Status s;
  void* writer = TEST_BeginWrite();
  if (two_write_queues_) {
    WriteThread::Writer nonmem_w;
    nonmem_write_thread_.EnterUnbatched(&nonmem_w, &mutex_);
    s = SwitchMemtable(cfd, &write_context);
    nonmem_write_thread_.ExitUnbatched(&nonmem_w);
  } else {
    s = SwitchMemtable(cfd, &write_context);
  }
  TEST_EndWrite(writer);
  return s;
}

Status DBImpl::TEST_FlushMemTable(bool wait, bool allow_write_stall,
                                  ColumnFamilyHandle* cfh) {
  DBUG_TRACE;
  FlushOptions fo;
  fo.wait = wait;
  fo.allow_write_stall = allow_write_stall;
  ColumnFamilyData* cfd;
  if (cfh == nullptr) {
    cfd = default_cf_handle_->cfd();
  } else {
    auto cfhi = static_cast_with_check<ColumnFamilyHandleImpl>(cfh);
    cfd = cfhi->cfd();
  }
  return FlushMemTable(cfd, fo, FlushReason::kTest);
}

Status DBImpl::TEST_FlushMemTable(ColumnFamilyData* cfd,
                                  const FlushOptions& flush_opts) {
  DBUG_TRACE;
  return FlushMemTable(cfd, flush_opts, FlushReason::kTest);
}

Status DBImpl::TEST_AtomicFlushMemTables(
    const autovector<ColumnFamilyData*>& provided_candidate_cfds,
    const FlushOptions& flush_opts) {
  DBUG_TRACE;
  return AtomicFlushMemTables(flush_opts, FlushReason::kTest,
                              provided_candidate_cfds);
}

Status DBImpl::TEST_WaitForBackgroundWork() {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);
  WaitForBackgroundWork();
  return error_handler_.GetBGError();
}

Status DBImpl::TEST_WaitForFlushMemTable(ColumnFamilyHandle* column_family) {
  DBUG_TRACE;
  ColumnFamilyData* cfd;
  if (column_family == nullptr) {
    cfd = default_cf_handle_->cfd();
  } else {
    auto cfh = static_cast_with_check<ColumnFamilyHandleImpl>(column_family);
    cfd = cfh->cfd();
  }
  return WaitForFlushMemTable(cfd, nullptr, false);
}

Status DBImpl::TEST_WaitForCompact() {
  DBUG_TRACE;
  return WaitForCompact(WaitForCompactOptions());
}
Status DBImpl::TEST_WaitForCompact(
    const WaitForCompactOptions& wait_for_compact_options) {
  DBUG_TRACE;
  return WaitForCompact(wait_for_compact_options);
}

Status DBImpl::TEST_WaitForPurge() {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);
  while (bg_purge_scheduled_ && error_handler_.GetBGError().ok()) {
    bg_cv_.Wait();
  }
  return error_handler_.GetBGError();
}

Status DBImpl::TEST_GetBGError() {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);
  return error_handler_.GetBGError();
}

void DBImpl::TEST_LockMutex() { DBUG_TRACE; mutex_.Lock(); }

void DBImpl::TEST_UnlockMutex() { DBUG_TRACE; mutex_.Unlock(); }

void DBImpl::TEST_SignalAllBgCv() { DBUG_TRACE; bg_cv_.SignalAll(); }

void* DBImpl::TEST_BeginWrite() {
  DBUG_TRACE;
  auto w = new WriteThread::Writer();
  write_thread_.EnterUnbatched(w, &mutex_);
  return static_cast<void*>(w);
}

void DBImpl::TEST_EndWrite(void* w) {
  DBUG_TRACE;
  auto writer = static_cast<WriteThread::Writer*>(w);
  write_thread_.ExitUnbatched(writer);
  delete writer;
}

size_t DBImpl::TEST_LogsToFreeSize() {
  DBUG_TRACE;
  InstrumentedMutexLock l(&log_write_mutex_);
  return logs_to_free_.size();
}

uint64_t DBImpl::TEST_LogfileNumber() {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);
  return logfile_number_;
}

Status DBImpl::TEST_GetAllImmutableCFOptions(
    std::unordered_map<std::string, const ImmutableCFOptions*>* iopts_map) {
  DBUG_TRACE;
  std::vector<std::string> cf_names;
  std::vector<const ImmutableCFOptions*> iopts;
  {
    InstrumentedMutexLock l(&mutex_);
    for (auto cfd : *versions_->GetColumnFamilySet()) {
      cf_names.push_back(cfd->GetName());
      iopts.push_back(cfd->ioptions());
    }
  }
  iopts_map->clear();
  for (size_t i = 0; i < cf_names.size(); ++i) {
    iopts_map->insert({cf_names[i], iopts[i]});
  }

  return Status::OK();
}

uint64_t DBImpl::TEST_FindMinLogContainingOutstandingPrep() {
  DBUG_TRACE;
  return logs_with_prep_tracker_.FindMinLogContainingOutstandingPrep();
}

size_t DBImpl::TEST_PreparedSectionCompletedSize() {
  DBUG_TRACE;
  return logs_with_prep_tracker_.TEST_PreparedSectionCompletedSize();
}

size_t DBImpl::TEST_LogsWithPrepSize() {
  DBUG_TRACE;
  return logs_with_prep_tracker_.TEST_LogsWithPrepSize();
}

uint64_t DBImpl::TEST_FindMinPrepLogReferencedByMemTable() {
  DBUG_TRACE;
  autovector<MemTable*> empty_list;
  return FindMinPrepLogReferencedByMemTable(versions_.get(), empty_list);
}

Status DBImpl::TEST_GetLatestMutableCFOptions(
    ColumnFamilyHandle* column_family, MutableCFOptions* mutable_cf_options) {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);

  auto cfh = static_cast_with_check<ColumnFamilyHandleImpl>(column_family);
  *mutable_cf_options = *cfh->cfd()->GetLatestMutableCFOptions();
  return Status::OK();
}

int DBImpl::TEST_BGCompactionsAllowed() const {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);
  return GetBGJobLimits().max_compactions;
}

int DBImpl::TEST_BGFlushesAllowed() const {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);
  return GetBGJobLimits().max_flushes;
}

SequenceNumber DBImpl::TEST_GetLastVisibleSequence() const {
  DBUG_TRACE;
  if (last_seq_same_as_publish_seq_) {
    return versions_->LastSequence();
  } else {
    return versions_->LastAllocatedSequence();
  }
}

size_t DBImpl::TEST_GetWalPreallocateBlockSize(
    uint64_t write_buffer_size) const {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);
  return GetWalPreallocateBlockSize(write_buffer_size);
}

void DBImpl::TEST_WaitForPeriodicTaskRun(std::function<void()> callback) const {
  DBUG_TRACE;
  periodic_task_scheduler_.TEST_WaitForRun(callback);
}

const PeriodicTaskScheduler& DBImpl::TEST_GetPeriodicTaskScheduler() const {
  DBUG_TRACE;
  return periodic_task_scheduler_;
}

SeqnoToTimeMapping DBImpl::TEST_GetSeqnoToTimeMapping() const {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);
  return seqno_to_time_mapping_;
}

const autovector<uint64_t>& DBImpl::TEST_GetFilesToQuarantine() const {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);
  return error_handler_.GetFilesToQuarantine();
}

void DBImpl::TEST_DeleteObsoleteFiles() {
  DBUG_TRACE;
  InstrumentedMutexLock l(&mutex_);
  DeleteObsoleteFiles();
}

size_t DBImpl::TEST_EstimateInMemoryStatsHistorySize() const {
  DBUG_TRACE;
  InstrumentedMutexLock l(&const_cast<DBImpl*>(this)->stats_history_mutex_);
  return EstimateInMemoryStatsHistorySize();
}
}  // namespace ROCKSDB_NAMESPACE
#endif  // NDEBUG