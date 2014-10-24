/*
   american fuzzy lop - injectable parts
   -------------------------------------

   Written and maintained by Michal Zalewski <lcamtuf@google.com>

   Forkserver design by Jann Horn <jannhorn@googlemail.com>

   Copyright 2013, 2014 Google Inc. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

   This file houses the assembly-level instrumentation injected into fuzzed
   programs. The instrumentation stores pairs of data: identifiers of the
   currently executing line and the line that executed immediately before.

   The assembly code shown below is designed for debug-enabled (-g), 32-bit
   x86 output produced by GCC for C/C++ programs. 64-bit mode can be enabled
   with USE_64BIT.

   In principle, similar code should be easy to inject into any well-behaved
   binary-only code (e.g., using DynamoRIO). Calls and jumps offer natural
   targets for instrumentation, and should offer comparable probe density.

 */

#ifndef _HAVE_AFL_AS_H
#define _HAVE_AFL_AS_H

#include "config.h"
#include "types.h"

#define STRINGIFY_INTERNAL(x) #x
#define STRINGIFY(x) STRINGIFY_INTERNAL((x))

static const u8* trampoline_fmt =

#ifndef USE_64BIT

  "\n"
  "/* --- AFL TRAMPOLINE (32-BIT) --- */\n"
  "\n"
  ".align 8\n"
  "\n"
  "pushl %%ecx\n"
  "pushl %%eax\n"
  "movl  $0x%08x, %%ecx\n"
  "call  __afl_maybe_log\n"
  "popl  %%eax\n"
  "popl  %%ecx\n"
  "\n"
  "/* --- END --- */\n"
  "\n";

#else /* USE_64BIT */

  "\n"
  "/* --- AFL TRAMPOLINE (64-BIT) --- */\n"
  "\n"
  ".align 8\n"
  "\n"
  "pushq %%rcx\n"
  "pushq %%rax\n"
  "movq  $0x%08x, %%rcx\n"
  "call  __afl_maybe_log\n"
  "popq  %%rax\n"
  "popq  %%rcx\n"
  "\n"
  "/* --- END --- */\n"
  "\n";

#endif /* ^!USE_64BIT */

static const u8* main_payload = 

#ifndef USE_64BIT

  "\n"
  "/* --- AFL MAIN PAYLOAD (32-BIT) --- */\n"
  "\n"
  ".text\n"
  ".align 8\n"
  "\n"
  "__afl_maybe_log:\n"
  "\n"
  "  lahf\n"
  "  seto %al\n"
  "  movw %ax, __afl_saved_flags\n"
  "\n"
  "  /* Check if SHM region is already mapped. */\n"
  "\n"
  "  movl  __afl_area_ptr, %eax\n"
  "  testl %eax, %eax\n"
  "  je    __afl_setup\n"
  "\n"
  "__afl_store:\n"
  "\n"
  "  /* Calculate and store hit for the code location specified in ecx. */\n"
  "\n"
#ifndef COVERAGE_ONLY
  "  xorl __afl_prev_loc, %ecx\n"
  "  xorl %ecx, __afl_prev_loc\n"
  "  xorl $" STRINGIFY(MAP_SIZE-1) ", __afl_prev_loc\n"
#endif /* !COVERAGE_ONLY */
  "  addl %ecx, %eax\n"
  "\n"
#ifdef COVERAGE_ONLY
  "  orb  $1, (%eax)\n"
#else
  "  incb (%eax)\n"
#endif /* ^COVERAGE_ONLY */
  "\n"
  "__afl_return:\n"
  "\n"
  "  movw __afl_saved_flags, %ax\n"
  "  addb $127, %al\n"
  "  sahf\n"
  "  ret\n"
  "\n"
  ".align 8\n"
  "\n"
  "__afl_setup:\n"
  "\n"
  "  /* Do not retry setup if we had previous failures. */\n"
  "\n"
  "  cmpb $0, __afl_setup_failure\n"
  "  jne  __afl_return\n"
  "\n"
  "  /* Map SHM, jumping to __afl_setup_abort if something goes wrong. */\n"
  "\n"
  "  pushl %ecx\n"
  "  pushl %edx\n"
  "\n"
  "  pushl $.AFL_SHM_ID\n"
  "  call  getenv\n"
  "  addl  $4, %esp\n"
  "\n"
  "  testl %eax, %eax\n"
  "  je    __afl_setup_abort\n"
  "\n"
  "  pushl %eax\n"
  "  call  atoi\n"
  "  addl  $4, %esp\n"
  "\n"
  "  pushl $0          /* shmat flags    */\n"
  "  pushl $0          /* requested addr */\n"
  "  pushl %eax        /* SHM ID         */\n"
  "  call  shmat\n"
  "  addl  $12, %esp\n"
  "\n"
  "  cmpl $-1, %eax\n"
  "  je   __afl_setup_abort\n"
  "\n"
  "  /* Store the address of the SHM region. */\n"
  "\n"
  "  movl %eax, __afl_area_ptr\n"
  "\n"
  "  popl %edx\n"
  "  popl %ecx\n"
  "\n"
  "__afl_forkserver:\n"
  "\n"
  "  /* Enter the fork server mode to avoid the overhead of execve() calls. */\n"
  "\n"
  "  pushl %eax\n"
  "  pushl %ecx\n"
  "  pushl %edx\n"
  "\n"
  "  /* Phone home and tell the parent that we're OK. (Note that signals with\n"
  "     no SA_RESTART will mess it up). */\n"
  "\n"
  "  pushl $4          /* length    */\n"
  "  pushl $__afl_temp /* data      */\n"
  "  pushl $" STRINGIFY(FORKSRV_FD + 1) "  /* file desc */\n"
  "  call  write\n"
  "  addl  $12, %esp\n"
  "\n"
  "__afl_fork_wait_loop:\n"
  "\n"
  "  /* Wait for parent by reading from the pipe. Abort if read fails. */\n"
  "\n"
  "  pushl $4          /* length    */\n"
  "  pushl $__afl_temp /* data      */\n"
  "  pushl $" STRINGIFY(FORKSRV_FD) "      /* file desc */\n"
  "  call  read\n"
  "  addl  $12, %esp\n"
  "\n"
  "  cmpl  $4, %eax\n"
  "  jne   __afl_die\n"
  "\n"
  "  /* Once woken up, create a clone of our process. This is an excellent use\n"
  "     case for syscall(__NR_clone, 0, CLONE_PARENT), but glibc boneheadedly\n"
  "     caches getpid() results and offers no way to update the value, breaking\n"
  "     abort(), raise(), and a bunch of other things :-( */\n"
  "\n"
  "  call fork\n"
  "\n"
  "  cmpl $0, %eax\n"
  "  jl   __afl_die\n"
  "  je   __afl_fork_resume\n"
  "\n"
  "  /* In parent process: write PID to pipe, then wait for child. */\n"
  "\n"
  "  movl  %eax, __afl_fork_pid\n"
  "\n"
  "  pushl $4              /* length    */\n"
  "  pushl $__afl_fork_pid /* data      */\n"
  "  pushl $" STRINGIFY(FORKSRV_FD + 1) "      /* file desc */\n"
  "  call  write\n"
  "  addl  $12, %esp\n"
  "\n"
  "  pushl $2             /* WUNTRACED */\n"
  "  pushl $__afl_temp    /* status    */\n"
  "  pushl __afl_fork_pid /* PID       */\n"
  "  call  waitpid\n"
  "  addl  $12, %esp\n"
  "\n"
  "  cmpl  $0, %eax\n"
  "  jle   __afl_die\n"
  "\n"
  "  /* Relay wait status to pipe, then loop back. */\n"
  "\n"
  "  pushl $4          /* length    */\n"
  "  pushl $__afl_temp /* data      */\n"
  "  pushl $" STRINGIFY(FORKSRV_FD + 1) "  /* file desc */\n"
  "  call  write\n"
  "  addl  $12, %esp\n"
  "\n"
  "  jmp __afl_fork_wait_loop\n"
  "\n"
  "__afl_fork_resume:\n"
  "\n"
  "  /* In child process: close fds, resume execution. */\n"
  "\n"
  "  pushl $" STRINGIFY(FORKSRV_FD) "\n"
  "  call  close\n"
  "\n"
  "  pushl $" STRINGIFY(FORKSRV_FD + 1) "\n"
  "  call  close\n"
  "\n"
  "  addl  $8, %esp\n"
  "\n"
  "  popl %edx\n"
  "  popl %ecx\n"
  "  popl %eax\n"
  "  jmp  __afl_store\n"
  "\n"
  "__afl_die:\n"
  "\n"
  "  xorl %eax, %eax\n"
  "  call exit\n"
  "\n"
  "__afl_setup_abort:\n"
  "\n"
  "  /* Record setup failure so that we don't keep calling\n"
  "     shmget() / shmat() over and over again. */\n"
  "\n"
  "  incb __afl_setup_failure\n"
  "  popl %edx\n"
  "  popl %ecx\n"
  "  jmp __afl_return\n"
  "\n"
  ".AFL_VARS:\n"
  "\n"
  "  .comm   __afl_area_ptr, 4, 32\n"
  "  .comm   __afl_setup_failure, 1, 32\n"
#ifndef COVERAGE_ONLY
  "  .comm   __afl_prev_loc, 4, 32\n"
#endif /* !COVERAGE_ONLY */
  "  .comm   __afl_saved_flags, 2, 32\n"
  "  .comm   __afl_fork_pid, 4, 32\n"
  "  .comm   __afl_temp, 4, 32\n"
  "\n"
  ".AFL_SHM_ID:\n"
  "  .string \"" SHM_ENV_VAR "\"\n"
  "\n"
  "/* --- END --- */\n"
  "\n";

#else /* USE_64BIT */

  "\n"
  "/* --- AFL MAIN PAYLOAD (64-BIT) --- */\n"
  "\n"
  ".text\n"
  ".align 8\n"
  "\n"
  "__afl_maybe_log:\n"
  "\n"
  "  lahf\n"
  "  seto %al\n"
  "  movw %ax, __afl_saved_flags(%rip)\n"
  "\n"
  "  /* Check if SHM region is already mapped. */\n"
  "\n"
  "  movq __afl_area_ptr(%rip), %rax\n"
  "  testq %rax, %rax\n"
  "  je __afl_setup\n"
  "\n"
  "__afl_store:\n"
  "\n"
  "  /* Calculate and store hit for the code location specified in ecx. */\n"
  "\n"
#ifndef COVERAGE_ONLY
  "  xorq __afl_prev_loc(%rip), %rcx\n"
  "  xorq %rcx, __afl_prev_loc(%rip)\n"
  "  xorq $" STRINGIFY(MAP_SIZE-1) ", __afl_prev_loc(%rip)\n"
#endif /* !COVERAGE_ONLY */
  "  addq %rcx, %rax\n"
  "\n"
#ifdef COVERAGE_ONLY
  "  orb  $1, (%rax)\n"
#else
  "  incb (%rax)\n"
#endif /* ^COVERAGE_ONLY */
  "\n"
  "__afl_return:\n"
  "\n"
  "  movw __afl_saved_flags(%rip), %ax\n"
  "  addb $127, %al\n"
  "  sahf\n"
  "  ret\n"
  "\n"
  ".align 8\n"
  "\n"
  "__afl_setup:\n"
  "\n"
  "  /* Do not retry setup if we had previous failures. */\n"
  "\n"
  "  cmpb $0, __afl_setup_failure(%rip)\n"
  "  jne __afl_return\n"
  "\n"
  "  /* Map SHM, jumping to __afl_setup_abort if something goes wrong. */\n"
  "\n"
  "  pushq %rcx\n"
  "  pushq %rdx\n"
  "  pushq %rdi\n"
  "  pushq %rsi\n"
  "\n"
  "  leaq .AFL_SHM_ID(%rip), %rdi\n"
  "  call getenv@PLT\n"
  "\n"
  "  testq %rax, %rax\n"
  "  je    __afl_setup_abort\n"
  "\n"
  "  movq  %rax, %rdi\n"
  "  call  atoi@PLT\n"
  "\n"
  "  xorq %rdx, %rdx   /* shmat flags    */\n"
  "  xorq %rsi, %rsi   /* requested addr */\n"
  "  movq %rax, %rdi   /* SHM ID         */\n"
  "  call shmat@PLT\n"
  "\n"
  "  cmpq $-1, %rax\n"
  "  je   __afl_setup_abort\n"
  "\n"
  "  /* Store the address of the SHM region. */\n"
  "\n"
  "  movq %rax, __afl_area_ptr(%rip)\n"
  "\n"
  "  popq %rsi\n"
  "  popq %rdi\n"
  "  popq %rdx\n"
  "  popq %rcx\n"

/* FIXME_START */

  "\n"
  "__afl_forkserver:\n"
  "\n"
  "  /* Enter the fork server mode to avoid the overhead of execve() calls. */\n"
  "\n"
  "  pushq %rax\n"
  "  pushq %rcx\n"
  "  pushq %rdx\n"
  "  pushq %rdi\n"
  "  pushq %rsi\n"
  "\n"
  "  /* Phone home and tell the parent that we're OK. (Note that signals with\n"
  "     no SA_RESTART will mess it up). */\n"
  "\n"
  "  movq $4, %rdx               /* length    */\n"
  "  leaq __afl_temp(%rip), %rsi /* data      */\n"
  "  movq $" STRINGIFY(FORKSRV_FD + 1) ", %rdi       /* file desc */\n"
  "  call write@PLT\n"
  "\n"
  "__afl_fork_wait_loop:\n"
  "\n"
  "  /* Wait for parent by reading from the pipe. Abort if read fails. */\n"
  "\n"
  "  movq $4, %rdx               /* length    */\n"
  "  leaq __afl_temp(%rip), %rsi /* data      */\n"
  "  movq $" STRINGIFY(FORKSRV_FD) ", %rdi           /* file desc */\n"
  "  call read@PLT\n"
  "  cmpq $4, %rax\n"
  "  jne  __afl_die\n"
  "\n"
  "  /* Once woken up, create a clone of our process. This is an excellent use\n"
  "     case for syscall(__NR_clone, 0, CLONE_PARENT), but glibc boneheadedly\n"
  "     caches getpid() results and offers no way to update the value, breaking\n"
  "     abort(), raise(), and a bunch of other things :-( */\n"
  "\n"
  "  call fork@PLT\n"
  "  cmpq $0, %rax\n"
  "  jl   __afl_die\n"
  "  je   __afl_fork_resume\n"
  "\n"
  "  /* In parent process: write PID to pipe, then wait for child. */\n"
  "\n"
  "  movl %eax, __afl_fork_pid(%rip)\n"
  "\n"
  "  movq $4, %rdx                   /* length    */\n"
  "  leaq __afl_fork_pid(%rip), %rsi /* data      */\n"
  "  movq $" STRINGIFY(FORKSRV_FD + 1) ", %rdi             /* file desc */\n"
  "  call  write@PLT\n"
  "\n"
  "  movq $2, %rdx                   /* WUNTRACED */\n"
  "  leaq __afl_temp(%rip), %rsi     /* status    */\n"
  "  movq __afl_fork_pid(%rip), %rdi /* PID       */\n"
  "  call waitpid@plt\n"
  "  cmpq $0, %rax\n"
  "  jle  __afl_die\n"
  "\n"
  "  /* Relay wait status to pipe, then loop back. */\n"
  "\n"
  "  movq $4, %rdx               /* length    */\n"
  "  leaq __afl_temp(%rip), %rsi /* data      */\n"
  "  movq $" STRINGIFY(FORKSRV_FD + 1) ", %rdi         /* file desc */\n"
  "  call write@plt\n"
  "\n"
  "  jmp  __afl_fork_wait_loop\n"
  "\n"
  "__afl_fork_resume:\n"
  "\n"
  "  /* In child process: close fds, resume execution. */\n"
  "\n"
  "  movq $" STRINGIFY(FORKSRV_FD) ", %rdi\n"
  "  call close@plt\n"
  "\n"
  "  movq $" STRINGIFY(FORKSRV_FD + 1) ", %rdi\n"
  "  call close@plt\n"
  "\n"
  "  popq %rsi\n"
  "  popq %rdi\n"
  "  popq %rdx\n"
  "  popq %rcx\n"
  "  popq %rax\n"
  "  jmp  __afl_store\n"
  "\n"
  "__afl_die:\n"
  "\n"
  "  xorq %rax, %rax\n"
  "  call exit@plt\n"
  "\n"
  "__afl_setup_abort:\n"
  "\n"
  "  /* Record setup failure so that we don't keep calling\n"
  "     shmget() / shmat() over and over again. */\n"
  "\n"
  "  incb __afl_setup_failure(%rip)\n"
  "  popq %rsi\n"
  "  popq %rdi\n"
  "  popq %rdx\n"
  "  popq %rcx\n"
  "  jmp __afl_return\n"
  "\n"
  ".AFL_VARS:\n"
  "\n"
  "  .comm   __afl_area_ptr, 8, 32\n"
  "  .comm   __afl_setup_failure, 1, 32\n"
#ifndef COVERAGE_ONLY
  "  .comm   __afl_prev_loc, 8, 32\n"
#endif /* !COVERAGE_ONLY */
  "  .comm   __afl_saved_flags, 2, 32\n"
  "  .comm   __afl_fork_pid, 4, 32\n"
  "  .comm   __afl_temp, 4, 32\n"
  "\n"
  ".AFL_SHM_ID:\n"
  "  .string \"" SHM_ENV_VAR "\"\n"
  "\n"
  "/* --- END --- */\n"
  "\n";

#endif /* ^!USE_64BIT */

#endif /* !_HAVE_AFL_AS_H */
