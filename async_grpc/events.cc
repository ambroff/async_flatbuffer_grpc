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
#include "async_grpc/events.h"

namespace async_grpc {

CompletionQueueRpcEvent::CompletionQueueRpcEvent(
    async_grpc::Event event, async_grpc::EventQueue* event_queue,
    async_grpc::EventHandlerInterface* event_handler,
    async_grpc::RpcInterface* rpc)
    : EventBase{event},
      event_queue_{event_queue},
      event_handler_{event_handler},
      rpc_{rpc} {}

void CompletionQueueRpcEvent::PushToEventQueue() {
  event_queue_->Push(
      UniqueEventPtr(this, EventDeleter(EventDeleter::DO_NOT_DELETE)));
}

void CompletionQueueRpcEvent::Handle() {
  pending_ = false;
  event_handler_->HandleEvent(event, rpc_, ok_);
}

void CompletionQueueRpcEvent::ok(bool ok) { ok_ = ok; }

}  // namespace async_grpc
