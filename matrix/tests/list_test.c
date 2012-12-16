#include <types.h>
#include <stddef.h>
#include "matrix/matrix.h"
#include "debug.h"
#include "list.h"

void list_test()
{
	struct list header;
	struct list entry1;
	struct list entry2;
	struct list *l;
	uint32_t i;

	LIST_INIT(&header);
	kprintf("header:0x%x, prev:0x%x, next:0x%x\n",
		&header, header.prev, header.next);

	LIST_INIT(&entry1);
	list_add_tail(&entry1, &header);

	LIST_INIT(&entry2);
	list_add_tail(&entry2, &header);

	i = 0;
	LIST_FOR_EACH(l, &header) {
		kprintf("entry %d\n", ++i);
	}
}