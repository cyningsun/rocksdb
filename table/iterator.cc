//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "rocksdb/util/dbug.h"
#include "rocksdb/iterator.h"

#include "memory/arena.h"
#include "table/internal_iterator.h"
#include "table/iterator_wrapper.h"

namespace ROCKSDB_NAMESPACE {

Status Iterator::GetProperty(std::string prop_name, std::string* prop) {
  DBUG_TRACE;
  if (prop == nullptr) {
    return Status::InvalidArgument("prop is nullptr");
  }
  if (prop_name == "rocksdb.iterator.is-key-pinned") {
    *prop = "0";
    return Status::OK();
  }
  if (prop_name == "rocksdb.iterator.is-value-pinned") {
    *prop = "0";
    return Status::OK();
  }
  return Status::InvalidArgument("Unidentified property.");
}

namespace {
class EmptyIterator : public Iterator {
 public:
  explicit EmptyIterator(const Status& s) : status_(s) {}
  bool Valid() const override { DBUG_TRACE; return false; }
  void Seek(const Slice& /*target*/) override {DBUG_TRACE;}
  void SeekForPrev(const Slice& /*target*/) override {DBUG_TRACE;}
  void SeekToFirst() override {DBUG_TRACE;}
  void SeekToLast() override {DBUG_TRACE;}
  void Next() override { DBUG_TRACE; assert(false); }
  void Prev() override { DBUG_TRACE; assert(false); }
  Slice key() const override {
    DBUG_TRACE;
    assert(false);
    return Slice();
  }
  Slice value() const override {
    DBUG_TRACE;
    assert(false);
    return Slice();
  }
  Status status() const override { DBUG_TRACE; return status_; }

 private:
  Status status_;
};

template <class TValue = Slice>
class EmptyInternalIterator : public InternalIteratorBase<TValue> {
 public:
  explicit EmptyInternalIterator(const Status& s) : status_(s) {}
  bool Valid() const override { DBUG_TRACE; return false; }
  void Seek(const Slice& /*target*/) override {DBUG_TRACE;}
  void SeekForPrev(const Slice& /*target*/) override {DBUG_TRACE;}
  void SeekToFirst() override {DBUG_TRACE;}
  void SeekToLast() override {DBUG_TRACE;}
  void Next() override { DBUG_TRACE; assert(false); }
  void Prev() override { DBUG_TRACE; assert(false); }
  Slice key() const override {
    DBUG_TRACE;
    assert(false);
    return Slice();
  }
  TValue value() const override {
    DBUG_TRACE;
    assert(false);
    return TValue();
  }
  Status status() const override { DBUG_TRACE; return status_; }

 private:
  Status status_;
};
}  // namespace

Iterator* NewEmptyIterator() { DBUG_TRACE; return new EmptyIterator(Status::OK()); }

Iterator* NewErrorIterator(const Status& status) {
  DBUG_TRACE;
  return new EmptyIterator(status);
}

template <class TValue>
InternalIteratorBase<TValue>* NewErrorInternalIterator(const Status& status) {
  return new EmptyInternalIterator<TValue>(status);
}
template InternalIteratorBase<IndexValue>* NewErrorInternalIterator(
    const Status& status);
template InternalIteratorBase<Slice>* NewErrorInternalIterator(
    const Status& status);

template <class TValue>
InternalIteratorBase<TValue>* NewErrorInternalIterator(const Status& status,
                                                       Arena* arena) {
  if (arena == nullptr) {
    return NewErrorInternalIterator<TValue>(status);
  } else {
    auto mem = arena->AllocateAligned(sizeof(EmptyInternalIterator<TValue>));
    return new (mem) EmptyInternalIterator<TValue>(status);
  }
}
template InternalIteratorBase<IndexValue>* NewErrorInternalIterator(
    const Status& status, Arena* arena);
template InternalIteratorBase<Slice>* NewErrorInternalIterator(
    const Status& status, Arena* arena);

template <class TValue>
InternalIteratorBase<TValue>* NewEmptyInternalIterator() {
  return new EmptyInternalIterator<TValue>(Status::OK());
}
template InternalIteratorBase<IndexValue>* NewEmptyInternalIterator();
template InternalIteratorBase<Slice>* NewEmptyInternalIterator();

template <class TValue>
InternalIteratorBase<TValue>* NewEmptyInternalIterator(Arena* arena) {
  if (arena == nullptr) {
    return NewEmptyInternalIterator<TValue>();
  } else {
    auto mem = arena->AllocateAligned(sizeof(EmptyInternalIterator<TValue>));
    return new (mem) EmptyInternalIterator<TValue>(Status::OK());
  }
}
template InternalIteratorBase<IndexValue>* NewEmptyInternalIterator(
    Arena* arena);
template InternalIteratorBase<Slice>* NewEmptyInternalIterator(Arena* arena);

}  // namespace ROCKSDB_NAMESPACE