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

#include "libs/a71ch/a71ch.h"
#include "libs/base/led.h"
#include "third_party/freertos_kernel/include/FreeRTOS.h"
#include "third_party/freertos_kernel/include/task.h"

#include MBEDTLS_CONFIG_FILE

#include <cstdio>

#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/base64.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/ccm.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/chacha20.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/chachapoly.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/ctr_drbg.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/des.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/dhm.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/ecp.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/entropy.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/gcm.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/hmac_drbg.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/md5.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/pkcs5.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/poly1305.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/ripemd160.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/rsa.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/sha1.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/sha256.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/sha512.h"
#include "third_party/nxp/rt1176-sdk/middleware/mbedtls/include/mbedtls/x509.h"

namespace {
// From
// third_party/nxp/rt1176-sdk/boards/evkmimxrt1170/mbedtls_examples/mbedtls_selftest/cm7/selftest.c
typedef struct {
  const char *name;
  int (*function)(int /* verbose */);
} selftest_t;

const selftest_t selftests[] = {
    {"aes", mbedtls_aes_self_test},
    {"base64", mbedtls_base64_self_test},
    {"ccm", mbedtls_ccm_self_test},
    {"chacha20", mbedtls_chacha20_self_test},
    {"chacha20-poly1305", mbedtls_chachapoly_self_test},
    {"ctr_drbg", mbedtls_ctr_drbg_self_test},
    {"des", mbedtls_des_self_test},
    {"dhm", mbedtls_dhm_self_test},
    {"ecp", mbedtls_ecp_self_test},
    {"entropy", mbedtls_entropy_self_test},
    {"gcm", mbedtls_gcm_self_test},
    {"hmac_drbg", mbedtls_hmac_drbg_self_test},
    {"md5", mbedtls_md5_self_test},
    {"mpi", mbedtls_mpi_self_test},
    {"pkcs5", mbedtls_pkcs5_self_test},
    {"poly1305", mbedtls_poly1305_self_test},
    {"ripemd160", mbedtls_ripemd160_self_test},
    {"rsa", mbedtls_rsa_self_test},
    {"sha1", mbedtls_sha1_self_test},
    {"sha256", mbedtls_sha256_self_test},
    {"sha512", mbedtls_sha512_self_test},
    {"x509", mbedtls_x509_self_test},
    {nullptr, nullptr}};

}  // namespace

extern "C" void app_main(void *param) {
  (void)param;

  printf("Coral Micro mbedtls Example!\r\n");
  // Status LED turn on to shows board is on.
  LedSet(coralmicro::Led::kStatus, true);

  if (!coralmicro::A71ChInit()) {
    printf("A71CH init failed\r\n");
    vTaskSuspend(nullptr);
  }

  for (const selftest_t *test = selftests; test->name != nullptr; test++) {
    test->function(1);
  }
  vTaskSuspend(nullptr);
}