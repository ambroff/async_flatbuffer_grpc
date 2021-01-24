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

#include "async_grpc/common/make_unique.h"
#include "async_grpc/execution_context.h"
#include "async_grpc/span.h"
#include "async_grpc/rpc_interface.h"

#include <flatbuffers/grpc.h>

#include "grpc++/grpc++.h"

namespace async_grpc {

class RpcInterface;

struct RpcHandlerInfo;
class Service;

using RpcFactory = std::function<std::unique_ptr<RpcInterface>(
    int,
    ::grpc::ServerCompletionQueue*,
    EventQueue*,
    ExecutionContext*,
    const RpcHandlerInfo& rpc_handler_info, Service* service,
    WeakPtrFactory weak_ptr_factory)>;

struct RpcHandlerInfo {
  const RpcFactory rpc_factory;
  const ::grpc::internal::RpcMethod::RpcType rpc_type;
  const std::string fully_qualified_name;
};

}  // namespace async_grpc
