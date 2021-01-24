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

#include "async_grpc/completion_queue_thread.h"
#include "async_grpc/events.h"
#include "async_grpc/event_queue_thread.h"
#include "async_grpc/execution_context.h"
#include "async_grpc/rpc.h"
#include "async_grpc/rpc_handler.h"
#include "grpc++/impl/codegen/service_type.h"

namespace async_grpc {

// A 'Service' represents a generic service for gRPC asynchronous methods and is
// responsible for managing the lifetime of active RPCs issued against methods
// of the service and distributing incoming gRPC events to their respective
// 'Rpc' handler objects.
class Service : public ::grpc::Service, public EventHandlerInterface {
 public:
  using EventQueueSelector = std::function<EventQueue*()>;

  Service(const std::string& service_name,
          const std::map<std::string, RpcHandlerInfo>& rpc_handlers,
          EventQueueSelector event_queue_selector);
  void StartServing(std::vector<CompletionQueueThread>& completion_queues,
                    ExecutionContext* execution_context);
  void HandleEvent(Event event, RpcInterface* rpc, bool ok) override;
  void StopServing();

 private:
  void HandleNewConnection(RpcInterface* rpc, bool ok);
  void HandleRead(RpcInterface* rpc, bool ok);
  void HandleWrite(RpcInterface* rpc, bool ok);
  void HandleFinish(RpcInterface* rpc, bool ok);
  void HandleDone(RpcInterface* rpc, bool ok);

  void RemoveIfNotPending(RpcInterface* rpc);

  std::map<std::string, RpcHandlerInfo> rpc_handler_infos_;
  EventQueueSelector event_queue_selector_;
  ActiveRpcs active_rpcs_;
  bool shutting_down_ = false;
};

}  // namespace async_grpc
