// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_DEBUG_RASTERIZE_AND_RECORD_BENCHMARK_H_
#define CC_DEBUG_RASTERIZE_AND_RECORD_BENCHMARK_H_

#include <stddef.h>

#include <map>
#include <utility>
#include <vector>

#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "base/time/time.h"
#include "cc/debug/micro_benchmark_controller.h"
#include "cc/playback/recording_source.h"

namespace base {
class DictionaryValue;
}

namespace cc {

class LayerTree;

class RasterizeAndRecordBenchmark : public MicroBenchmark {
 public:
  explicit RasterizeAndRecordBenchmark(
      std::unique_ptr<base::Value> value,
      const MicroBenchmark::DoneCallback& callback);
  ~RasterizeAndRecordBenchmark() override;

  // Implements MicroBenchmark interface.
  void DidUpdateLayers(LayerTree* layer_tree) override;
  void RunOnLayer(PictureLayer* layer) override;

  std::unique_ptr<MicroBenchmarkImpl> CreateBenchmarkImpl(
      scoped_refptr<base::SingleThreadTaskRunner> origin_task_runner) override;

 private:
  void RecordRasterResults(std::unique_ptr<base::Value> results);

  struct RecordResults {
    RecordResults();
    ~RecordResults();

    int pixels_recorded;
    size_t bytes_used;
    base::TimeDelta total_best_time[RecordingSource::RECORDING_MODE_COUNT];
  };

  RecordResults record_results_;
  int record_repeat_count_;
  std::unique_ptr<base::Value> settings_;
  std::unique_ptr<base::DictionaryValue> results_;

  // The following is used in DCHECKs.
  bool main_thread_benchmark_done_;

  LayerTree* layer_tree_;

  base::WeakPtrFactory<RasterizeAndRecordBenchmark> weak_ptr_factory_;
};

}  // namespace cc

#endif  // CC_DEBUG_RASTERIZE_AND_RECORD_BENCHMARK_H_
