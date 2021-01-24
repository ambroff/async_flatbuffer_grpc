/*
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

#include <flatbuffers/grpc.h>

#include <memory>
#include <queue>
#include <unordered_set>
#include <utility>

#include "async_grpc/common/blocking_queue.h"
#include "async_grpc/common/mutex.h"
#include "async_grpc/events.h"
#include "async_grpc/execution_context.h"
#include "async_grpc/rpc_handler_info.h"
#include "async_grpc/rpc_interface.h"
#include "grpc++/grpc++.h"
#include "grpc++/impl/codegen/async_stream.h"
#include "grpc++/impl/codegen/async_unary_call.h"
#include "grpc++/impl/codegen/proto_utils.h"
#include "grpc++/impl/codegen/service_type.h"

namespace async_grpc {

namespace detail {

// Finishes the gRPC for non-streaming response RPCs, i.e. NORMAL_RPC and
// CLIENT_STREAMING. If no 'msg' is passed, we signal an error to the client as
// the server is not honoring the gRPC call signature.
template <typename ReaderWriter>
void SendUnaryFinish(ReaderWriter* reader_writer, ::grpc::Status status,
                     const google::protobuf::Message* msg,
                     EventBase* rpc_event) {
  if (msg) {
    reader_writer->Finish(*msg, status, rpc_event);
  } else {
    reader_writer->FinishWithError(status, rpc_event);
  }
}

}  // namespace detail

// TODO(cschuet): Add a unittest that tests the logic of this class.
template <typename HandlerType, typename ServiceType, typename RequestType,
          typename ResponseType>
class Rpc : public RpcInterface {
 public:
  // Flows only through our EventQueue.
  struct InternalRpcEvent : public EventBase {
    InternalRpcEvent(Event event, std::weak_ptr<Rpc> rpc)
        : EventBase{event}, rpc{std::move(rpc)} {}
    void Handle() override {
      if (auto rpc_shared = rpc.lock()) {
        rpc_shared->service()->HandleEvent(event, rpc_shared.get(), true);
      } else {
        LOG(WARNING) << "Ignoring stale event.";
      }
    }

    std::weak_ptr<Rpc> rpc;
  };

  Rpc(int method_index, ::grpc::ServerCompletionQueue* server_completion_queue,
      EventQueue* event_queue, ExecutionContext* execution_context,
      const RpcHandlerInfo& rpc_handler_info, ServiceType* service,
      WeakPtrFactory weak_ptr_factory)
      : method_index_(method_index),
        server_completion_queue_(server_completion_queue),
        event_queue_(event_queue),
        execution_context_(execution_context),
        rpc_handler_info_(rpc_handler_info),
        service_{service},
        weak_ptr_factory_{std::move(weak_ptr_factory)},
        new_connection_event_(Event::NEW_CONNECTION, event_queue, service, this),
        read_event_(Event::READ, event_queue, service, this),
        write_event_(Event::WRITE, event_queue, service, this),
        finish_event_(Event::FINISH, event_queue, service, this),
        done_event_(Event::DONE, event_queue, service, this) {
    InitializeReadersAndWriters(rpc_handler_info_.rpc_type);
  }

  Rpc(const Rpc&) = delete;
  Rpc& operator=(const Rpc&) = delete;

  std::unique_ptr<RpcInterface> Clone() override {
    return common::make_unique<
        Rpc<HandlerType, ServiceType, RequestType, ResponseType>>(
        method_index_, server_completion_queue_, event_queue_,
        execution_context_, rpc_handler_info_, service_, weak_ptr_factory_);
  }

  void OnConnection() override {
    if (!handler_) {
      handler_ = std::make_unique<HandlerType>();
      handler_->SetRpc(this);
      handler_->SetExecutionContext(execution_context_);
      handler_->Initialize();
    }

    // For request-streaming RPCs ask the client to start sending requests.
    RequestStreamingReadIfNeeded();
  }

  void OnRequest() override { handler_->OnRequest(request_); }

  void OnReadsDone() override { handler_->OnReadsDone(); }

  void OnFinish() override { handler_->OnFinish(); }

  void RequestNextMethodInvocation() override {
    // Ask gRPC to notify us when the connection terminates.
    SetRpcEventState(Event::DONE, true);
    // TODO(gaschler): Asan reports direct leak of this new from both calls
    // StartServing and HandleNewConnection.
    server_context_.AsyncNotifyWhenDone(GetRpcEvent(Event::DONE));

    // Make sure after terminating the connection, gRPC notifies us with this
    // event.
    SetRpcEventState(Event::NEW_CONNECTION, true);
    switch (rpc_handler_info_.rpc_type) {
      case ::grpc::internal::RpcMethod::BIDI_STREAMING:
        service_->RequestAsyncBidiStreaming(
            method_index_, &server_context_, streaming_interface(),
            server_completion_queue_, server_completion_queue_,
            GetRpcEvent(Event::NEW_CONNECTION));
        break;
      case ::grpc::internal::RpcMethod::CLIENT_STREAMING:
        service_->RequestAsyncClientStreaming(
            method_index_, &server_context_, streaming_interface(),
            server_completion_queue_, server_completion_queue_,
            GetRpcEvent(Event::NEW_CONNECTION));
        break;
      case ::grpc::internal::RpcMethod::NORMAL_RPC:
        service_->RequestAsyncUnary(
            method_index_, &server_context_, request_.get(),
            streaming_interface(), server_completion_queue_,
            server_completion_queue_, GetRpcEvent(Event::NEW_CONNECTION));
        break;
      case ::grpc::internal::RpcMethod::SERVER_STREAMING:
        service_->RequestAsyncServerStreaming(
            method_index_, &server_context_, request_.get(),
            streaming_interface(), server_completion_queue_,
            server_completion_queue_, GetRpcEvent(Event::NEW_CONNECTION));
        break;
    }
  }

  void RequestStreamingReadIfNeeded() override {
    // For request-streaming RPCs ask the client to start sending requests.
    switch (rpc_handler_info_.rpc_type) {
      case ::grpc::internal::RpcMethod::BIDI_STREAMING:
      case ::grpc::internal::RpcMethod::CLIENT_STREAMING:
        SetRpcEventState(Event::READ, true);
        async_reader_interface()->Read(request_.get(),
                                       GetRpcEvent(Event::READ));
        break;
      case ::grpc::internal::RpcMethod::NORMAL_RPC:
      case ::grpc::internal::RpcMethod::SERVER_STREAMING:
        // For NORMAL_RPC and SERVER_STREAMING we don't need to queue an event,
        // since gRPC automatically issues a READ request and places the request
        // into the 'Message' we provided to 'RequestAsyncUnary' above.
        OnRequest();
        OnReadsDone();
        break;
    }
  }

  void HandleSendQueue() override {
    SendItem send_item;
    {
      common::MutexLocker locker(&send_queue_lock_);
      if (send_queue_.empty() || IsRpcEventPending(Event::WRITE) ||
          IsRpcEventPending(Event::FINISH)) {
        return;
      }

      send_item = std::move(send_queue_.front());
      send_queue_.pop();
    }

    if (!send_item.msg ||
        rpc_handler_info_.rpc_type == ::grpc::internal::RpcMethod::NORMAL_RPC ||
        rpc_handler_info_.rpc_type ==
            ::grpc::internal::RpcMethod::CLIENT_STREAMING) {
      PerformFinish(std::move(send_item.msg), send_item.status);
      return;
    }

    PerformWrite(std::move(send_item.msg), send_item.status);
  }

  void Write(std::any message) override {
    Write(std::move(std::any_cast<flatbuffers::grpc::Message<ResponseType>>(message)));
  }

  void Write(flatbuffers::grpc::Message<ResponseType> message) {
    EnqueueMessage(SendItem{std::move(message), ::grpc::Status::OK});
    event_queue_->Push(UniqueEventPtr(
        new InternalRpcEvent(Event::WRITE_NEEDED, weak_ptr_factory_(this))));
  }

  void Finish(::grpc::Status status) override {
    EnqueueMessage(SendItem{{}, status});
    event_queue_->Push(UniqueEventPtr(
        new InternalRpcEvent(Event::WRITE_NEEDED, weak_ptr_factory_(this))));
  }

  ServiceType* service() { return service_; }

  bool IsRpcEventPending(Event event) {
    return GetRpcEvent(event)->pending();
  }

  bool IsAnyEventPending() override {
    return IsRpcEventPending(Event::DONE) || IsRpcEventPending(Event::READ) ||
           IsRpcEventPending(Event::WRITE) || IsRpcEventPending(Event::FINISH);
  }

  void SetEventQueue(EventQueue* event_queue) override {
    event_queue_ = event_queue;
  }

  EventQueue* event_queue() { return event_queue_; }

  std::weak_ptr<RpcInterface> GetWeakPtr() override {
    return weak_ptr_factory_(this);
  }

  HandlerType& handler() { return *handler_; }

 private:
  struct SendItem {
    // If optional is not set, that means there is not response body.
    std::optional<flatbuffers::grpc::Message<ResponseType>> msg;
    ::grpc::Status status;
  };

  void InitializeReadersAndWriters(
      ::grpc::internal::RpcMethod::RpcType rpc_type) {
    switch (rpc_type) {
      case ::grpc::internal::RpcMethod::BIDI_STREAMING:
        server_async_reader_writer_ =
            common::make_unique<::grpc::ServerAsyncReaderWriter<
                google::protobuf::Message, google::protobuf::Message>>(
                &server_context_);
        break;
      case ::grpc::internal::RpcMethod::CLIENT_STREAMING:
        server_async_reader_ = common::make_unique<::grpc::ServerAsyncReader<
            google::protobuf::Message, google::protobuf::Message>>(
            &server_context_);
        break;
      case ::grpc::internal::RpcMethod::NORMAL_RPC:
        server_async_response_writer_ = common::make_unique<
            ::grpc::ServerAsyncResponseWriter<google::protobuf::Message>>(
            &server_context_);
        break;
      case ::grpc::internal::RpcMethod::SERVER_STREAMING:
        server_async_writer_ = common::make_unique<
            ::grpc::ServerAsyncWriter<google::protobuf::Message>>(
            &server_context_);
        break;
    }
  }

  CompletionQueueRpcEvent* GetRpcEvent(Event event) {
    switch (event) {
      case Event::NEW_CONNECTION:
        return &new_connection_event_;
      case Event::READ:
        return &read_event_;
      case Event::WRITE_NEEDED:
        LOG(FATAL) << "Rpc does not store Event::WRITE_NEEDED.";
        break;
      case Event::WRITE:
        return &write_event_;
      case Event::FINISH:
        return &finish_event_;
      case Event::DONE:
        return &done_event_;
    }
    LOG(FATAL) << "Never reached.";
  }

  void SetRpcEventState(Event event, bool pending) {
    // TODO(gaschler): Since the only usage is setting this true at creation,
    // consider removing this method.
    return &GetRpcEvent(event)->pending(pending);
  }

  void EnqueueMessage(SendItem&& send_item) {
    common::MutexLocker locker(&send_queue_lock_);
    send_queue_.emplace(std::forward<SendItem>(send_item));
  }

  void PerformFinish(flatbuffers::grpc::Message<ResponseType> message,
                     ::grpc::Status status) {
    SetRpcEventState(Event::FINISH, true);
    switch (rpc_handler_info_.rpc_type) {
      case ::grpc::internal::RpcMethod::BIDI_STREAMING:
        CHECK(!message);
        server_async_reader_writer_->Finish(status, GetRpcEvent(Event::FINISH));
        break;
      case ::grpc::internal::RpcMethod::CLIENT_STREAMING:
        response_ = std::move(message);
        SendUnaryFinish(server_async_reader_.get(), status, response_.get(),
                        GetRpcEvent(Event::FINISH));
        break;
      case ::grpc::internal::RpcMethod::NORMAL_RPC:
        response_ = std::move(message);
        SendUnaryFinish(server_async_response_writer_.get(), status,
                        response_.get(), GetRpcEvent(Event::FINISH));
        break;
      case ::grpc::internal::RpcMethod::SERVER_STREAMING:
        CHECK(!message);
        server_async_writer_->Finish(status, GetRpcEvent(Event::FINISH));
        break;
    }
  }

  void PerformWrite(flatbuffers::grpc::Message<ResponseType> message,
                    ::grpc::Status status) {
    CHECK(message) << "PerformWrite must be called with a non-null message";
    CHECK_NE(rpc_handler_info_.rpc_type,
             ::grpc::internal::RpcMethod::NORMAL_RPC);
    CHECK_NE(rpc_handler_info_.rpc_type,
             ::grpc::internal::RpcMethod::CLIENT_STREAMING);
    SetRpcEventState(Event::WRITE, true);
    response_ = std::move(message);
    async_writer_interface()->Write(response_, GetRpcEvent(Event::WRITE));
  }

  ::grpc::internal::AsyncReaderInterface<
      flatbuffers::grpc::Message<RequestType>>*
  async_reader_interface() {
    switch (rpc_handler_info_.rpc_type) {
      case ::grpc::internal::RpcMethod::BIDI_STREAMING:
        return server_async_reader_writer_.get();
      case ::grpc::internal::RpcMethod::CLIENT_STREAMING:
        return server_async_reader_.get();
      case ::grpc::internal::RpcMethod::NORMAL_RPC:
        LOG(FATAL) << "For NORMAL_RPC no streaming reader interface exists.";
      case ::grpc::internal::RpcMethod::SERVER_STREAMING:
        LOG(FATAL)
            << "For SERVER_STREAMING no streaming reader interface exists.";
    }
    LOG(FATAL) << "Never reached.";
  }

  ::grpc::internal::AsyncWriterInterface<
      flatbuffers::grpc::Message<ResponseType>>*
  async_writer_interface() {
    switch (rpc_handler_info_.rpc_type) {
      case ::grpc::internal::RpcMethod::BIDI_STREAMING:
        return server_async_reader_writer_.get();
      case ::grpc::internal::RpcMethod::CLIENT_STREAMING:
      case ::grpc::internal::RpcMethod::NORMAL_RPC:
        LOG(FATAL) << "For NORMAL_RPC and CLIENT_STREAMING no streaming writer "
                      "interface exists.";
        break;
      case ::grpc::internal::RpcMethod::SERVER_STREAMING:
        return server_async_writer_.get();
    }
    LOG(FATAL) << "Never reached.";
  }

  ::grpc::internal::ServerAsyncStreamingInterface* streaming_interface() {
    switch (rpc_handler_info_.rpc_type) {
      case ::grpc::internal::RpcMethod::BIDI_STREAMING:
        return server_async_reader_writer_.get();
      case ::grpc::internal::RpcMethod::CLIENT_STREAMING:
        return server_async_reader_.get();
      case ::grpc::internal::RpcMethod::NORMAL_RPC:
        return server_async_response_writer_.get();
      case ::grpc::internal::RpcMethod::SERVER_STREAMING:
        return server_async_writer_.get();
    }
    LOG(FATAL) << "Never reached.";
  }

  int method_index_;
  ::grpc::ServerCompletionQueue* server_completion_queue_;
  EventQueue* event_queue_;
  ExecutionContext* execution_context_;
  RpcHandlerInfo rpc_handler_info_;
  ServiceType* service_;
  WeakPtrFactory weak_ptr_factory_;
  ::grpc::ServerContext server_context_;

  CompletionQueueRpcEvent new_connection_event_;
  CompletionQueueRpcEvent read_event_;
  CompletionQueueRpcEvent write_event_;
  CompletionQueueRpcEvent finish_event_;
  CompletionQueueRpcEvent done_event_;

  flatbuffers::grpc::Message<RequestType> request_;
  flatbuffers::grpc::Message<ResponseType> response_;

  std::unique_ptr<HandlerType> handler_;

  std::unique_ptr<::grpc::ServerAsyncResponseWriter<
      flatbuffers::grpc::Message<ResponseType>>>
      server_async_response_writer_;
  std::unique_ptr<
      ::grpc::ServerAsyncReader<flatbuffers::grpc::Message<RequestType>,
                                flatbuffers::grpc::Message<ResponseType>>>
      server_async_reader_;
  std::unique_ptr<
      ::grpc::ServerAsyncReaderWriter<flatbuffers::grpc::Message<RequestType>,
                                      flatbuffers::grpc::Message<ResponseType>>>
      server_async_reader_writer_;
  std::unique_ptr<
      ::grpc::ServerAsyncWriter<flatbuffers::grpc::Message<ResponseType>>>
      server_async_writer_;

  common::Mutex send_queue_lock_;
  std::queue<SendItem> send_queue_;
};

using EventQueue = EventQueue;

// This class keeps track of all in-flight RPCs for a 'Service'. Make sure that
// all RPCs have been terminated and removed from this object before it goes out
// of scope.
class ActiveRpcs {
 public:
  ActiveRpcs() : lock_{} {}

  ~ActiveRpcs() EXCLUDES(lock_) {
    common::MutexLocker locker(&lock_);
    if (!rpcs_.empty()) {
      LOG(FATAL) << "RPCs still in flight!";
    }
  }

  std::shared_ptr<RpcInterface> Add(std::unique_ptr<RpcInterface> rpc)
      EXCLUDES(lock_) {
    common::MutexLocker locker(&lock_);
    std::shared_ptr<RpcInterface> shared_ptr_rpc = std::move(rpc);
    const auto result = rpcs_.emplace(shared_ptr_rpc.get(), shared_ptr_rpc);
    CHECK(result.second) << "RPC already active.";
    return shared_ptr_rpc;
  }

  bool Remove(RpcInterface* rpc) EXCLUDES(lock_) {
    common::MutexLocker locker(&lock_);

    auto it = rpcs_.find(rpc);
    if (it != rpcs_.end()) {
      rpcs_.erase(it);
      return true;
    }

    return false;
  }

  WeakPtrFactory GetWeakPtrFactory() {
    return [this](RpcInterface* rpc) { return GetWeakPtr(rpc); };
  }

 private:
  std::weak_ptr<RpcInterface> GetWeakPtr(RpcInterface* rpc) {
    common::MutexLocker locker(&lock_);
    auto it = rpcs_.find(rpc);
    CHECK(it != rpcs_.end());
    return it->second;
  }

  common::Mutex lock_;
  std::map<RpcInterface*, std::shared_ptr<RpcInterface>> rpcs_;
};

}  // namespace async_grpc
