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

#include <functional>
#include <memory>

#include <grpcpp/support/status.h>

namespace async_grpc {

class RpcInterface {
 public:
  virtual ~RpcInterface() = default;

  virtual void RequestNextMethodInvocation()  = 0;

  virtual std::weak_ptr<RpcInterface> GetWeakPtr() = 0;

  virtual std::unique_ptr<RpcInterface> Clone() = 0;

  virtual void OnConnection() = 0;

  virtual void Finish(::grpc::Status status) = 0;

  virtual void OnFinish() = 0;

  virtual void SetEventQueue(EventQueue* event_queue) = 0;

  virtual bool IsAnyEventPending() = 0;

  virtual void HandleSendQueue() = 0;
};

using WeakPtrFactory = std::function<std::weak_ptr<RpcInterface>(RpcInterface*)>;

}  // namespace async_grpc
