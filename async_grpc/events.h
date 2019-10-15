/**
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

#include "async_grpc/common/blocking_queue.h"
#include "async_grpc/rpc_interface.h"

namespace async_grpc {

// Flows through gRPC's CompletionQueue and then our EventQueue.
class CompletionQueueRpcEvent : public EventBase {
 public:
  CompletionQueueRpcEvent(Event event, EventQueue* event_queue,
                          EventHandlerInterface* event_handler,
                          RpcInterface* rpc);

  void PushToEventQueue();

  void Handle() override;

  void ok(bool ok);

  void pending(bool pending);

  [[nodiscard]] bool ok() const;

  [[nodiscard]] bool pending() const;

 private:
  EventQueue* event_queue_;
  EventHandlerInterface* event_handler_;
  RpcInterface* rpc_;
  bool ok_{false};
  bool pending_{false};
};

}  // namespace async_grpc
