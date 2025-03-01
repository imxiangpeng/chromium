// Copyright 2017 The Crashpad Authors. All rights reserved.
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

#include "snapshot/linux/cpu_context_linux.h"

#include <stddef.h>
#include <string.h>

#include "base/logging.h"

namespace crashpad {
namespace internal {

#if defined(ARCH_CPU_X86_FAMILY)

void InitializeCPUContextX86(const ThreadContext::t32_t& thread_context,
                             const FloatContext::f32_t& float_context,
                             CPUContextX86* context) {
  context->eax = thread_context.eax;
  context->ebx = thread_context.ebx;
  context->ecx = thread_context.ecx;
  context->edx = thread_context.edx;
  context->edi = thread_context.edi;
  context->esi = thread_context.esi;
  context->ebp = thread_context.ebp;
  context->esp = thread_context.esp;
  context->eip = thread_context.eip;
  context->eflags = thread_context.eflags;
  context->cs = thread_context.xcs;
  context->ds = thread_context.xds;
  context->es = thread_context.xes;
  context->fs = thread_context.xfs;
  context->gs = thread_context.xgs;
  context->ss = thread_context.xss;

  static_assert(sizeof(context->fxsave) == sizeof(float_context.fxsave),
                "fxsave size mismatch");
  memcpy(&context->fxsave, &float_context.fxsave, sizeof(context->fxsave));

  // TODO(jperaza): debug registers
  context->dr0 = 0;
  context->dr1 = 0;
  context->dr2 = 0;
  context->dr3 = 0;
  context->dr4 = 0;
  context->dr5 = 0;
  context->dr6 = 0;
  context->dr7 = 0;

}

void InitializeCPUContextX86_64(const ThreadContext::t64_t& thread_context,
                                const FloatContext::f64_t& float_context,
                                CPUContextX86_64* context) {
  context->rax = thread_context.rax;
  context->rbx = thread_context.rbx;
  context->rcx = thread_context.rcx;
  context->rdx = thread_context.rdx;
  context->rdi = thread_context.rdi;
  context->rsi = thread_context.rsi;
  context->rbp = thread_context.rbp;
  context->rsp = thread_context.rsp;
  context->r8 = thread_context.r8;
  context->r9 = thread_context.r9;
  context->r10 = thread_context.r10;
  context->r11 = thread_context.r11;
  context->r12 = thread_context.r12;
  context->r13 = thread_context.r13;
  context->r14 = thread_context.r14;
  context->r15 = thread_context.r15;
  context->rip = thread_context.rip;
  context->rflags = thread_context.eflags;
  context->cs = thread_context.cs;
  context->fs = thread_context.fs;
  context->gs = thread_context.gs;

  static_assert(sizeof(context->fxsave) == sizeof(float_context.fxsave),
                "fxsave size mismatch");
  memcpy(&context->fxsave, &float_context.fxsave, sizeof(context->fxsave));

  // TODO(jperaza): debug registers.
  context->dr0 = 0;
  context->dr1 = 0;
  context->dr2 = 0;
  context->dr3 = 0;
  context->dr4 = 0;
  context->dr5 = 0;
  context->dr6 = 0;
  context->dr7 = 0;
}

#else
#error Port.
#endif  // ARCH_CPU_X86_FAMILY || DOXYGEN

}  // namespace internal
}  // namespace crashpad
