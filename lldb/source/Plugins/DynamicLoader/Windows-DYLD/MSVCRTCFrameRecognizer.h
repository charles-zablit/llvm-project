//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_DYNAMICLOADER_WINDOWS_MSVCRTCFRAMERECOGNIZER_H
#define LLDB_PLUGINS_DYNAMICLOADER_WINDOWS_MSVCRTCFRAMERECOGNIZER_H

#include "lldb/Target/StackFrameRecognizer.h"

namespace lldb_private {

class Target;

/// Registers the MSVC run-time check failure frame recognizer with the
/// supplied target. Safe to call more than once per target -- the second
/// call is a no-op because StackFrameRecognizerManager dedupes by symbol
/// name. Process-plugin agnostic: the recognizer fires when frame 0 is
/// `failwithmessage` regardless of whether the inferior is being driven
/// by ProcessWindows or ProcessGDBRemote.
void RegisterMSVCRTCFrameRecognizer(Target &target);

/// Recognized stack frame for an MSVC _RTC failure. Carries the human-readable
/// stop description extracted from failwithmessage's \c msg parameter.
class MSVCRTCRecognizedFrame : public RecognizedStackFrame {
public:
  MSVCRTCRecognizedFrame(std::string desc) { m_stop_desc = std::move(desc); }
};

/// Recognizes the MSVC CRT's \c failwithmessage frame, extracts the
/// run-time check failure message from the \c msg parameter, and returns it
/// as the thread stop description.
class MSVCRTCFrameRecognizer : public StackFrameRecognizer {
public:
  std::string GetName() override {
    return "MSVC Run-Time Check Failure Recognizer";
  }
  lldb::RecognizedStackFrameSP
  RecognizeFrame(lldb::StackFrameSP frame_sp) override;
};

} // namespace lldb_private

#endif // LLDB_PLUGINS_DYNAMICLOADER_WINDOWS_MSVCRTCFRAMERECOGNIZER_H
