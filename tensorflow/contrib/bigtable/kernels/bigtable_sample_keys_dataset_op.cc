/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/contrib/bigtable/kernels/bigtable_lib.h"
#include "tensorflow/core/framework/op_kernel.h"

namespace tensorflow {
namespace data {
namespace {

class BigtableSampleKeysDatasetOp : public DatasetOpKernel {
 public:
  using DatasetOpKernel::DatasetOpKernel;

  void MakeDataset(OpKernelContext* ctx, DatasetBase** output) override {
    core::RefCountPtr<BigtableTableResource> resource;
    OP_REQUIRES_OK(ctx,
                   LookupResource(ctx, HandleFromInput(ctx, 0), &resource));
    *output = new Dataset(ctx, resource.get());
  }

 private:
  class Dataset : public DatasetBase {
   public:
    explicit Dataset(OpKernelContext* ctx, BigtableTableResource* table)
        : DatasetBase(DatasetContext(ctx)), table_(table) {
      table_->Ref();
    }

    ~Dataset() override { table_->Unref(); }

    std::unique_ptr<IteratorBase> MakeIteratorInternal(
        const string& prefix) const override {
      return std::unique_ptr<IteratorBase>(new Iterator(
          {this, strings::StrCat(prefix, "::BigtableSampleKeys")}));
    }

    const DataTypeVector& output_dtypes() const override {
      static DataTypeVector* dtypes = new DataTypeVector({DT_STRING});
      return *dtypes;
    }

    const std::vector<PartialTensorShape>& output_shapes() const override {
      static std::vector<PartialTensorShape>* shapes =
          new std::vector<PartialTensorShape>({{}});
      return *shapes;
    }

    string DebugString() const override {
      return "BigtableRangeKeyDatasetOp::Dataset";
    }

    BigtableTableResource* table() const { return table_; }

    Status CheckExternalState() const override {
      return errors::FailedPrecondition(DebugString(),
                                        " depends on external state.");
    }

   protected:
    Status AsGraphDefInternal(SerializationContext* ctx,
                              DatasetGraphDefBuilder* b,
                              Node** output) const override {
      return errors::Unimplemented(DebugString(),
                                   " does not support serialization");
    }

   private:
    class Iterator : public DatasetIterator<Dataset> {
     public:
      explicit Iterator(const Params& params)
          : DatasetIterator<Dataset>(params) {}

      Status Initialize(IteratorContext* ctx) override {
        ::google::cloud::StatusOr<
            std::vector<::google::cloud::bigtable::RowKeySample>>
            sampled_rows = dataset()->table()->table().SampleRows();
        if (!sampled_rows.ok()) {
          row_keys_.clear();
          return GcpStatusToTfStatus(sampled_rows.status());
        }
        row_keys_ = std::move(*sampled_rows);
        return Status::OK();
      }

      Status GetNextInternal(IteratorContext* ctx,
                             std::vector<Tensor>* out_tensors,
                             bool* end_of_sequence) override {
        mutex_lock l(mu_);
        if (index_ < row_keys_.size()) {
          out_tensors->emplace_back(ctx->allocator({}), DT_STRING,
                                    TensorShape({}));
          out_tensors->back().scalar<tstring>()() =
              string(row_keys_[index_].row_key);
          *end_of_sequence = false;
          index_++;
        } else {
          *end_of_sequence = true;
        }
        return Status::OK();
      }

     protected:
      Status SaveInternal(IteratorStateWriter* writer) override {
        return errors::Unimplemented("SaveInternal is currently not supported");
      }

      Status RestoreInternal(IteratorContext* ctx,
                             IteratorStateReader* reader) override {
        return errors::Unimplemented(
            "RestoreInternal is currently not supported");
      }

     private:
      mutex mu_;
      size_t index_ = 0;
      std::vector<::google::cloud::bigtable::RowKeySample> row_keys_;
    };

    BigtableTableResource* const table_;
  };
};

REGISTER_KERNEL_BUILDER(Name("BigtableSampleKeysDataset").Device(DEVICE_CPU),
                        BigtableSampleKeysDatasetOp);

}  // namespace
}  // namespace data
}  // namespace tensorflow
