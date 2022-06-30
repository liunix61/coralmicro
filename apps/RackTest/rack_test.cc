// Copyright 2022 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <array>

#include "apps/RackTest/rack_test_ipc.h"
#include "libs/base/ipc_m7.h"
#include "libs/base/utils.h"
#include "libs/camera/camera.h"
#include "libs/coremark/core_portme.h"
#include "libs/rpc/rpc_http_server.h"
#include "libs/testlib/test_lib.h"
#include "third_party/freertos_kernel/include/FreeRTOS.h"
#include "third_party/freertos_kernel/include/task.h"

#if defined TEST_WIFI
#include "libs/base/wifi.h"
#endif

namespace {
constexpr char kMethodM4XOR[] = "m4_xor";
constexpr char kMethodM4CoreMark[] = "m4_coremark";
constexpr char kMethodM7CoreMark[] = "m7_coremark";
constexpr char kMethodGetFrame[] = "get_frame";

std::vector<uint8_t> camera_rgb;

void HandleAppMessage(
    const uint8_t data[coralmicro::ipc::kMessageBufferDataSize], void* param) {
  auto rpc_task_handle = reinterpret_cast<TaskHandle_t>(param);
  const auto* app_message = reinterpret_cast<const RackTestAppMessage*>(data);
  switch (app_message->message_type) {
    case RackTestAppMessageType::kXor: {
      xTaskNotify(rpc_task_handle, app_message->message.xor_value,
                  eSetValueWithOverwrite);
      break;
    }
    case RackTestAppMessageType::kCoreMark: {
      xTaskNotify(rpc_task_handle, 0, eSetValueWithOverwrite);
      break;
    }
    default:
      printf("Unknown message type\r\n");
  }
}

void M4XOR(struct jsonrpc_request* request) {
  std::string value_string;
  if (!coralmicro::testlib::JsonRpcGetStringParam(request, "value",
                                                  &value_string))
    return;

  if (!coralmicro::IPCM7::GetSingleton()->M4IsAlive(1000 /*ms*/)) {
    jsonrpc_return_error(request, -1, "M4 has not been started", nullptr);
    return;
  }

  auto value =
      reinterpret_cast<uint32_t>(strtoul(value_string.c_str(), nullptr, 10));
  coralmicro::ipc::Message msg{};
  msg.type = coralmicro::ipc::MessageType::kApp;
  auto* app_message = reinterpret_cast<RackTestAppMessage*>(&msg.message.data);
  app_message->message_type = RackTestAppMessageType::kXor;
  app_message->message.xor_value = value;
  coralmicro::IPCM7::GetSingleton()->SendMessage(msg);

  // hang out here and wait for an event.
  uint32_t xor_value;
  if (xTaskNotifyWait(0, 0, &xor_value, pdMS_TO_TICKS(1000)) != pdTRUE) {
    jsonrpc_return_error(request, -1, "Timed out waiting for response from M4",
                         nullptr);
    return;
  }

  jsonrpc_return_success(request, "{%Q:%lu}", "value", xor_value);
}

void M4CoreMark(struct jsonrpc_request* request) {
  auto* ipc = coralmicro::IPCM7::GetSingleton();
  if (!ipc->M4IsAlive(1000 /*ms*/)) {
    jsonrpc_return_error(request, -1, "M4 has not been started", nullptr);
    return;
  }

  char coremark_buffer[MAX_COREMARK_BUFFER];
  coralmicro::ipc::Message msg{};
  msg.type = coralmicro::ipc::MessageType::kApp;
  auto* app_message = reinterpret_cast<RackTestAppMessage*>(&msg.message.data);
  app_message->message_type = RackTestAppMessageType::kCoreMark;
  app_message->message.buffer_ptr = coremark_buffer;
  coralmicro::IPCM7::GetSingleton()->SendMessage(msg);

  if (xTaskNotifyWait(0, 0, nullptr, pdMS_TO_TICKS(30000)) != pdTRUE) {
    jsonrpc_return_error(request, -1, "Timed out waiting for response from M4",
                         nullptr);
    return;
  }

  jsonrpc_return_success(request, "{%Q:%Q}", "coremark_results",
                         coremark_buffer);
}

void M7CoreMark(struct jsonrpc_request* request) {
  char coremark_buffer[MAX_COREMARK_BUFFER];
  RunCoreMark(coremark_buffer);
  jsonrpc_return_success(request, "{%Q:%Q}", "coremark_results",
                         coremark_buffer);
}

void GetFrame(struct jsonrpc_request* request) {
  int rpc_width, rpc_height;
  std::string rpc_format;
  bool rpc_width_valid =
      coralmicro::testlib::JsonRpcGetIntegerParam(request, "width", &rpc_width);
  bool rpc_height_valid = coralmicro::testlib::JsonRpcGetIntegerParam(
      request, "height", &rpc_height);
  bool rpc_format_valid = coralmicro::testlib::JsonRpcGetStringParam(
      request, "format", &rpc_format);

  int width = rpc_width_valid ? rpc_width : coralmicro::CameraTask::kWidth;
  int height = rpc_height_valid ? rpc_height : coralmicro::CameraTask::kHeight;
  coralmicro::camera::Format format = coralmicro::camera::Format::kRgb;

  if (rpc_format_valid) {
    constexpr char kFormatRGB[] = "RGB";
    constexpr char kFormatGrayscale[] = "L";
    if (memcmp(rpc_format.c_str(), kFormatRGB,
               std::min(rpc_format.length(), strlen(kFormatRGB))) == 0) {
      format = coralmicro::camera::Format::kRgb;
    }
    if (memcmp(rpc_format.c_str(), kFormatGrayscale,
               std::min(rpc_format.length(), strlen(kFormatGrayscale))) == 0) {
      format = coralmicro::camera::Format::kY8;
    }
  }

  camera_rgb.resize(width * height *
                    coralmicro::CameraTask::FormatToBPP(format));

  coralmicro::CameraTask::GetSingleton()->SetPower(true);
  coralmicro::camera::TestPattern pattern =
      coralmicro::camera::TestPattern::kColorBar;
  coralmicro::CameraTask::GetSingleton()->SetTestPattern(pattern);
  coralmicro::CameraTask::GetSingleton()->Enable(
      coralmicro::camera::Mode::kStreaming);
  coralmicro::camera::FrameFormat fmt_rgb{};

  fmt_rgb.fmt = format;
  fmt_rgb.filter = coralmicro::camera::FilterMethod::kBilinear;
  fmt_rgb.width = width;
  fmt_rgb.height = height;
  fmt_rgb.preserve_ratio = false;
  fmt_rgb.buffer = camera_rgb.data();

  bool success = coralmicro::CameraTask::GetFrame({fmt_rgb});
  coralmicro::CameraTask::GetSingleton()->SetPower(false);

  if (success)
    jsonrpc_return_success(request, "{}");
  else
    jsonrpc_return_error(request, -1, "Call to GetFrame returned false.",
                         nullptr);
}

coralmicro::HttpServer::Content UriHandler(const char* name) {
  if (std::strcmp("/camera.rgb", name) == 0)
    return coralmicro::HttpServer::Content{std::move(camera_rgb)};
  return {};
}
}  // namespace

extern "C" void app_main(void* param) {
  coralmicro::IPCM7::GetSingleton()->RegisterAppMessageHandler(
      HandleAppMessage, xTaskGetHandle(TCPIP_THREAD_NAME));
  jsonrpc_init(nullptr, nullptr);
#if defined(TEST_WIFI)
  if (!coralmicro::TurnOnWiFi()) {
    printf("Wi-Fi failed to come up (is the Wi-Fi board attached?)\r\n");
    vTaskSuspend(nullptr);
  }
  jsonrpc_export(coralmicro::testlib::kMethodWiFiSetAntenna,
                 coralmicro::testlib::WiFiSetAntenna);
  jsonrpc_export(coralmicro::testlib::kMethodWiFiScan,
                 coralmicro::testlib::WiFiScan);
  jsonrpc_export(coralmicro::testlib::kMethodWiFiConnect,
                 coralmicro::testlib::WiFiConnect);
  jsonrpc_export(coralmicro::testlib::kMethodWiFiDisconnect,
                 coralmicro::testlib::WiFiDisconnect);
  jsonrpc_export(coralmicro::testlib::kMethodWiFiGetIp,
                 coralmicro::testlib::WiFiGetIp);
  jsonrpc_export(coralmicro::testlib::kMethodWiFiGetStatus,
                 coralmicro::testlib::WiFiGetStatus);
#endif
  jsonrpc_export(coralmicro::testlib::kMethodGetSerialNumber,
                 coralmicro::testlib::GetSerialNumber);
  jsonrpc_export(coralmicro::testlib::kMethodRunTestConv1,
                 coralmicro::testlib::RunTestConv1);
  jsonrpc_export(coralmicro::testlib::kMethodSetTPUPowerState,
                 coralmicro::testlib::SetTPUPowerState);
  jsonrpc_export(coralmicro::testlib::kMethodPosenetStressRun,
                 coralmicro::testlib::PosenetStressRun);
  jsonrpc_export(coralmicro::testlib::kMethodBeginUploadResource,
                 coralmicro::testlib::BeginUploadResource);
  jsonrpc_export(coralmicro::testlib::kMethodUploadResourceChunk,
                 coralmicro::testlib::UploadResourceChunk);
  jsonrpc_export(coralmicro::testlib::kMethodDeleteResource,
                 coralmicro::testlib::DeleteResource);
  jsonrpc_export(coralmicro::testlib::kMethodFetchResource,
                 coralmicro::testlib::FetchResource);
  jsonrpc_export(coralmicro::testlib::kMethodRunClassificationModel,
                 coralmicro::testlib::RunClassificationModel);
  jsonrpc_export(coralmicro::testlib::kMethodRunDetectionModel,
                 coralmicro::testlib::RunDetectionModel);
  jsonrpc_export(coralmicro::testlib::kMethodRunSegmentationModel,
                 coralmicro::testlib::RunSegmentationModel);
  jsonrpc_export(coralmicro::testlib::kMethodStartM4,
                 coralmicro::testlib::StartM4);
  jsonrpc_export(coralmicro::testlib::kMethodGetTemperature,
                 coralmicro::testlib::GetTemperature);
  jsonrpc_export(kMethodM4XOR, M4XOR);
  jsonrpc_export(coralmicro::testlib::kMethodCaptureTestPattern,
                 coralmicro::testlib::CaptureTestPattern);
  jsonrpc_export(kMethodM4CoreMark, M4CoreMark);
  jsonrpc_export(kMethodM7CoreMark, M7CoreMark);
  jsonrpc_export(kMethodGetFrame, GetFrame);
  jsonrpc_export(coralmicro::testlib::kMethodCaptureAudio,
                 coralmicro::testlib::CaptureAudio);
  jsonrpc_export(coralmicro::testlib::kMethodInitCrypto,
                 coralmicro::testlib::CryptoInit);
  jsonrpc_export(coralmicro::testlib::kMethodGetCryptoUId,
                 coralmicro::testlib::CryptoGetUID);

  coralmicro::JsonRpcHttpServer server;
  server.AddUriHandler(UriHandler);
  coralmicro::UseHttpServer(&server);
  vTaskSuspend(nullptr);
}
