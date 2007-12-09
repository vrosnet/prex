/*-
 * Copyright (c) 2005, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors 
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * context.c - context management routines
 */

/*
 * The context consists of kernel/user mode registers, and
 * kernel stack. The user mode registers are always saved to the
 * kernel stack when processor enters kernel mode by H/W or S/W events.
 *
 * The user mode registers are located in the interrupt/trap frame
 * at the top of the kernel stack. Before the control returns to user
 * mode next time, these register value will be restored automatically.
 *
 * All thread owns its context to keep its execution state. The
 * scheduler will switch the context to change an active thread.
 */

#include <kernel.h>
#include "cpu.h"

extern void syscall_ret();
extern void kernel_thread_entry();
extern void __context_switch(struct kern_regs *, struct kern_regs *);

/*
 * Initialize specified context.
 * All thread will start at the trap return routine - syscall_ret().
 * In this time, the interrupt flag is enabled and I/O access
 * is disabled.
 * 
 * @ctx: context id (pointer)
 * @kstack: kernel stack for the context
 */
void context_init(context_t ctx, void *kstack)
{
	struct kern_regs *k;
	struct cpu_regs *u;

	ctx->uregs = (struct cpu_regs *)(kstack - sizeof(struct cpu_regs));

	/* Initialize kernel mode registers */
	k = &ctx->kregs;
	k->lr = (u_long)syscall_ret;
	k->sp = (u_long)ctx->uregs;

	/* Reset minimum user mode registers */
	u = ctx->uregs;
	u->r0 = 0;
	u->r1 = 0x11111111;
	u->r2 = 0x22222222;
	u->r3 = 0x33333333;
	u->svc_sp = (u_long)kstack;
	u->cpsr = PSR_APP_MODE;	/* FIQ/IRQ is enabled */
}

/*
 * Set data to the specific register stored in context.
 *
 * @type: register type to be set
 * @val: register value to be set
 *
 * Note: When user mode program counter is set, all register
 * values except stack pointer are reset to default value.
 */
void context_set(context_t ctx, int type, u_long val)
{
	struct kern_regs *k;
	struct cpu_regs *u;

	switch (type) {
	case USER_ENTRY:	/* User mode program counter */
		u = ctx->uregs;
		u->cpsr = PSR_APP_MODE;	/* FIQ/IRQ is enabled */
		u->pc = u->lr = val;
		break;
	case USER_STACK:	/* User mode stack pointer */
		u = ctx->uregs;
		u->sp = val;
		break;
	case KERN_ENTRY:	/* Kernel mode program counter */
		k = &ctx->kregs;
		k->lr = (u_long)kernel_thread_entry;
		k->r4 = val;	/* Entry point */
		break;
	case KERN_ARG:
		k = &ctx->kregs;
		k->r5 = val;
		break;
	}
}

/*
 * Switch to new context
 *
 * Kernel mode registers and kernel stack pointer are switched to the
 * next context.
 *
 * We don't use x86 task switch mechanism to minimize the context space.
 * The system has only one TSS(task state segment), and the context
 * switching is done by changing the register value in this TSS. Processor
 * will reload them automatically when it enters to the kernel mode in
 * next time.
 *
 * It is assumed all interrupts are disabled by caller.
 *
 * TODO: FPU context is not switched as of now.
 */
void context_switch(context_t prev, context_t next)
{
	__context_switch(&prev->kregs, &next->kregs);
}

/*
 * Save user mode context to handle exceptions.
 *
 * @exc: exception code passed to the exception handler
 *
 * Copy current user mode registers in the kernel stack to the user
 * mode stack. The user stack pointer is adjusted for this area.
 * So that the exception handler can get the register state of
 * the target thread.
 *
 * It builds arguments for the exception handler in the following
 * format.
 *
 *   void exception_handler(int exc, void *regs);
 */
void context_save(context_t ctx, int exc)
{
	struct cpu_regs *cur, *sav;

	/* Copy current register context into user mode stack */
	cur = ctx->uregs;
	sav = (struct cpu_regs *)(cur->sp - sizeof(struct cpu_regs));
	memcpy(sav, cur, sizeof(struct cpu_regs));

	/* Setup arguments for exception handler */
	cur->sp = (u_long)sav;
	cur->r0 = exc;		/* Argument 1 */
	cur->r1 = (u_long)sav;	/* Argument 2 */
	cur->r2 = 0xdeadbeef;	/* Tag */
}

/*
 * Restore register context to return from the exception handler.
 *
 * @regs: pointer to user mode register context.
 */
void context_restore(context_t ctx, void *regs)
{
	struct cpu_regs *cur;

	/* Restore user mode context */
	cur = ctx->uregs;
	memcpy(cur, regs, sizeof(struct cpu_regs));

	/* Correct some registers for fail safe */
	cur->cpsr = PSR_APP_MODE;
}