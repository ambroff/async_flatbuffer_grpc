/**
 * Copyright 2017 The Cartographer Authors
 * Copyright 2021 Kyle Ambroff-Kao <kyle@ambroffkao.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "async_grpc/execution_context.h"
#include "async_grpc/rpc.h"
#include "async_grpc/rpc_handler_info.h"
#include "async_grpc/rpc_service_method_traits.h"
#include "async_grpc/span.h"
#include "glog/logging.h"
#include "grpc++/grpc++.h"
#if BUILD_TRACING
#include "async_grpc/opencensus_span.h"
#endif

namespace async_grpc {

namespace detail {

template <typename MessageType>
class SpecificMessage : public AnyMessage {
 public:
  SpecificMessage(MessageType message) {}

 private:
  MessageType message_;
};

}  // namespace detail

template <typename RpcServiceMethodConcept>
class RpcHandler {
 public:
  using RpcServiceMethod = RpcServiceMethodTraits<RpcServiceMethodConcept>;
  using RequestType = typename RpcServiceMethod::RequestType;
  using ResponseType = typename RpcServiceMethod::ResponseType;

  virtual ~RpcHandler() = default;

  class Writer {
   public:
    explicit Writer(std::weak_ptr<RpcInterface> rpc) : rpc_{std::move(rpc)} {}

    bool Write(flatbuffers::grpc::Message<ResponseType> message) const {
      if (auto rpc = rpc_.lock()) {
        rpc->Write(
            std::make_unique<detail::SpecificMessage<
                flatbuffers::grpc::Message<ResponseType>>>(std::move(message)));
        return true;
      }
      return false;
    }
    bool WritesDone() const {
      if (auto rpc = rpc_.lock()) {
        rpc->Finish(::grpc::Status::OK);
        return true;
      }
      return false;
    }
    bool Finish(const ::grpc::Status& status) const {
      if (auto rpc = rpc_.lock()) {
        rpc->Finish(status);
#if BUILD_TRACING
        auto* span = rpc->handler()->trace_span();
        if (span) {
          span->SetStatus(status);
        }
#endif
        return true;
      }
      return false;
    }

   private:
    const std::weak_ptr<RpcInterface> rpc_;
  };

#if BUILD_TRACING
  RpcHandler()
      : span_(
            OpencensusSpan::StartSpan(RpcServiceMethodConcept::MethodName())) {}
  virtual ~RpcHandler() { span_->End(); }
#endif

#if BUILD_TRACING
  virtual Span* trace_span() { return span_.get(); }
#endif

  virtual void SetExecutionContext(ExecutionContext* execution_context) {
    execution_context_ = execution_context;
  }

  virtual void SetRpc(RpcInterface* rpc) { rpc_ = rpc; }

  virtual void Initialize() {}

  virtual void OnRequest(flatbuffers::grpc::Message<RequestType>& request) = 0;

  void Finish(::grpc::Status status) {
    rpc_->Finish(status);
#if BUILD_TRACING
    span_->SetStatus(status);
#endif
  }

  void Send(flatbuffers::grpc::Message<ResponseType> response) {
    rpc_->Write(
        std::make_unique<
            detail::SpecificMessage<flatbuffers::grpc::Message<ResponseType>>>(
            std::move(response)));
  }

  template <typename T>
  ExecutionContext::Synchronized<T> GetContext() {
    return {execution_context_->lock(), execution_context_};
  }

  template <typename T>
  T* GetUnsynchronizedContext() {
    return dynamic_cast<T*>(execution_context_);
  }

  Writer GetWriter() { return Writer(rpc_->GetWeakPtr()); }

  virtual void OnReadsDone() {}
  virtual void OnFinish() {}

 private:
  RpcInterface* rpc_;
  ExecutionContext* execution_context_;
  std::unique_ptr<Span> span_;
};

}  // namespace async_grpc
