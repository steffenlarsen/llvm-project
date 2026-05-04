//===--- Monitor.cpp - Request server monitoring information through CLI --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MonitoringService.grpc.pb.h"
#include "MonitoringService.pb.h"

#include "support/Logger.h"
#include "clang/Basic/Version.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/Signals.h"

#include <chrono>
#include <google/protobuf/util/json_util.h>
#include <grpcpp/grpcpp.h>

namespace clang {
namespace clangd {
namespace remote {
namespace {

static constexpr char Overview[] = R"(
This tool requests monitoring information (uptime, index freshness) from the
server and prints it to stdout.
)";

std::string ServerAddress;

namespace clv2 = llvm::clv2;

inline constexpr clv2::OptionInfo<std::string> ServerAddressOpt{
    "", "Address of the invoked server.", clv2::Positional{}, clv2::Required};

inline constexpr clv2::OptionsRegistry<&ServerAddressOpt> MonitorReg;

} // namespace
} // namespace remote
} // namespace clangd
} // namespace clang

int main(int argc, char *argv[]) {
  using namespace clang::clangd::remote;
  llvm::clv2::OptionParser P;
  P.add<&MonitorReg>();
  llvm::RegisterAllLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, Overview);
  if (!OptsCtx)
    return 1;
  if (const auto *O = OptsCtx->getViewPtr<&MonitorReg>())
    ServerAddress = O->get<&ServerAddressOpt>();
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);

  const auto Channel =
      grpc::CreateChannel(ServerAddress, grpc::InsecureChannelCredentials());
  const auto Stub = clang::clangd::remote::v1::Monitor::NewStub(Channel);
  grpc::ClientContext Context;
  Context.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::seconds(10));
  Context.AddMetadata("version", clang::getClangToolFullVersion("clangd"));
  const clang::clangd::remote::v1::MonitoringInfoRequest Request;
  clang::clangd::remote::v1::MonitoringInfoReply Response;
  const auto Status = Stub->MonitoringInfo(&Context, Request, &Response);
  if (!Status.ok()) {
    clang::clangd::elog("Can not request monitoring information ({0}): {1}\n",
                        Status.error_code(), Status.error_message());
    return -1;
  }
  std::string Output;
  google::protobuf::util::JsonPrintOptions Options;
  Options.add_whitespace = true;
  Options.always_print_primitive_fields = true;
  Options.preserve_proto_field_names = true;
  const auto JsonStatus =
      google::protobuf::util::MessageToJsonString(Response, &Output, Options);
  if (!JsonStatus.ok()) {
    clang::clangd::elog("Can not convert response ({0}) to JSON ({1}): {2}\n",
                        Response.DebugString(),
                        static_cast<int>(JsonStatus.code()),
                        JsonStatus.message().as_string());
    return -1;
  }
  llvm::outs() << Output;
}
