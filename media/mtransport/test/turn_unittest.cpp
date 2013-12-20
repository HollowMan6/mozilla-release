/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

// Some code copied from nICEr. License is:
/*
Copyright (c) 2007, Adobe Systems, Incorporated
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

* Neither the name of Adobe Systems, Network Resonance nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdlib.h>
#include <iostream>

#include "sigslot.h"

#include "logging.h"
#include "nspr.h"
#include "nss.h"
#include "ssl.h"

#include "mozilla/Scoped.h"
#include "nsThreadUtils.h"
#include "nsXPCOM.h"

#include "mtransport_test_utils.h"
#include "runnable_utils.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

// nICEr includes
extern "C" {
#include "nr_api.h"
#include "registry.h"
#include "async_timer.h"
#include "r_crc32.h"
#include "ice_util.h"
#include "transport_addr.h"
#include "nr_crypto.h"
#include "nr_socket.h"
#include "nr_socket_local.h"
#include "nr_socket_buffered_stun.h"
#include "stun_client_ctx.h"
#include "turn_client_ctx.h"
}

#include "nricemediastream.h"
#include "nricectx.h"


MtransportTestUtils *test_utils;

using namespace mozilla;

std::string g_turn_server;
std::string g_turn_user;
std::string g_turn_password;

std::string kDummyTurnServer("192.0.2.1");  // From RFC 5737

class TurnClient : public ::testing::Test {
 public:
  TurnClient()
      : turn_server_(g_turn_server),
        real_socket_(nullptr),
        net_socket_(nullptr),
        buffered_socket_(nullptr),
        net_fd_(nullptr),
        turn_ctx_(nullptr),
        allocated_(false),
        received_(0),
        protocol_(IPPROTO_UDP) {
  }

  ~TurnClient() {
  }

  void SetTcp() {
    protocol_ = IPPROTO_TCP;
  }

  void Init_s() {
    int r;
    nr_transport_addr addr;
    r = nr_ip4_port_to_transport_addr(0, 0, protocol_, &addr);
    ASSERT_EQ(0, r);

    r = nr_socket_local_create(&addr, &real_socket_);
    ASSERT_EQ(0, r);

    if (protocol_ == IPPROTO_TCP) {
      int r =
          nr_socket_buffered_stun_create(real_socket_, 100000,
                                         &buffered_socket_);
      ASSERT_EQ(0, r);
      net_socket_ = buffered_socket_;
    } else {
      net_socket_ = real_socket_;
    }

    r = nr_ip4_str_port_to_transport_addr(turn_server_.c_str(), 3478,
      protocol_, &addr);
    ASSERT_EQ(0, r);

    std::vector<unsigned char> password_vec(
        g_turn_password.begin(), g_turn_password.end());
    Data password;
    INIT_DATA(password, &password_vec[0], password_vec.size());
    r = nr_turn_client_ctx_create("test", net_socket_,
                                  g_turn_user.c_str(),
                                  &password,
                                  &addr, &turn_ctx_);
    ASSERT_EQ(0, r);

    r = nr_socket_getfd(net_socket_, &net_fd_);
    ASSERT_EQ(0, r);

    NR_ASYNC_WAIT(net_fd_, NR_ASYNC_WAIT_READ, socket_readable_cb,
        (void *)this);
  }

  void TearDown_s() {
    nr_turn_client_ctx_destroy(&turn_ctx_);
    if (net_fd_) {
      NR_ASYNC_CANCEL(net_fd_, NR_ASYNC_WAIT_READ);
    }

    nr_socket_destroy(&buffered_socket_);
  }

  void TearDown() {
    RUN_ON_THREAD(test_utils->sts_target(),
                  WrapRunnable(this, &TurnClient::TearDown_s),
                  NS_DISPATCH_SYNC);
  }

  void Allocate_s() {
    Init_s();
    ASSERT_TRUE(turn_ctx_);

    int r = nr_turn_client_allocate(turn_ctx_,
                                    allocate_success_cb,
                                    this);
    ASSERT_EQ(0, r);
  }

  void Allocate(bool expect_success=true) {
    RUN_ON_THREAD(test_utils->sts_target(),
                  WrapRunnable(this, &TurnClient::Allocate_s),
                  NS_DISPATCH_SYNC);

    if (expect_success) {
      ASSERT_TRUE_WAIT(allocated_, 5000);
    }
    else {
      PR_Sleep(10000);
      ASSERT_FALSE(allocated_);
    }
  }

  void Allocated() {
    if (turn_ctx_->state!=NR_TURN_CLIENT_STATE_ALLOCATED) {
      std::cerr << "Allocation failed" << std::endl;
      return;
    }
    allocated_ = true;

    int r;
    nr_transport_addr addr;

    r = nr_turn_client_get_relayed_address(turn_ctx_, &addr);
    ASSERT_EQ(0, r);

    relay_addr_ = addr.as_string;

    std::cerr << "Allocation succeeded with addr=" << relay_addr_ << std::endl;
  }

  void Readable(NR_SOCKET s, int how, void *arg) {
    // Re-arm
    std::cerr << "Socket is readable" << std::endl;
    NR_ASYNC_WAIT(s, how, socket_readable_cb, arg);

    UCHAR buf[8192];
    size_t len_s;
    nr_transport_addr addr;

    int r = nr_socket_recvfrom(net_socket_, buf, sizeof(buf), &len_s, 0, &addr);
    if (r) {
      std::cerr << "Error reading from socket" << std::endl;
      return;
    }

    ASSERT_LT(len_s, (size_t)INT_MAX);
    int len = (int)len_s;

    if (nr_is_stun_response_message(buf, len)) {
      std::cerr << "STUN response" << std::endl;
      r = nr_turn_client_process_response(turn_ctx_, buf, len, &addr);

      if (r && r != R_REJECTED && r != R_RETRY) {
        std::cerr << "Error processing STUN: " << r << std::endl;
      }
    } else if (nr_is_stun_indication_message(buf, len)) {
      std::cerr << "STUN indication" << std::endl;

      /* Process the indication */
      unsigned char data[NR_STUN_MAX_MESSAGE_SIZE];
      size_t datal;
      nr_transport_addr remote_addr;

      r = nr_turn_client_parse_data_indication(turn_ctx_, &addr,
                                               buf, len,
                                               data, &datal, sizeof(data),
                                               &remote_addr);
      ASSERT_EQ(0, r);
      std::cerr << "Received " << datal << " bytes from "
                << remote_addr.as_string << std::endl;

      received_ += datal;

      for (size_t i=0; i < datal; i++) {
        ASSERT_EQ(i & 0xff, data[i]);
      }
    }
    else {
      if (nr_is_stun_message(buf, len)) {
        std::cerr << "STUN message of unexpected type" << std::endl;
      } else {
        std::cerr << "Not a STUN message" << std::endl;
      }
      return;
    }
  }

  void SendTo_s(const std::string& target) {
    nr_transport_addr addr;
    int r;

    // Expected pattern here is "IP4:127.0.0.1:3487"
    ASSERT_EQ(0, target.compare(0, 4, "IP4:"));

    size_t offset = target.rfind(':');
    ASSERT_NE(offset, std::string::npos);

    std::string host = target.substr(4, offset - 4);
    std::string port = target.substr(offset + 1);

    r = nr_ip4_str_port_to_transport_addr(host.c_str(),
                                          atoi(port.c_str()),
                                          IPPROTO_UDP,
                                          &addr);
    ASSERT_EQ(0, r);

    unsigned char test[100];
    for (size_t i=0; i<sizeof(test); i++) {
      test[i] = i & 0xff;
    }

    r = nr_turn_client_send_indication(turn_ctx_,
                                            test, sizeof(test), 0,
                                            &addr);
    ASSERT_EQ(0, r);
  }

  void SendTo(const std::string& target) {
    RUN_ON_THREAD(test_utils->sts_target(),
                  WrapRunnable(this, &TurnClient::SendTo_s, target),
                  NS_DISPATCH_SYNC);
  }

  int received() const { return received_; }

  static void socket_readable_cb(NR_SOCKET s, int how, void *arg) {
    static_cast<TurnClient *>(arg)->Readable(s, how, arg);
  }

  static void allocate_success_cb(NR_SOCKET s, int how, void *arg){
    static_cast<TurnClient *>(arg)->Allocated();
  }

 protected:
  std::string turn_server_;
  nr_socket *real_socket_;
  nr_socket *net_socket_;
  nr_socket *buffered_socket_;
  NR_SOCKET net_fd_;
  nr_turn_client_ctx *turn_ctx_;
  std::string relay_addr_;
  bool allocated_;
  int received_;
  int protocol_;
};

TEST_F(TurnClient, Allocate) {
  Allocate();
}

TEST_F(TurnClient, AllocateTcp) {
  SetTcp();
  Allocate();
}

TEST_F(TurnClient, AllocateAndHold) {
  Allocate();
  PR_Sleep(20000);
}

TEST_F(TurnClient, SendToSelf) {
  Allocate();
  SendTo(relay_addr_);
  ASSERT_TRUE_WAIT(received() == 100, 1000);
  PR_Sleep(10000); // Wait 10 seconds to make sure the
                   // CreatePermission has time to complete/fail.
  SendTo(relay_addr_);
  ASSERT_TRUE_WAIT(received() == 200, 1000);
}


TEST_F(TurnClient, SendToSelfTcp) {
  SetTcp();
  Allocate();
  SendTo(relay_addr_);
  ASSERT_TRUE_WAIT(received() == 100, 1000);
  PR_Sleep(10000); // Wait 10 seconds to make sure the
                   // CreatePermission has time to complete/fail.
  SendTo(relay_addr_);
  ASSERT_TRUE_WAIT(received() == 200, 1000);
}

TEST_F(TurnClient, AllocateDummyServer) {
  turn_server_ = kDummyTurnServer;
  Allocate(false);
}

static std::string get_environment(const char *name) {
  char *value = getenv(name);

  if (!value)
    return "";

  return value;
}

int main(int argc, char **argv)
{
  g_turn_server = get_environment("TURN_SERVER_ADDRESS");
  g_turn_user = get_environment("TURN_SERVER_USER");
  g_turn_password = get_environment("TURN_SERVER_PASSWORD");

  if (g_turn_server.empty() ||
      g_turn_user.empty(),
      g_turn_password.empty()) {
    printf(
        "Set TURN_SERVER_ADDRESS, TURN_SERVER_USER, and TURN_SERVER_PASSWORD\n"
        "environment variables to run this test\n");
    return 0;
  }
  {
    nr_transport_addr addr;
    if (nr_ip4_str_port_to_transport_addr(g_turn_server.c_str(), 3478,
                                          IPPROTO_UDP, &addr)) {
      printf("Invalid TURN_SERVER_ADDRESS \"%s\". Only IP numbers supported.\n",
             g_turn_server.c_str());
      return 0;
    }
  }
  test_utils = new MtransportTestUtils();
  NSS_NoDB_Init(nullptr);
  NSS_SetDomesticPolicy();

  // Set up the ICE registry, etc.
  // TODO(ekr@rtfm.com): Clean up
  std::string dummy("dummy");
  RUN_ON_THREAD(test_utils->sts_target(),
                WrapRunnableNM(&NrIceCtx::Create,
                               dummy, false, false),
                NS_DISPATCH_SYNC);

  // Start the tests
  ::testing::InitGoogleTest(&argc, argv);

  int rv = RUN_ALL_TESTS();
  delete test_utils;
  return rv;
}
