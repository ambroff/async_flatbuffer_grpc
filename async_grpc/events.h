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

enum class Event {
  NEW_CONNECTION = 0,
  READ,
  WRITE_NEEDED,
  WRITE,
  FINISH,
  DONE
};

class EventHandlerInterface {
 public:
  virtual ~EventHandlerInterface() = default;

  virtual void HandleEvent(Event event, RpcInterface* rpc, bool ok) = 0;
};

class EventBase {
 public:
  explicit EventBase(Event event) : event(event) {}
  virtual ~EventBase() = default;
  virtual void Handle() = 0;

 protected:
  const Event event;
};

class EventDeleter {
 public:
  enum Action { DELETE = 0, DO_NOT_DELETE };

  // The default action 'DELETE' is used implicitly, for instance for a
  // new UniqueEventPtr or a UniqueEventPtr that is created by
  // 'return nullptr'.
  EventDeleter() : action_(DELETE) {}
  explicit EventDeleter(Action action) : action_(action) {}
  void operator()(EventBase* e) {
    if (e != nullptr && action_ == DELETE) {
      delete e;
    }
  }

 private:
  Action action_;
};

using UniqueEventPtr = std::unique_ptr<EventBase, EventDeleter>;
using EventQueue = common::BlockingQueue<UniqueEventPtr>;

// Flows through gRPC's CompletionQueue and then our EventQueue.
class CompletionQueueRpcEvent : public EventBase {
 public:
  CompletionQueueRpcEvent(Event event, EventQueue* event_queue, EventHandlerInterface* event_handler, RpcInterface* rpc)
      : EventBase{event},
        event_queue_{event_queue},
        event_handler_{event_handler},
        rpc_{rpc}
  {
  }

  void PushToEventQueue() {
    event_queue_->Push(
        UniqueEventPtr(this, EventDeleter(EventDeleter::DO_NOT_DELETE)));
  }

  void Handle() override {
    pending_ = false;
    event_handler_->HandleEvent(event, rpc_, ok);
  }

  bool ok() const {
    return ok_;
  }

  bool pending() const {
    pending_;
  }

 private:
  EventQueue* event_queue_;
  EventHandlerInterface* event_handler_;
  RpcInterface* rpc_;
  bool ok_{false};
  bool pending_{false};
};

}
