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

#include <grpcpp/support/status.h>

#include <functional>
#include <memory>

namespace async_grpc {

enum class Event {
  NEW_CONNECTION = 0,
  READ,
  WRITE_NEEDED,
  WRITE,
  FINISH,
  DONE
};

class RpcInterface;

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

class RpcInterface {
 public:
  virtual ~RpcInterface() = default;

  virtual void RequestNextMethodInvocation() = 0;

  virtual std::weak_ptr<RpcInterface> GetWeakPtr() = 0;

  virtual std::unique_ptr<RpcInterface> Clone() = 0;

  virtual void OnConnection() = 0;

  virtual void OnRequest() = 0;

  virtual void RequestStreamingReadIfNeeded() = 0;

  virtual void OnReadsDone() = 0;

  virtual void Finish(::grpc::Status status) = 0;

  virtual void OnFinish() = 0;

  virtual void SetEventQueue(EventQueue* event_queue) = 0;

  virtual bool IsAnyEventPending() = 0;

  virtual void HandleSendQueue() = 0;
};

using WeakPtrFactory =
    std::function<std::weak_ptr<RpcInterface>(RpcInterface*)>;

}  // namespace async_grpc
