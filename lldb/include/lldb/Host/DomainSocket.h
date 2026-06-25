//===-- DomainSocket.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_DOMAINSOCKET_H
#define LLDB_HOST_DOMAINSOCKET_H

#include "lldb/Host/common/DomainSocket.h"

#if defined(_WIN32)
#include "lldb/Host/windows/DomainSocketWindows.h"
#else
#include "lldb/Host/posix/DomainSocketPosix.h"
#endif

namespace lldb_private {

/// The DomainSocket implementation for the host platform. Use this to reach
/// the platform-specific factories (e.g. DomainSocketPlatform::CreatePair());
/// the cross-platform behavior lives in the DomainSocket base class.
#if defined(_WIN32)
using DomainSocketPlatform = DomainSocketWindows;
#else
using DomainSocketPlatform = DomainSocketPosix;
#endif

} // namespace lldb_private

#endif // LLDB_HOST_DOMAINSOCKET_H
