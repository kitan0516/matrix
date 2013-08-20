/*
 * mmu.c
 */

#include <types.h>
#include <string.h>	// memset
#include "hal/hal.h"
#include "hal/isr.h"	// register interrupt handler
#include "hal/core.h"
#include "mm/mm.h"
#include "mm/mlayout.h"
#include "mm/mmu.h"
#include "mm/kmem.h"
#include "debug.h"
#include "proc/process.h"
#include "proc/thread.h"

/* Determine if an MMU context is the kernel context */
#define IS_KERNEL_CTX(ctx)	(ctx == &_kernel_mmu_ctx)

/* Determine if an MMU context is the current context */
#define IS_CURRENT_CTX(ctx)	(ctx == CURR_ASPACE)

/*
 * Page Table
 * Each page table has 1024 page table entries, actually each
 * page table entry is just a 32 bit integer for X86 arch
 */
struct ptbl {
	struct page pte[1024];
};

/*
 * Page Directory
 * Each page directory has 1024 pointers to its page table entry
 */
struct pdir {
	/* Page directory entry for the page table below, this must be the
	 * first member of pdir structure because we use the physical address
	 * of this structure as the PDBR.
	 */
	uint32_t pde[1024];

	/* Page tables for this page directory */
	struct ptbl *ptbl[1024];
};

struct mmu_ctx _kernel_mmu_ctx;
static struct irq_hook _pf_hook;

extern uint32_t _placement_addr;

extern void copy_page_physical(uint32_t dst, uint32_t src);

static struct ptbl *clone_ptbl(struct ptbl *src, phys_addr_t *phys_addr)
{
	int i;
	struct ptbl *ptbl;
	
	/* Make a new page table, which is page aligned */
	ptbl = (struct ptbl *)kmem_alloc_p(sizeof(struct ptbl), phys_addr, MM_ALIGN_F);
	
	/* Clear the content of the new page table */
	memset(ptbl, 0, sizeof(struct ptbl));

	/* Clone each of the page frames */
	for (i = 0; i < 1024; i++) {
		/* If the source entry has a frame associated with it */
		if (src->pte[i].frame) {

			/* Get a new frame */
			page_alloc(&(ptbl->pte[i]), 0);

			/* Clone the flags from source to destination */
			if (src->pte[i].present) ptbl->pte[i].present = 1;
			if (src->pte[i].rw) ptbl->pte[i].rw = 1;
			if (src->pte[i].user) ptbl->pte[i].user = 1;
			if (src->pte[i].accessed) ptbl->pte[i].accessed = 1;
			if (src->pte[i].dirty) ptbl->pte[i].dirty = 1;

			/* Physically copy the data accross */
			copy_page_physical(ptbl->pte[i].frame * 0x1000, 
					   src->pte[i].frame * 0x1000);
		}
	}

	return ptbl;
}

/**
 * Get a page from the specified mmu context
 * @ctx		- mmu context
 * @virt	- virtual address to map the memory to
 * @make	- make a new page table if we are out of page table
 * @mmflag	- memory manager flags
 */
struct page *mmu_get_page(struct mmu_ctx *ctx, ptr_t virt, boolean_t make, int mmflag)
{
	struct page *page;
	uint32_t dir_idx, tbl_idx;
	struct pdir *pdir;

	ASSERT(ctx != NULL);

	/* Calculate the page table index and page directory index */
	tbl_idx = (virt / PAGE_SIZE) % 1024;
	dir_idx = (virt / PAGE_SIZE) / 1024;

	/* Get the page directory from the context */
	pdir = ctx->pdir;

	if (pdir->ptbl[dir_idx]) {	// The page table already assigned
		page = &pdir->ptbl[dir_idx]->pte[tbl_idx];
	} else if (make) {		// Make a new page table
		phys_addr_t tmp;
		
		/* Allocate a new page table */
		pdir->ptbl[dir_idx] =
			(struct ptbl *)kmem_alloc_p(sizeof(struct ptbl), &tmp, MM_ALIGN_F);
		
		/* Clear the content of the page table */
		memset(pdir->ptbl[dir_idx], 0, sizeof(struct ptbl));

		/* Set the content of the page table */
		pdir->pde[dir_idx] = tmp | 0x7;	// PRESENT, RW, US.
		
		page = &pdir->ptbl[dir_idx]->pte[tbl_idx];
	} else {
		DEBUG(DL_INF, ("no page for addr(0x%08x) in mmu ctx(0x%08x)\n",
			       virt, ctx));
		page = NULL;
	}

	return page;
}

int mmu_map(struct mmu_ctx *ctx, ptr_t start, size_t size, int flags, ptr_t *addrp)
{
	int rc;
	struct page *p;
	ptr_t virt;

	if (!size || (size % PAGE_SIZE)) {
		DEBUG(DL_DBG, ("size (%x) invalid.\n", size));
		rc = -1;
		goto out;
	} else if (!(flags & (MAP_READ_F | MAP_WRITE_F | MAP_EXEC_F))) {
		DEBUG(DL_DBG, ("flags (%x) invalid.\n", flags));
		rc = -1;
		goto out;
	}

	if (flags & MAP_FIXED_F) {
		if (start % PAGE_SIZE) {
			DEBUG(DL_DBG, ("parameter start(%p) not aligned.\n", start));
			rc = -1;
			goto out;
		}
	} else /*if (!addrp)*/ {
		/* We didn't support this yet. */
		DEBUG(DL_DBG, ("non-fixed map not support yet.\n"));
		rc = -1;
		goto out;
	}

	DEBUG(DL_DBG, ("ctx(%p) start(%p), size(%x).\n", ctx, start, size));

	for (virt = start; virt < (start + size); virt += PAGE_SIZE) {
		p = mmu_get_page(ctx, virt, TRUE, 0);
		if (!p) {
			DEBUG(DL_DBG, ("mmu_get_page failed, addr(%p).\n", virt));
			rc = -1;
			goto out;
		}
		DEBUG(DL_DBG, ("ctx(%p) page(%p) frame(%x).\n", ctx, p, p->frame));
		page_alloc(p, 0);
		p->user = IS_KERNEL_CTX(ctx) ? FALSE : TRUE;
		p->rw = FLAG_ON(flags, MAP_WRITE_F) ? TRUE : FALSE;
	}
	
	rc = 0;

 out:
	if (rc != 0) {
		/* Do clean up if needed */
		;
	}
	
	return rc;
}

int mmu_unmap(struct mmu_ctx *ctx, ptr_t start, size_t size)
{
	int rc;
	ptr_t virt;
	struct page *p;

	if (!size || (start % PAGE_SIZE) || (size % PAGE_SIZE)) {
		rc = -1;
		goto out;
	}

	for (virt = start; virt < start + size; virt += PAGE_SIZE) {
		p = mmu_get_page(ctx, virt, FALSE, 0);
		if (!p) {
			rc = -1;
			goto out;
		}
		DEBUG(DL_DBG, ("ctx(%p) page(%p) frame(%x).\n", ctx, p, p->frame));
		page_free(p);
	}

	rc = 0;

 out:
	return rc;
}

void mmu_switch_ctx(struct mmu_ctx *ctx)
{
	uint32_t cr0;
	boolean_t state;

	/* The kernel process does not have an address space. When switching
	 * to one of its threads, it is not necessary to switch to the kernel
	 * MMU context, as all mappings in the kernel context are visible in
	 * all address spaces. Kernel threads should never touch the userspace
	 * portion of the address space.
	 */
	if (ctx && (ctx != CURR_ASPACE)) {
		
		ASSERT((ctx->pdbr % PAGE_SIZE) == 0);

		DEBUG(DL_DBG, ("ctx(%p), aspace(%p), core(%p).\n", ctx,
			       CURR_ASPACE, CURR_CORE));

		state = irq_disable();

		/* Update the current mmu context */
		CURR_ASPACE = ctx;

		/* Set CR3 register */
		asm volatile("mov %0, %%cr3":: "r"(ctx->pdbr));
	
		asm volatile("mov %%cr0, %0": "=r"(cr0));
		cr0 |= 0x80000000;	// Enable paging
		asm volatile("mov %0, %%cr0":: "r"(cr0));

		irq_restore(state);
	}
}

/*
 * We don't call irq_done here, check this when we implementing
 * the paging feature of our kernel.
 */
void page_fault(struct registers *regs)
{
	uint32_t faulting_addr;
	int present;
	int rw;
	int us;
	int reserved;

	/* A page fault has occurred. The CR2 register
	 * contains the faulting address.
	 */
	asm volatile("mov %%cr2, %0" : "=r"(faulting_addr));

	present = regs->err_code & 0x1;
	rw = regs->err_code & 0x2;
	us = regs->err_code & 0x4;
	reserved = regs->err_code & 0x8;

	dump_registers(regs);

	/* Print an error message */
	kprintf("Page fault(%s%s%s%s) at 0x%x - EIP: 0x%x\n\n", 
		present ? "present " : "non-present ",
		rw ? "write " : "read ",
		us ? "user-mode " : "supervisor-mode ",
		reserved ? "reserved " : "",
		faulting_addr,
		regs->eip);

	/* Print current thread */
	//kprintf("Faulting process(%s:%d) pdir(%p) thread(%s:%d)\n",
	//	CURR_PROC->name, CURR_PROC->id, CURR_PROC->mmu_ctx->pdir,
	//	CURR_THREAD->name, CURR_THREAD->id);

	PANIC("Page fault");
}

void mmu_clone_ctx(struct mmu_ctx *dst, struct mmu_ctx *src)
{
	int i;
	struct pdir *dst_dir, *src_dir, *krn_dir;

	dst_dir = dst->pdir;
	src_dir = src->pdir;
	krn_dir = _kernel_mmu_ctx.pdir;

	/* For each page table, if the page table is in the kernel directory,
	 * do not make a new copy.
	 */
	for (i = 0; i < 1024; i++) {
		if (!src_dir->ptbl[i]) {
			/* Already allocated */
			continue;
		}
		
		if (krn_dir->ptbl[i] == src_dir->ptbl[i]) {
			/* It's in the kernel, so just use the same page table
			 * and page directory entry
			 */
			dst_dir->ptbl[i] = src_dir->ptbl[i];
			dst_dir->pde[i] = src_dir->pde[i];
		} else {
			/* Physically clone the page table if it's not kernel stuff */
			uint32_t pde;

			DEBUG(DL_DBG, ("dst(0x%x), src(0x%x), addr(0x%x).\n",
				       dst, src, i * 1024 * PAGE_SIZE));
			dst_dir->ptbl[i] = clone_ptbl(src_dir->ptbl[i], &pde);
			dst_dir->pde[i] = pde | 0x07;
		}
	}
}

struct mmu_ctx *mmu_create_ctx()
{
	struct mmu_ctx *ctx;
	phys_addr_t pdbr;

	ctx = kmem_alloc(sizeof(struct mmu_ctx), 0);
	if (!ctx) {
		return NULL;
	}

	ctx->pdir = kmem_alloc_p(sizeof(struct pdir), &pdbr, MM_ALIGN_F);
	if (!ctx->pdir) {
		kmem_free(ctx);
		return NULL;
	}
	memset(ctx->pdir, 0, sizeof(struct pdir));
	ctx->pdbr = pdbr;
	ASSERT((ctx->pdbr % PAGE_SIZE) == 0);

	mutex_init(&ctx->lock, "mmu-mutex", 0);	// TODO: flags need to be confirmed

	return ctx;
}

void mmu_destroy_ctx(struct mmu_ctx *ctx)
{
	ASSERT(!IS_KERNEL_CTX(ctx));

	kmem_free(ctx->pdir);
	kmem_free(ctx);
}

void init_mmu()
{
	phys_addr_t i, pdbr;
	struct page *page;

	/* Initialize the kernel MMU context structure */
	_kernel_mmu_ctx.pdir = kmem_alloc_p(sizeof(struct pdir), &pdbr, MM_ALIGN_F);
	_kernel_mmu_ctx.pdbr = pdbr;
	memset(_kernel_mmu_ctx.pdir, 0, sizeof(struct pdir));
	
	DEBUG(DL_DBG, ("kernel mmu ctx(%p), pdbr(%p), core(%p)\n",
		       &_kernel_mmu_ctx, _kernel_mmu_ctx.pdbr, CURR_CORE));

	/* Allocate some pages in the kernel pool area. Here we call mmu_get_page
	 * but not page_alloc. this cause the page tables to be created when necessary.
	 * We can't allocate page yet because they need to be identity mapped first.
	 */
	for (i = KERNEL_KMEM_START;
	     i < (KERNEL_KMEM_START + KERNEL_KMEM_SIZE);
	     i += PAGE_SIZE) {
		mmu_get_page(&_kernel_mmu_ctx, i, TRUE, 0);
	}

	/* Allocate some pages in the physical map area. */
	for (i = KERNEL_PMAP_START;
	     (i < (KERNEL_PMAP_START + KERNEL_PMAP_SIZE)) && (i != 0);
	     i += PAGE_SIZE) {
		mmu_get_page(&_kernel_mmu_ctx, i, TRUE, 0);
	}

	/* Do identity map (physical addr == virtual addr) for the memory we used. */
	for (i = 0; i < (_placement_addr + PAGE_SIZE); i += PAGE_SIZE) {
		/* Kernel code is readable but not writable from user-mode */
		page = mmu_get_page(&_kernel_mmu_ctx, i, TRUE, 0);
		page_alloc(page, 0);
		page->user = FALSE;
		page->rw = FALSE;
	}

	/* Allocate those pages we mapped for kernel pool area */
	for (i = KERNEL_KMEM_START;
	     i < (KERNEL_KMEM_START + KERNEL_KMEM_SIZE);
	     i += PAGE_SIZE) {
		page = mmu_get_page(&_kernel_mmu_ctx, i, TRUE, 0);
		page_alloc(page, 0);
		page->user = FALSE;
		page->rw = FALSE;
	}

	/* Allocate those pages we mapped for physical map area */
	for (i = KERNEL_PMAP_START;
	     (i < (KERNEL_PMAP_START + KERNEL_PMAP_SIZE)) && (i != 0);
	     i += PAGE_SIZE) {
		page = mmu_get_page(&_kernel_mmu_ctx, i, TRUE, 0);
		page_alloc(page, 0);
		page->user = FALSE;
		page->rw = FALSE;
	}

	/* Before we enable paging, we must register our page fault handler */
	register_irq_handler(14, &_pf_hook, page_fault);

	/* Enable paging and switch to our kernel mmu context, kernel mmu context
	 * will be used by every process.
	 */
	mmu_switch_ctx(&_kernel_mmu_ctx);
}
