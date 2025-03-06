//===- DependencyScanningService.cpp - clang-scan-deps service ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/DependencyScanning/DependencyScanningService.h"
#include "llvm/CAS/ActionCache.h"
#include "llvm/CAS/CachingOnDiskFileSystem.h"
#include "llvm/CAS/ObjectStore.h"

using namespace clang;
using namespace tooling;
using namespace dependencies;

DependencyScanningService::DependencyScanningService(
    ScanningMode Mode, ScanningOutputFormat Format, CASOptions CASOpts,
    std::shared_ptr<llvm::cas::ObjectStore> CAS,
    std::shared_ptr<llvm::cas::ActionCache> Cache,
    IntrusiveRefCntPtr<llvm::cas::CachingOnDiskFileSystem> SharedFS,
    ScanningOptimizations OptimizeArgs, bool EagerLoadModules, bool TraceVFS)
    : Mode(Mode), Format(Format), CASOpts(std::move(CASOpts)), CAS(std::move(CAS)), Cache(std::move(Cache)),
      OptimizeArgs(OptimizeArgs), EagerLoadModules(EagerLoadModules), TraceVFS(TraceVFS),
      SharedFS(std::move(SharedFS)) {
  if (!this->SharedFS)
    SharedCache.emplace();
}
