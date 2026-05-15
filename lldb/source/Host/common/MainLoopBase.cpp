//===-- MainLoopBase.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/MainLoopBase.h"
#include <chrono>

using namespace lldb;
using namespace lldb_private;

bool MainLoopBase::AddCallback(const Callback &callback, TimePoint point) {
  bool interrupt_needed;
  bool interrupt_succeeded = true;
  {
    std::lock_guard<std::mutex> lock{m_callback_mutex};
    // We need to interrupt the main thread if this callback is scheduled to
    // execute at an earlier time than the earliest callback registered so far.
    interrupt_needed =
        m_callbacks.empty() || point < std::get<0>(m_callbacks.top());
    m_callbacks.emplace(point, m_callback_sequence++, callback);
  }
  if (interrupt_needed)
    interrupt_succeeded = Interrupt();
  return interrupt_succeeded;
}

void MainLoopBase::ProcessCallbacks() {
  while (true) {
    Callback callback;
    {
      std::lock_guard<std::mutex> lock{m_callback_mutex};
      if (m_callbacks.empty() ||
          std::chrono::steady_clock::now() < std::get<0>(m_callbacks.top()))
        return;
      // top() returns const-ref; get the callback out via const-cast on the
      // queue's underlying storage so we can move it.
      callback = std::move(
          const_cast<Callback &>(std::get<2>(m_callbacks.top())));
      m_callbacks.pop();
    }

    callback(*this);
  }
}

std::optional<MainLoopBase::TimePoint> MainLoopBase::GetNextWakeupTime() {
  std::lock_guard<std::mutex> lock(m_callback_mutex);
  if (m_callbacks.empty())
    return std::nullopt;
  return std::get<0>(m_callbacks.top());
}
