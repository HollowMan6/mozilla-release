/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NotificationCallback_h__
#define NotificationCallback_h__

#include <filesystem>
#include <tuple>
#include <unknwn.h>
#include <wrl.h>

#include "mozilla/Maybe.h"
#include "nsWindowsHelpers.h"

using namespace Microsoft::WRL;
using namespace std::filesystem;

// Windows 10+ declarations.
// TODO remove declarations and add `#include
// <notificationactivationcallback.h>` when Windows 10 is the minimum supported.
typedef struct NOTIFICATION_USER_INPUT_DATA {
  LPCWSTR Key;
  LPCWSTR Value;
} NOTIFICATION_USER_INPUT_DATA;

MIDL_INTERFACE("53E31837-6600-4A81-9395-75CFFE746F94")
INotificationActivationCallback : public IUnknown {
 public:
  virtual HRESULT STDMETHODCALLTYPE Activate(
      LPCWSTR appUserModelId, LPCWSTR invokedArgs,
      const NOTIFICATION_USER_INPUT_DATA* data, ULONG count) = 0;
};

struct ToastArgs {
  std::wstring profile;
  std::wstring windowsTag;
  std::wstring action;
};

class NotificationCallback final
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
                          INotificationActivationCallback> {
 public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) final;

  HRESULT STDMETHODCALLTYPE Activate(LPCWSTR appUserModelId,
                                     LPCWSTR invokedArgs,
                                     const NOTIFICATION_USER_INPUT_DATA* data,
                                     ULONG dataCount) final;

  explicit NotificationCallback(const GUID& runtimeGuid,
                                const path& dllInstallDir)
      : guid(runtimeGuid), installDir(dllInstallDir) {}

 private:
  const GUID guid = {};
  const path installDir = {};

  void HandleActivation(LPCWSTR invokedArgs);
  mozilla::Maybe<ToastArgs> ParseToastArguments(LPCWSTR invokedArgs);
  std::tuple<path, mozilla::UniquePtr<wchar_t[]>> BuildRunCommand(
      const ToastArgs& args);

  static mozilla::Maybe<nsAutoHandle> CreatePipe(const std::wstring& tag);
  static bool ConnectPipeWithTimeout(const nsAutoHandle& pipe);
  static void HandlePipeMessages(const nsAutoHandle& pipe);
  static DWORD TransferForegroundPermission(const DWORD pid);
};

#endif
