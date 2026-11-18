/*
 * Common FPS for PS5
 * Copyright (C) 2026 porhe911
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The control flow is reconstructed from the hardware-proven v0.28b
 * injector.  Ptrace and payload-args primitives come from the pinned GPL
 * ps5-payload-dev/shsrv v0.20 source dependency.
 */

#include "stable_shellui_injector.hpp"
#include "remote_call.hpp"
#include "stable_elfldr_preflight.h"

#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <unistd.h>

extern "C" {
#include <ps5/kernel.h>
#include <ps5/nid.h>
#include "pt.h"

intptr_t commonfps_stable_elfldr_load(pid_t pid, std::uint8_t* elf);
intptr_t commonfps_stable_elfldr_payload_args(pid_t pid);
int commonfps_stable_trace_continue_seen(void);
int commonfps_stable_trace_stop_seen(void);
unsigned commonfps_stable_import_resolved_count(void);
unsigned commonfps_stable_import_unresolved_count(void);
const char* commonfps_stable_first_unresolved(void);
}

namespace common_fps::ps5 {
namespace {

constexpr std::uint64_t kPtraceAuthId = 0x4800000000010003ULL;

using RemotePthreadCreate = int (*)(
    std::uintptr_t*,
    const void*,
    void* (*)(void*),
    void*);

struct RemoteBootstrapFunctions {
    void* debug_out = nullptr;
    void* elf_main = nullptr;
    void* payload_args = nullptr;
    RemotePthreadCreate pthread_create_fn = nullptr;
    int pthread_create_rc = -777;
    std::uint32_t start_thread = 1;
};

static_assert(offsetof(RemoteBootstrapFunctions, elf_main) == 0x08);
static_assert(offsetof(RemoteBootstrapFunctions, payload_args) == 0x10);
static_assert(offsetof(RemoteBootstrapFunctions, pthread_create_fn) == 0x18);
static_assert(offsetof(RemoteBootstrapFunctions, pthread_create_rc) == 0x20);
static_assert(offsetof(RemoteBootstrapFunctions, start_thread) == 0x24);
static_assert(sizeof(RemoteBootstrapFunctions) == 0x28);

extern "C" __attribute__((used, noinline, section(".commonfps_stager.1")))
int commonfps_stable_stager(RemoteBootstrapFunctions* functions) {
    if (functions->start_thread != 0) {
        std::uintptr_t thread = 0;
        functions->pthread_create_rc = functions->pthread_create_fn(
            &thread,
            nullptr,
            reinterpret_cast<void* (*)(void*)>(functions->elf_main),
            functions->payload_args);
    } else {
        /*
         * PARITY TEST25 still exercises the exact target-stack stager call and
         * INT3/register-restore cycle, but this branch performs no remote
         * pthread_create and never enters the mapped renderer.
         */
        functions->pthread_create_rc = kPthreadCreateSkippedRc;
    }

    asm volatile("int3");
    return 0;
}

extern "C" __attribute__((used, noinline, section(".commonfps_stager.2")))
int commonfps_stable_stager_end() {
    return 0;
}

std::size_t stager_size() noexcept {
    const auto begin =
        reinterpret_cast<std::uintptr_t>(&commonfps_stable_stager);
    const auto end =
        reinterpret_cast<std::uintptr_t>(&commonfps_stable_stager_end);
    return end > begin ? static_cast<std::size_t>(end - begin) : 0;
}

std::uintptr_t resolve_remote(pid_t pid, const char* symbol) noexcept {
    char nid[12]{};
    if (!nid_encode(symbol, nid))
        return 0;
    return static_cast<std::uintptr_t>(pt_resolve(pid, nid));
}

// PT_ATTACH has stopped the tracee; SSTOP is expected here, unlike the
// controller's live-process check. Fail closed on incomplete enumeration.
bool identity_matches(ShellProcessIdentity target) noexcept {
    if (target.pid <= 1 || target.started <= 0) return false;
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    std::size_t length = 0;
    if (sysctl(mib, 4, nullptr, &length, nullptr, 0) || !length || length > 16*1024*1024) return false;
    auto* bytes = static_cast<char*>(std::malloc(length));
    if (!bytes) return false;
    bool valid = sysctl(mib, 4, bytes, &length, nullptr, 0) == 0 && length != 0;
    bool found = false;
    for (std::size_t i = 0; valid && i < length;) {
        if (length-i < sizeof(kinfo_proc)) { valid = false; break; }
        const auto* p = reinterpret_cast<const kinfo_proc*>(bytes+i);
        if (p->ki_structsize < static_cast<int>(sizeof(kinfo_proc)) ||
            static_cast<std::size_t>(p->ki_structsize) > length-i) { valid = false; break; }
        if (p->ki_pid == target.pid) {
            const long long started = static_cast<long long>(p->ki_start.tv_sec)*1000000 + p->ki_start.tv_usec;
            const bool named = std::strncmp(p->ki_comm, "SceShellUI", sizeof(p->ki_comm)) == 0 ||
                               std::strncmp(p->ki_comm, "SceShellUI.elf", sizeof(p->ki_comm)) == 0;
            if (found || !named || started != target.started || p->ki_stat == SZOMB || (p->ki_flag & P_WEXIT)) valid = false;
            found = true;
        }
        i += static_cast<std::size_t>(p->ki_structsize);
    }
    std::free(bytes);
    return valid && found;
}

} // namespace

bool shellui_identity_matches_attached(ShellProcessIdentity target) noexcept {
    return identity_matches(target);
}

static StableInjectionResult inject_impl(
    pid_t shellui_pid, const ShellProcessIdentity* identity,
    const std::uint8_t* renderer_elf,
    std::size_t renderer_size,
    StableInjectionMode mode) noexcept {

    StableInjectionResult result{};
    result.thread_start_requested =
        mode == StableInjectionMode::StartRendererThread;
    if (shellui_pid <= 0 || !renderer_elf || renderer_size < 64)
        return result;

    const pid_t self = getpid();
    const std::uint64_t original_auth = kernel_get_ucred_authid(self);
    if (original_auth == 0)
        return result;

    if (kernel_set_ucred_authid(self, kPtraceAuthId) != 0)
        return result;
    result.auth_changed = true;

    if (pt_attach(shellui_pid) == 0) {
        result.attached = true;

        do {
            if (identity && !identity_matches(*identity)) break;
            result.identity_verified = identity != nullptr;
            commonfps_preflight_result preflight{};
            result.preflight_status = commonfps_stable_elfldr_preflight(
                shellui_pid, renderer_elf, renderer_size, &preflight);
            result.preflight_resolved = preflight.resolved;
            result.preflight_unresolved = preflight.unresolved;
            std::memcpy(result.preflight_first_unresolved, preflight.first_unresolved,
                        sizeof(result.preflight_first_unresolved));
            result.preflight_pthread_create = preflight.pthread_create_available != 0;
            result.preflight_getpid = preflight.getpid_available != 0;
            if (result.preflight_status != COMMONFPS_PREFLIGHT_OK ||
                !result.preflight_pthread_create || !result.preflight_getpid) break;
            const std::uintptr_t pthread_create =
                resolve_remote(shellui_pid, "pthread_create");
            if (pthread_create == 0)
                break;

            const intptr_t entry = commonfps_stable_elfldr_load(
                shellui_pid,
                const_cast<std::uint8_t*>(renderer_elf));

            result.trace_continue_seen =
                commonfps_stable_trace_continue_seen() != 0;
            result.trace_stop_seen =
                commonfps_stable_trace_stop_seen() != 0;
            result.imports_resolved =
                commonfps_stable_import_resolved_count();
            result.imports_unresolved =
                commonfps_stable_import_unresolved_count();
            result.first_unresolved =
                commonfps_stable_first_unresolved();

            if (entry <= 0)
                break;
            result.elf_loaded = true;

            const intptr_t payload_args =
                commonfps_stable_elfldr_payload_args(shellui_pid);
            if (payload_args <= 0)
                break;
            result.payload_args_ready = true;

            const std::size_t code_size = stager_size();
            if (code_size == 0)
                break;

            const intptr_t remote_stager = pt_mmap(
                shellui_pid,
                0,
                code_size,
                PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1,
                0);
            if (remote_stager <= 0)
                break;

            if (kernel_mprotect(
                    shellui_pid,
                    remote_stager,
                    code_size,
                    PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
                break;
            }

            if (pt_copyin(
                    shellui_pid,
                    reinterpret_cast<const void*>(&commonfps_stable_stager),
                    remote_stager,
                    code_size) != 0) {
                break;
            }
            result.remote_stager_ready = true;

            RemoteBootstrapFunctions functions{};
            functions.elf_main = reinterpret_cast<void*>(entry);
            functions.payload_args = reinterpret_cast<void*>(payload_args);
            functions.pthread_create_fn =
                reinterpret_cast<RemotePthreadCreate>(pthread_create);
            functions.start_thread = result.thread_start_requested ? 1U : 0U;

            const intptr_t remote_functions = pt_mmap(
                shellui_pid,
                0,
                sizeof(functions),
                PROT_READ | PROT_WRITE,
                MAP_ANONYMOUS | MAP_PRIVATE,
                -1,
                0);
            if (remote_functions <= 0)
                break;

            if (pt_copyin(
                    shellui_pid,
                    &functions,
                    remote_functions,
                    sizeof(functions)) != 0) {
                break;
            }
            result.remote_functions_ready = true;

            /*
             * Exact v1.0.0 pt_call2 parity: change RIP/arguments while
             * preserving the stopped ShellUI thread's original RSP/RBP.
             * The stager reaches INT3 before it can return.
             */
            result.target_stack_preserved = true;
            (void)call_until_breakpoint_on_target_stack(
                shellui_pid,
                static_cast<std::uintptr_t>(remote_stager),
                static_cast<std::uint64_t>(remote_functions));

            RemoteBootstrapFunctions readback{};
            readback.pthread_create_rc = -777;
            if (pt_copyout(
                    shellui_pid,
                    remote_functions,
                    &readback,
                    sizeof(readback)) == 0 &&
                readback.pthread_create_rc != -777) {
                result.bootstrap_started = true;
                result.pthread_create_rc = readback.pthread_create_rc;
                result.pthread_create_ok =
                    result.thread_start_requested &&
                    readback.pthread_create_rc == 0;
            }
        } while (false);

        result.detached = pt_detach(shellui_pid, 0) == 0;
        if (!identity) (void)kill(shellui_pid, SIGCONT);
    }

    if (kernel_set_ucred_authid(self, original_auth) == 0) {
        result.auth_restored =
            kernel_get_ucred_authid(self) == original_auth;
    }

    return result;
}

StableInjectionResult inject_shellui_stable(pid_t pid, const std::uint8_t* elf, std::size_t size, StableInjectionMode mode) noexcept {
    return inject_impl(pid, nullptr, elf, size, mode);
}
StableInjectionResult inject_shellui_stable(ShellProcessIdentity target, const std::uint8_t* elf, std::size_t size, StableInjectionMode mode) noexcept {
    return inject_impl(target.pid, &target, elf, size, mode);
}

static std::uintptr_t map_plugin_impl(pid_t pid,const ShellProcessIdentity* identity,std::uint8_t* elf,std::uint64_t entry_offset,std::uint64_t symbol_offset) noexcept {
    const auto self=getpid();const auto original=kernel_get_ucred_authid(self);
    if(!original||kernel_set_ucred_authid(self,kPtraceAuthId))return 0;
    std::uintptr_t address=0;
    if(pt_attach(pid)==0) {
        const bool verified=!identity || identity_matches(*identity);
        if (verified) {
            const auto entry=commonfps_stable_elfldr_load(pid,elf);
            if(entry>0)address=std::uintptr_t(entry)-entry_offset+symbol_offset;
        }
        if(pt_detach(pid,0))address=0;
        if(!identity) (void)kill(pid,SIGCONT);
    }
    if(kernel_set_ucred_authid(self,original)||kernel_get_ucred_authid(self)!=original)return 0;
    return address;
}
std::uintptr_t map_shellui_plugin(pid_t pid,std::uint8_t* elf,std::uint64_t entry_offset,std::uint64_t symbol_offset) noexcept {
    return map_plugin_impl(pid,nullptr,elf,entry_offset,symbol_offset);
}
std::uintptr_t map_shellui_plugin(ShellProcessIdentity target,std::uint8_t* elf,std::uint64_t entry_offset,std::uint64_t symbol_offset) noexcept {
    return map_plugin_impl(target.pid,&target,elf,entry_offset,symbol_offset);
}
} // namespace common_fps::ps5
