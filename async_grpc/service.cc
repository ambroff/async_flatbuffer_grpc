/*
 * Copyright 2017 The Cartographer Authors
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

#include <cstdlib>

#include "async_grpc/server.h"
#include "glog/logging.h"
#include "grpc++/impl/codegen/proto_utils.h"

namespace async_grpc {

Service::Service(const std::string& service_name,
                 const std::map<std::string, RpcHandlerInfo>& rpc_handler_infos,
                 EventQueueSelector event_queue_selector)
    : rpc_handler_infos_(rpc_handler_infos),
      event_queue_selector_(event_queue_selector) {
  for (const auto& rpc_handler_info : rpc_handler_infos_) {
    // The 'handler' below is set to 'nullptr' indicating that we want to
    // handle this method asynchronously.
    this->AddMethod(new ::grpc::internal::RpcServiceMethod(
        rpc_handler_info.second.fully_qualified_name.c_str(),
        rpc_handler_info.second.rpc_type, nullptr /* handler */));
  }
}

void Service::StartServing(
    std::vector<CompletionQueueThread>& completion_queue_threads,
    ExecutionContext* execution_context) {
  int i = 0;
  for (const auto& rpc_handler_info : rpc_handler_infos_) {
    for (auto& completion_queue_thread : completion_queue_threads) {
      std::shared_ptr<RpcInterface> rpc =
          active_rpcs_.Add(rpc_handler_info.second.rpc_factory(
              i, completion_queue_thread.completion_queue(),
              event_queue_selector_(), execution_context,
              rpc_handler_info.second, this, active_rpcs_.GetWeakPtrFactory()));
      rpc->RequestNextMethodInvocation();
    }
    ++i;
  }
}

void Service::StopServing() { shutting_down_ = true; }

void Service::HandleEvent(Event event, RpcInterface* rpc, bool ok) {
  switch (event) {
    case Event::NEW_CONNECTION:
      HandleNewConnection(rpc, ok);
      break;
    case Event::READ:
      HandleRead(rpc, ok);
      break;
    case Event::WRITE_NEEDED:
    case Event::WRITE:
      HandleWrite(rpc, ok);
      break;
    case Event::FINISH:
      HandleFinish(rpc, ok);
      break;
    case Event::DONE:
      HandleDone(rpc, ok);
      break;
  }
}

void Service::HandleNewConnection(RpcInterface* rpc, bool ok) {
  if (shutting_down_) {
    if (ok) {
      LOG(WARNING) << "Server shutting down. Refusing to handle new RPCs.";
    }
    active_rpcs_.Remove(rpc);
    return;
  }

  if (!ok) {
    LOG(ERROR) << "Failed to establish connection for unknown reason.";
    active_rpcs_.Remove(rpc);
  }

  if (ok) {
    rpc->OnConnection();
  }

  // Create new active rpc to handle next connection and register it for the
  // incoming connection. Assign event queue in a round-robin fashion.
  std::unique_ptr<RpcInterface> new_rpc = rpc->Clone();
  new_rpc->SetEventQueue(event_queue_selector_());
  active_rpcs_.Add(std::move(new_rpc))->RequestNextMethodInvocation();
}

void Service::HandleRead(RpcInterface* rpc, bool ok) {
  if (ok) {
    rpc->OnRequest();
    rpc->RequestStreamingReadIfNeeded();
    return;
  }

  // Reads completed.
  rpc->OnReadsDone();

  RemoveIfNotPending(rpc);
}

void Service::HandleWrite(RpcInterface* rpc, bool ok) {
  if (!ok) {
    LOG(ERROR) << "Write failed";
  }

  // Send the next message or potentially finish the connection.
  rpc->HandleSendQueue();

  RemoveIfNotPending(rpc);
}

void Service::HandleFinish(RpcInterface* rpc, bool ok) {
  if (!ok) {
    LOG(ERROR) << "Finish failed";
  }

  rpc->OnFinish();

  RemoveIfNotPending(rpc);
}

void Service::HandleDone(RpcInterface* rpc, bool ok) {
  RemoveIfNotPending(rpc);
}

void Service::RemoveIfNotPending(RpcInterface* rpc) {
  if (!rpc->IsAnyEventPending()) {
    active_rpcs_.Remove(rpc);
  }
}

}  // namespace async_grpc
