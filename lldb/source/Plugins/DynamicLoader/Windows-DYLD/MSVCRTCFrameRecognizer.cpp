//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MSVCRTCFrameRecognizer.h"

#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrameRecognizer.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/ValueObject/ValueObject.h"

using namespace lldb;
using namespace lldb_private;

namespace lldb_private {

void RegisterMSVCRTCFrameRecognizer(Target &target) {
  target.GetFrameRecognizerManager().AddRecognizer(
      std::make_shared<MSVCRTCFrameRecognizer>(), ConstString(""),
      {ConstString("failwithmessage")}, Mangled::ePreferDemangled,
      /*first_instruction_only=*/false);
}

lldb::RecognizedStackFrameSP
MSVCRTCFrameRecognizer::RecognizeFrame(lldb::StackFrameSP frame_sp) {
  // failwithmessage calls __debugbreak() which lands at frame 0.
  if (frame_sp->GetFrameIndex() != 0)
    return RecognizedStackFrameSP();

  // We trust the function-name match the recognizer was registered against:
  // failwithmessage exists in MSVC CRT specifically to call __debugbreak()
  // for run-time check failures, so being stopped at frame 0 of it means
  // we're at one. Earlier revisions also gated on EXCEPTION_BREAKPOINT
  // via a ProcessWindows-specific accessor; that gate doesn't translate
  // to the lldb-server (ProcessGDBRemote) path, and dropping it here lets
  // the recognizer fire on both process plugins. False positives would
  // require user code to be paused at frame 0 of failwithmessage outside
  // an actual RTC failure, which doesn't happen in normal flow.

  const char *fn_name = frame_sp->GetFunctionName();
  if (!fn_name)
    return RecognizedStackFrameSP();
  if (!llvm::StringRef(fn_name).contains("failwithmessage"))
    return RecognizedStackFrameSP();

  VariableListSP vars = frame_sp->GetInScopeVariableList(false);
  if (!vars)
    return RecognizedStackFrameSP();

  for (size_t i = 0; i < vars->GetSize(); ++i) {
    VariableSP var = vars->GetVariableAtIndex(i);
    if (!var || var->GetName() != ConstString("msg"))
      continue;

    ValueObjectSP val =
        frame_sp->GetValueObjectForFrameVariable(var, eNoDynamicValues);
    if (!val)
      break;

    uint64_t ptr = val->GetValueAsUnsigned(0);
    if (!ptr)
      break;

    std::string msg;
    Status err;
    frame_sp->GetThread()->GetProcess()->ReadCStringFromMemory(ptr, msg, err);
    if (err.Success() && !msg.empty())
      return lldb::RecognizedStackFrameSP(
          new MSVCRTCRecognizedFrame("Run-time check failure: " + msg));
    break;
  }

  return RecognizedStackFrameSP();
}

} // namespace lldb_private
