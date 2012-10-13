#include <types.h>
#include <stddef.h>
#include "spinlock.h"

uint32_t _test_global_var;

void lock_test()
{
	struct spinlock l;
	int i;

	_test_global_var = 0;
	
	spinlock_init(&l, "test_lock");

	for (i = 0; i < 128; i++) {
		
		spinlock_acquire(&l);
		_test_global_var++;
		spinlock_release(&l);

		spinlock_acquire(&l);
		_test_global_var--;
		spinlock_release(&l);
	}

	kprintf("spinlock test OK!\n");
}