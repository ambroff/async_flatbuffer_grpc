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

#include "async_grpc/server.h"

#include <chrono>
#include <future>
#include <thread>

#include "async_grpc/async_client.h"
#include "async_grpc/client.h"
#include "async_grpc/execution_context.h"
#include "async_grpc/retry.h"
#include "async_grpc/rpc_handler.h"
#include "glog/logging.h"
#include "grpc++/grpc++.h"
#include "gtest/gtest.h"
#include "math_service.grpc.fb.h"

namespace async_grpc {
namespace {

using EchoResponder = std::function<bool()>;

class MathServerContext : public ExecutionContext {
 public:
  int additional_increment() { return 10; }
  std::promise<EchoResponder> echo_responder;
};

struct GetSumMethod {
  static constexpr const char* MethodName() {
    return "/async_grpc.proto.Math/GetSum";
  }
  using IncomingType = Stream<proto::GetSumRequest>;
  using OutgoingType = proto::GetSumResponse;
};

class GetSumHandler : public RpcHandler<GetSumMethod> {
 public:
  void OnRequest(flatbuffers::grpc::Message<proto::GetSumRequest>& request) override {
    sum_ += GetContext<MathServerContext>()->additional_increment();
    sum_ += request.GetRoot()->input();
  }

  void OnReadsDone() override {
    flatbuffers::grpc::MessageBuilder builder;
    auto response_offset = proto::CreateGetSumResponse(builder, sum_);
    builder.Finish(response_offset);
    Send(builder.ReleaseMessage<proto::GetSumResponse>());
  }

 private:
  int sum_ = 0;
};

struct GetRunningSumMethod {
  static constexpr const char* MethodName() {
    return "/async_grpc.proto.Math/GetRunningSum";
  }
  using IncomingType = Stream<proto::GetSumRequest>;
  using OutgoingType = Stream<proto::GetSumResponse>;
};

class GetRunningSumHandler : public RpcHandler<GetRunningSumMethod> {
 public:
  void OnRequest(flatbuffers::grpc::Message<proto::GetSumRequest>& request) override {
    sum_ += request.GetRoot()->input();

    // Respond twice to demonstrate bidirectional streaming.
    {
      flatbuffers::grpc::MessageBuilder builder;
      auto response_offset = proto::CreateGetSumRequest(builder, sum_);
      builder.Finish(response_offset);
      Send(builder.ReleaseMessage<proto::GetSumResponse>());
    }

    {
      flatbuffers::grpc::MessageBuilder builder;
      auto response_offset = proto::CreateGetSumRequest(builder, sum_);
      builder.Finish(response_offset);
      Send(builder.ReleaseMessage<proto::GetSumResponse>());
    }
  }

  void OnReadsDone() override { Finish(::grpc::Status::OK); }

 private:
  int sum_ = 0;
};

struct GetSquareMethod {
  static constexpr const char* MethodName() {
    return "/async_grpc.proto.Math/GetSquare";
  }
  using IncomingType = proto::GetSquareRequest;
  using OutgoingType = proto::GetSquareResponse;
};

class GetSquareHandler : public RpcHandler<GetSquareMethod> {
 public:
  void OnRequest(flatbuffers::grpc::Message<proto::GetSquareRequest>& request) override {
    auto input = request.GetRoot()->input();

    if (input < 0) {
      Finish(::grpc::Status(::grpc::INTERNAL, "internal error"));
    }

    flatbuffers::grpc::MessageBuilder builder;
    auto response_offset = proto::CreateGetSquareResponse(builder, input * input);
    builder.Finish(response_offset);
    Send(builder.ReleaseMessage<proto::GetSquareResponse>());
  }
};

struct GetEchoMethod {
  static constexpr const char* MethodName() {
    return "/async_grpc.proto.Math/GetEcho";
  }
  using IncomingType = proto::GetEchoRequest;
  using OutgoingType = proto::GetEchoResponse;
};

class GetEchoHandler : public RpcHandler<GetEchoMethod> {
 public:
  void OnRequest(flatbuffers::grpc::Message<proto::GetEchoRequest>& request) override {
    int value = request.GetRoot()->input();
    Writer writer = GetWriter();
    GetContext<MathServerContext>()->echo_responder.set_value(
        [writer, value]() {
          flatbuffers::grpc::MessageBuilder builder;
          auto response_offset = proto::CreateGetEchoResponse(builder, value);
          builder.Finish(response_offset);
          return writer.Write(builder.ReleaseMessage<proto::GetEchoResponse>());
        });
  }
};

struct GetSequenceMethod {
  static constexpr const char* MethodName() {
    return "/async_grpc.proto.Math/GetSequence";
  }
  using IncomingType = proto::GetSequenceRequest;
  using OutgoingType = Stream<proto::GetSequenceResponse>;
};

class GetSequenceHandler : public RpcHandler<GetSequenceMethod> {
 public:
  void OnRequest(flatbuffers::grpc::Message<proto::GetSequenceRequest>& request) override {
    for (int i = 0; i < request.GetRoot()->input(); ++i) {
      flatbuffers::grpc::MessageBuilder builder;
      auto response_offset = proto::CreateGetSequenceResponse(builder, i);
      builder.Finish(response_offset);
      Send(builder.ReleaseMessage<proto::GetSequenceResponse>());
    }

    Finish(::grpc::Status::OK);
  }
};

// TODO(cschuet): Due to the hard-coded part these tests will become flaky when
// run in parallel. It would be nice to find a way to solve that. gRPC also
// allows to communicate over UNIX domain sockets.
const std::string kServerAddress = "localhost:50051";
const std::size_t kNumThreads = 1;

class ServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Server::Builder server_builder;
    server_builder.SetServerAddress(kServerAddress);
    server_builder.SetNumGrpcThreads(kNumThreads);
    server_builder.SetNumEventThreads(kNumThreads);
    server_builder.RegisterHandler<GetSumHandler>();
    server_builder.RegisterHandler<GetSquareHandler>();
    server_builder.RegisterHandler<GetRunningSumHandler>();
    server_builder.RegisterHandler<GetEchoHandler>();
    server_builder.RegisterHandler<GetSequenceHandler>();
    server_ = server_builder.Build();

    client_channel_ = ::grpc::CreateChannel(
        kServerAddress, ::grpc::InsecureChannelCredentials());

    server_->SetExecutionContext(common::make_unique<MathServerContext>());
    server_->Start();
  }

  void TearDown() override {
    server_->Shutdown();
    CompletionQueuePool::Shutdown();
  }

  std::unique_ptr<Server> server_;
  std::shared_ptr<::grpc::Channel> client_channel_;
};

TEST_F(ServerTest, StartAndStopServerTest) {}

TEST_F(ServerTest, ProcessRpcStreamTest) {
  Client<GetSumMethod> client(client_channel_);
  for (int i = 0; i < 3; ++i) {
    flatbuffers::grpc::MessageBuilder builder;
    auto request_offset = proto::CreateGetSumRequest(builder, i);
    builder.Finish(request_offset);
    EXPECT_TRUE(client.Write(builder.ReleaseMessage<proto::GetSumRequest>()));
  }
  EXPECT_TRUE(client.StreamWritesDone());
  EXPECT_TRUE(client.StreamFinish().ok());
  EXPECT_EQ(client.response().output(), 33);
}

TEST_F(ServerTest, ProcessUnaryRpcTest) {
  Client<GetSquareMethod> client(client_channel_);
  flatbuffers::grpc::MessageBuilder builder;
  auto request_offset = proto::CreateGetSquareRequest(builder, 11);
  builder.Finish(request_offset);
  EXPECT_TRUE(client.Write(builder.ReleaseMessage<proto::GetSquareRequest>()));
  EXPECT_EQ(client.response().GetRoot()->output(), 121);
}

TEST_F(ServerTest, ProcessBidiStreamingRpcTest) {
  Client<GetRunningSumMethod> client(client_channel_);
  for (int i = 0; i < 3; ++i) {
    flatbuffers::grpc::MessageBuilder builder;
    auto request_offset = proto::CreateGetSumRequest(builder, i);
    builder.Finish(request_offset);
    EXPECT_TRUE(client.Write(builder.ReleaseMessage<proto::GetSumRequest>()));
  }
  client.StreamWritesDone();

  flatbuffers::grpc::Message<proto::GetSumResponse> response;
  std::list<int> expected_responses = {0, 0, 1, 1, 3, 3};
  while (client.StreamRead(&response)) {
    EXPECT_EQ(expected_responses.front(), response.GetRoot()->output());
    expected_responses.pop_front();
  }
  EXPECT_TRUE(expected_responses.empty());
  EXPECT_TRUE(client.StreamFinish().ok());
}

TEST_F(ServerTest, WriteFromOtherThread) {
  Server* server = server_.get();
  std::thread response_thread([server]() {
    std::future<EchoResponder> responder_future =
        server->GetContext<MathServerContext>()->echo_responder.get_future();
    responder_future.wait();
    auto responder = responder_future.get();
    CHECK(responder());
  });

  Client<GetEchoMethod> client(client_channel_);
  {
    flatbuffers::grpc::MessageBuilder builder;
    auto request_offset = proto::CreateGetEchoRequest(builder, 13);
    builder.Finish(request_offset);
    EXPECT_TRUE(client.Write(builder.ReleaseMessage<proto::GetEchoRequest>()));
  }
  response_thread.join();
  EXPECT_EQ(client.response().GetRoot()->output(), 13);
}

TEST_F(ServerTest, ProcessServerStreamingRpcTest) {
  Client<GetSequenceMethod> client(client_channel_);
  {
    flatbuffers::grpc::MessageBuilder builder;
    auto request_offset = proto::CreateGetSequenceRequest(builder, 12);
    builder.Finish(request_offset);
    client.Write(builder.ReleaseMessage<proto::GetSequenceRequest>());
  }

  for (int i = 0; i < 12; ++i) {
    flatbuffers::grpc::Message<proto::GetSequenceResponse> response;
    EXPECT_TRUE(client.StreamRead(&response));
    EXPECT_EQ(response.GetRoot()->output(), i);
  }

  flatbuffers::grpc::Message<proto::GetSequenceResponse> final_response;
  EXPECT_FALSE(client.StreamRead(&final_response));
  EXPECT_TRUE(client.StreamFinish().ok());
}

TEST_F(ServerTest, RetryWithUnrecoverableError) {
  Client<GetSquareMethod> client(
      client_channel_, common::FromSeconds(5),
      CreateUnlimitedConstantDelayStrategy(common::FromSeconds(1),
                                           {::grpc::INTERNAL}));

  flatbuffers::grpc::MessageBuilder builder;
  auto request_offset = proto::CreateGetSquareRequest(builder, -11);
  builder.Finish(request_offset);
  EXPECT_FALSE(client.Write(builder.ReleaseMessage<proto::GetSquareRequest>()));
}

TEST_F(ServerTest, AsyncClientUnary) {
  std::mutex m;
  std::condition_variable cv;
  bool done = false;

  AsyncClient<GetSquareMethod> async_client(
      client_channel_,
      [&done, &m, &cv](const ::grpc::Status& status,
                       const proto::GetSquareResponse* response) {
        EXPECT_TRUE(status.ok());
        EXPECT_EQ(response->output(), 121);
        {
          std::lock_guard<std::mutex> lock(m);
          done = true;
        }
        cv.notify_all();
      });

  flatbuffers::grpc::MessageBuilder builder;
  auto request_offset = proto::CreateGetSquareRequest(builder, 11);
  builder.Finish(request_offset);
  async_client.WriteAsync(builder.ReleaseMessage<proto::GetSquareRequest>());

  std::unique_lock<std::mutex> lock(m);
  cv.wait(lock, [&done] { return done; });
}

TEST_F(ServerTest, AsyncClientServerStreaming) {
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  int counter = 0;

  AsyncClient<GetSequenceMethod> async_client(
      client_channel_,
      [&done, &m, &cv, &counter](const ::grpc::Status& status,
                                 const proto::GetSequenceResponse* response) {
        LOG(INFO) << status.error_code() << " " << status.error_message();
        EXPECT_TRUE(status.ok());

        if (!response) {
          {
            std::lock_guard<std::mutex> lock(m);
            done = true;
          }
          cv.notify_all();
        } else {
          EXPECT_EQ(response->output(), counter++);
        }
      });

  flatbuffers::grpc::MessageBuilder builder;
  auto request_offset = proto::CreateGetSequenceRequest(builder, 10);
  builder.Finish(request_offset);
  auto request = builder.ReleaseMessage<proto::GetSequenceRequest>();
  async_client.WriteAsync(request);

  std::unique_lock<std::mutex> lock(m);
  LOG(INFO) << "Waiting for responses...";
  cv.wait(lock, [&done] { return done; });
}

}  // namespace
}  // namespace async_grpc
