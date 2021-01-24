/*
 * Copyright 2018 The Cartographer Authors
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

#include <flatbuffers/grpc.h>

namespace async_grpc::testing {

template <class RpcHandlerType>
class RpcHandlerWrapper : public RpcHandlerType {
 public:
  enum RpcHandlerEvent { ON_REQUEST, ON_READS_DONE, ON_FINISH };
  using EventCallback = std::function<void(RpcHandlerEvent)>;

  RpcHandlerWrapper(EventCallback event_callback)
      : event_callback_(event_callback) {}

  void OnRequest(const flatbuffers::grpc::Message<typename RpcHandlerType::RequestType> &request) override {
    RpcHandlerType::OnRequest(request);
    event_callback_(ON_REQUEST);
  }

  void OnReadsDone() override {
    RpcHandlerType::OnReadsDone();
    event_callback_(ON_READS_DONE);
  }

  void OnFinish() override {
    RpcHandlerType::OnFinish();
    event_callback_(ON_FINISH);
  }

 private:
  EventCallback event_callback_;
};

}  // namespace async_grpc::testing
