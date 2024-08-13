//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include "rocksdb/util/dbug.h"
#include "table/block_based/full_filter_block.h"

#include <array>

#include "block_type.h"
#include "monitoring/perf_context_imp.h"
#include "port/malloc.h"
#include "port/port.h"
#include "rocksdb/filter_policy.h"
#include "table/block_based/block_based_table_reader.h"
#include "util/coding.h"

namespace ROCKSDB_NAMESPACE {

FullFilterBlockBuilder::FullFilterBlockBuilder(
    const SliceTransform* _prefix_extractor, bool whole_key_filtering,
    FilterBitsBuilder* filter_bits_builder)
    : need_last_prefix_(whole_key_filtering && _prefix_extractor != nullptr),
      prefix_extractor_(_prefix_extractor),
      whole_key_filtering_(whole_key_filtering),
      last_whole_key_recorded_(false),
      last_prefix_recorded_(false),
      last_key_in_domain_(false),
      any_added_(false) {
  assert(filter_bits_builder != nullptr);
  filter_bits_builder_.reset(filter_bits_builder);
}

size_t FullFilterBlockBuilder::EstimateEntriesAdded() {
  DBUG_TRACE;
  return filter_bits_builder_->EstimateEntriesAdded();
}

void FullFilterBlockBuilder::Add(const Slice& key_without_ts) {
  DBUG_TRACE;
  const bool add_prefix =
      prefix_extractor_ && prefix_extractor_->InDomain(key_without_ts);

  if (need_last_prefix_ && !last_prefix_recorded_ && last_key_in_domain_) {
    // We can reach here when a new filter partition starts in partitioned
    // filter. The last prefix in the previous partition should be added if
    // necessary regardless of key_without_ts, to support prefix SeekForPrev.
    AddKey(last_prefix_str_);
    last_prefix_recorded_ = true;
  }

  if (whole_key_filtering_) {
    if (!add_prefix) {
      AddKey(key_without_ts);
    } else {
      // if both whole_key and prefix are added to bloom then we will have whole
      // key_without_ts and prefix addition being interleaved and thus cannot
      // rely on the bits builder to properly detect the duplicates by comparing
      // with the last item.
      Slice last_whole_key = Slice(last_whole_key_str_);
      if (!last_whole_key_recorded_ ||
          last_whole_key.compare(key_without_ts) != 0) {
        AddKey(key_without_ts);
        last_whole_key_recorded_ = true;
        last_whole_key_str_.assign(key_without_ts.data(),
                                   key_without_ts.size());
      }
    }
  }
  if (add_prefix) {
    last_key_in_domain_ = true;
    AddPrefix(key_without_ts);
  } else {
    last_key_in_domain_ = false;
  }
}

// Add key to filter if needed
inline void FullFilterBlockBuilder::AddKey(const Slice& key) {
  DBUG_TRACE;
  filter_bits_builder_->AddKey(key);
  any_added_ = true;
}

// Add prefix to filter if needed
void FullFilterBlockBuilder::AddPrefix(const Slice& key) {
  DBUG_TRACE;
  assert(prefix_extractor_ && prefix_extractor_->InDomain(key));
  Slice prefix = prefix_extractor_->Transform(key);
  if (need_last_prefix_) {
    // WART/FIXME: Because last_prefix_str_ is needed above to make
    // SeekForPrev work with partitioned + prefix filters, we are currently
    // use this inefficient code in that case (in addition to prefix+whole
    // key). Hopefully this can be optimized with some refactoring up the call
    // chain to BlockBasedTableBuilder. Even in PartitionedFilterBlockBuilder,
    // we don't currently have access to the previous key/prefix by the time we
    // know we are starting a new partition.

    // if both whole_key and prefix are added to bloom then we will have whole
    // key and prefix addition being interleaved and thus cannot rely on the
    // bits builder to properly detect the duplicates by comparing with the last
    // item.
    Slice last_prefix = Slice(last_prefix_str_);
    if (!last_prefix_recorded_ || last_prefix.compare(prefix) != 0) {
      AddKey(prefix);
      last_prefix_recorded_ = true;
      last_prefix_str_.assign(prefix.data(), prefix.size());
    }
  } else {
    AddKey(prefix);
  }
}

void FullFilterBlockBuilder::Reset() {
  DBUG_TRACE;
  last_whole_key_recorded_ = false;
  last_prefix_recorded_ = false;
}

Status FullFilterBlockBuilder::Finish(
    const BlockHandle& /*last_partition_block_handle*/, Slice* filter,
    std::unique_ptr<const char[]>* filter_owner) {
  DBUG_TRACE;
  Reset();
  Status s = Status::OK();
  if (any_added_) {
    any_added_ = false;
    *filter = filter_bits_builder_->Finish(
        filter_owner ? filter_owner : &filter_data_, &s);
  } else {
    *filter = Slice{};
  }
  return s;
}

FullFilterBlockReader::FullFilterBlockReader(
    const BlockBasedTable* t,
    CachableEntry<ParsedFullFilterBlock>&& filter_block)
    : FilterBlockReaderCommon(t, std::move(filter_block)) {}

bool FullFilterBlockReader::KeyMayMatch(const Slice& key,
                                        const Slice* const /*const_ikey_ptr*/,
                                        GetContext* get_context,
                                        BlockCacheLookupContext* lookup_context,
                                        const ReadOptions& read_options) {
  DBUG_TRACE;
  if (!whole_key_filtering()) {
    return true;
  }
  return MayMatch(key, get_context, lookup_context, read_options);
}

std::unique_ptr<FilterBlockReader> FullFilterBlockReader::Create(
    const BlockBasedTable* table, const ReadOptions& ro,
    FilePrefetchBuffer* prefetch_buffer, bool use_cache, bool prefetch,
    bool pin, BlockCacheLookupContext* lookup_context) {
  DBUG_TRACE;
  assert(table);
  assert(table->get_rep());
  assert(!pin || prefetch);

  CachableEntry<ParsedFullFilterBlock> filter_block;
  if (prefetch || !use_cache) {
    const Status s = ReadFilterBlock(table, prefetch_buffer, ro, use_cache,
                                     nullptr /* get_context */, lookup_context,
                                     &filter_block);
    if (!s.ok()) {
      IGNORE_STATUS_IF_ERROR(s);
      return std::unique_ptr<FilterBlockReader>();
    }

    if (use_cache && !pin) {
      filter_block.Reset();
    }
  }

  return std::unique_ptr<FilterBlockReader>(
      new FullFilterBlockReader(table, std::move(filter_block)));
}

bool FullFilterBlockReader::PrefixMayMatch(
    const Slice& prefix, const Slice* const /*const_ikey_ptr*/,
    GetContext* get_context, BlockCacheLookupContext* lookup_context,
    const ReadOptions& read_options) {
  DBUG_TRACE;
  return MayMatch(prefix, get_context, lookup_context, read_options);
}

bool FullFilterBlockReader::MayMatch(const Slice& entry,
                                     GetContext* get_context,
                                     BlockCacheLookupContext* lookup_context,
                                     const ReadOptions& read_options) const {
  DBUG_TRACE;
  CachableEntry<ParsedFullFilterBlock> filter_block;

  const Status s = GetOrReadFilterBlock(get_context, lookup_context,
                                        &filter_block, read_options);
  if (!s.ok()) {
    IGNORE_STATUS_IF_ERROR(s);
    return true;
  }

  assert(filter_block.GetValue());

  FilterBitsReader* const filter_bits_reader =
      filter_block.GetValue()->filter_bits_reader();

  if (filter_bits_reader) {
    if (filter_bits_reader->MayMatch(entry)) {
      PERF_COUNTER_ADD(bloom_sst_hit_count, 1);
      return true;
    } else {
      PERF_COUNTER_ADD(bloom_sst_miss_count, 1);
      return false;
    }
  }
  return true;
}

void FullFilterBlockReader::KeysMayMatch(
    MultiGetRange* range, BlockCacheLookupContext* lookup_context,
    const ReadOptions& read_options) {
  DBUG_TRACE;
  if (!whole_key_filtering()) {
    // Simply return. Don't skip any key - consider all keys as likely to be
    // present
    return;
  }
  MayMatch(range, nullptr, lookup_context, read_options);
}

void FullFilterBlockReader::PrefixesMayMatch(
    MultiGetRange* range, const SliceTransform* prefix_extractor,
    BlockCacheLookupContext* lookup_context, const ReadOptions& read_options) {
  DBUG_TRACE;
  MayMatch(range, prefix_extractor, lookup_context, read_options);
}

void FullFilterBlockReader::MayMatch(MultiGetRange* range,
                                     const SliceTransform* prefix_extractor,
                                     BlockCacheLookupContext* lookup_context,
                                     const ReadOptions& read_options) const {
  DBUG_TRACE;
  CachableEntry<ParsedFullFilterBlock> filter_block;

  const Status s = GetOrReadFilterBlock(
      range->begin()->get_context, lookup_context, &filter_block, read_options);
  if (!s.ok()) {
    IGNORE_STATUS_IF_ERROR(s);
    return;
  }

  assert(filter_block.GetValue());

  FilterBitsReader* const filter_bits_reader =
      filter_block.GetValue()->filter_bits_reader();

  if (!filter_bits_reader) {
    return;
  }

  // We need to use an array instead of autovector for may_match since
  // &may_match[0] doesn't work for autovector<bool> (compiler error). So
  // declare both keys and may_match as arrays, which is also slightly less
  // expensive compared to autovector
  std::array<Slice*, MultiGetContext::MAX_BATCH_SIZE> keys;
  std::array<bool, MultiGetContext::MAX_BATCH_SIZE> may_match = {{true}};
  autovector<Slice, MultiGetContext::MAX_BATCH_SIZE> prefixes;
  int num_keys = 0;
  MultiGetRange filter_range(*range, range->begin(), range->end());
  for (auto iter = filter_range.begin(); iter != filter_range.end(); ++iter) {
    if (!prefix_extractor) {
      keys[num_keys++] = &iter->ukey_without_ts;
    } else if (prefix_extractor->InDomain(iter->ukey_without_ts)) {
      prefixes.emplace_back(prefix_extractor->Transform(iter->ukey_without_ts));
      keys[num_keys++] = &prefixes.back();
    } else {
      filter_range.SkipKey(iter);
    }
  }

  filter_bits_reader->MayMatch(num_keys, keys.data(), may_match.data());

  int i = 0;
  for (auto iter = filter_range.begin(); iter != filter_range.end(); ++iter) {
    if (!may_match[i]) {
      // Update original MultiGet range to skip this key. The filter_range
      // was temporarily used just to skip keys not in prefix_extractor domain
      range->SkipKey(iter);
      PERF_COUNTER_ADD(bloom_sst_miss_count, 1);
    } else {
      // PERF_COUNTER_ADD(bloom_sst_hit_count, 1);
      PerfContext* perf_ctx = get_perf_context();
      perf_ctx->bloom_sst_hit_count++;
    }
    ++i;
  }
}

size_t FullFilterBlockReader::ApproximateMemoryUsage() const {
  DBUG_TRACE;
  size_t usage = ApproximateFilterBlockMemoryUsage();
#ifdef ROCKSDB_MALLOC_USABLE_SIZE
  usage += malloc_usable_size(const_cast<FullFilterBlockReader*>(this));
#else
  usage += sizeof(*this);
#endif  // ROCKSDB_MALLOC_USABLE_SIZE
  return usage;
}

}  // namespace ROCKSDB_NAMESPACE