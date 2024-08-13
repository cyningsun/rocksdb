//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "rocksdb/util/dbug.h"
#include "table/two_level_iterator.h"

#include "db/pinned_iterators_manager.h"
#include "memory/arena.h"
#include "rocksdb/options.h"
#include "rocksdb/table.h"
#include "table/block_based/block.h"
#include "table/format.h"

namespace ROCKSDB_NAMESPACE {

namespace {

class TwoLevelIndexIterator : public InternalIteratorBase<IndexValue> {
 public:
  explicit TwoLevelIndexIterator(
      TwoLevelIteratorState* state,
      InternalIteratorBase<IndexValue>* first_level_iter);

  ~TwoLevelIndexIterator() override {
    first_level_iter_.DeleteIter(false /* is_arena_mode */);
    second_level_iter_.DeleteIter(false /* is_arena_mode */);
    delete state_;
  }

  void Seek(const Slice& target) override;
  void SeekForPrev(const Slice& target) override;
  void SeekToFirst() override;
  void SeekToLast() override;
  void Next() override;
  void Prev() override;

  bool Valid() const override { DBUG_TRACE; return second_level_iter_.Valid(); }
  Slice key() const override {
    DBUG_TRACE;
    assert(Valid());
    return second_level_iter_.key();
  }
  Slice user_key() const override {
    DBUG_TRACE;
    assert(Valid());
    return second_level_iter_.user_key();
  }
  IndexValue value() const override {
    DBUG_TRACE;
    assert(Valid());
    return second_level_iter_.value();
  }
  Status status() const override {
    DBUG_TRACE;
    if (!first_level_iter_.status().ok()) {
      assert(second_level_iter_.iter() == nullptr);
      return first_level_iter_.status();
    } else if (second_level_iter_.iter() != nullptr &&
               !second_level_iter_.status().ok()) {
      return second_level_iter_.status();
    } else {
      return status_;
    }
  }
  void SetPinnedItersMgr(
      PinnedIteratorsManager* /*pinned_iters_mgr*/) override {DBUG_TRACE;}
  bool IsKeyPinned() const override { DBUG_TRACE; return false; }
  bool IsValuePinned() const override { DBUG_TRACE; return false; }

 private:
  void SaveError(const Status& s) {
    DBUG_TRACE;
    if (status_.ok() && !s.ok()) {
      status_ = s;
    }
  }
  void SkipEmptyDataBlocksForward();
  void SkipEmptyDataBlocksBackward();
  void SetSecondLevelIterator(InternalIteratorBase<IndexValue>* iter);
  void InitDataBlock();

  TwoLevelIteratorState* state_;
  IteratorWrapperBase<IndexValue> first_level_iter_;
  IteratorWrapperBase<IndexValue> second_level_iter_;  // May be nullptr
  Status status_;
  // If second_level_iter is non-nullptr, then "data_block_handle_" holds the
  // "index_value" passed to block_function_ to create the second_level_iter.
  BlockHandle data_block_handle_;
};

TwoLevelIndexIterator::TwoLevelIndexIterator(
    TwoLevelIteratorState* state,
    InternalIteratorBase<IndexValue>* first_level_iter)
    : state_(state), first_level_iter_(first_level_iter) {}

void TwoLevelIndexIterator::Seek(const Slice& target) {
  DBUG_TRACE;
  first_level_iter_.Seek(target);

  InitDataBlock();
  if (second_level_iter_.iter() != nullptr) {
    second_level_iter_.Seek(target);
  }
  SkipEmptyDataBlocksForward();
}

void TwoLevelIndexIterator::SeekForPrev(const Slice& target) {
  DBUG_TRACE;
  first_level_iter_.Seek(target);
  InitDataBlock();
  if (second_level_iter_.iter() != nullptr) {
    second_level_iter_.SeekForPrev(target);
  }
  if (!Valid()) {
    if (!first_level_iter_.Valid() && first_level_iter_.status().ok()) {
      first_level_iter_.SeekToLast();
      InitDataBlock();
      if (second_level_iter_.iter() != nullptr) {
        second_level_iter_.SeekForPrev(target);
      }
    }
    SkipEmptyDataBlocksBackward();
  }
}

void TwoLevelIndexIterator::SeekToFirst() {
  DBUG_TRACE;
  first_level_iter_.SeekToFirst();
  InitDataBlock();
  if (second_level_iter_.iter() != nullptr) {
    second_level_iter_.SeekToFirst();
  }
  SkipEmptyDataBlocksForward();
}

void TwoLevelIndexIterator::SeekToLast() {
  DBUG_TRACE;
  first_level_iter_.SeekToLast();
  InitDataBlock();
  if (second_level_iter_.iter() != nullptr) {
    second_level_iter_.SeekToLast();
  }
  SkipEmptyDataBlocksBackward();
}

void TwoLevelIndexIterator::Next() {
  DBUG_TRACE;
  assert(Valid());
  second_level_iter_.Next();
  SkipEmptyDataBlocksForward();
}

void TwoLevelIndexIterator::Prev() {
  DBUG_TRACE;
  assert(Valid());
  second_level_iter_.Prev();
  SkipEmptyDataBlocksBackward();
}

void TwoLevelIndexIterator::SkipEmptyDataBlocksForward() {
  DBUG_TRACE;
  while (second_level_iter_.iter() == nullptr ||
         (!second_level_iter_.Valid() && second_level_iter_.status().ok())) {
    // Move to next block
    if (!first_level_iter_.Valid()) {
      SetSecondLevelIterator(nullptr);
      return;
    }
    first_level_iter_.Next();
    InitDataBlock();
    if (second_level_iter_.iter() != nullptr) {
      second_level_iter_.SeekToFirst();
    }
  }
}

void TwoLevelIndexIterator::SkipEmptyDataBlocksBackward() {
  DBUG_TRACE;
  while (second_level_iter_.iter() == nullptr ||
         (!second_level_iter_.Valid() && second_level_iter_.status().ok())) {
    // Move to next block
    if (!first_level_iter_.Valid()) {
      SetSecondLevelIterator(nullptr);
      return;
    }
    first_level_iter_.Prev();
    InitDataBlock();
    if (second_level_iter_.iter() != nullptr) {
      second_level_iter_.SeekToLast();
    }
  }
}

void TwoLevelIndexIterator::SetSecondLevelIterator(
    InternalIteratorBase<IndexValue>* iter) {
  DBUG_TRACE;
  InternalIteratorBase<IndexValue>* old_iter = second_level_iter_.Set(iter);
  delete old_iter;
}

void TwoLevelIndexIterator::InitDataBlock() {
  DBUG_TRACE;
  if (!first_level_iter_.Valid()) {
    SetSecondLevelIterator(nullptr);
  } else {
    BlockHandle handle = first_level_iter_.value().handle;
    if (second_level_iter_.iter() != nullptr &&
        !second_level_iter_.status().IsIncomplete() &&
        handle.offset() == data_block_handle_.offset()) {
      // second_level_iter is already constructed with this iterator, so
      // no need to change anything
    } else {
      InternalIteratorBase<IndexValue>* iter =
          state_->NewSecondaryIterator(handle);
      data_block_handle_ = handle;
      SetSecondLevelIterator(iter);
      if (iter == nullptr) {
        status_ = Status::Corruption("Missing block for partition " +
                                     handle.ToString());
      }
    }
  }
}

}  // namespace

InternalIteratorBase<IndexValue>* NewTwoLevelIterator(
    TwoLevelIteratorState* state,
    InternalIteratorBase<IndexValue>* first_level_iter) {
  DBUG_TRACE;
  return new TwoLevelIndexIterator(state, first_level_iter);
}
}  // namespace ROCKSDB_NAMESPACE