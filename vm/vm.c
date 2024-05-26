/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

struct list frame_table;
#include "threads/mmu.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	list_init(&frame_table);
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

uint64_t page_hash(const struct hash_elem *e, void *aux);
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux);
static void hash_page_destroy(struct hash_elem *e, void *aux);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
// 커널이 새로운 페이지를 달라는 요청을 받을 때 수행됨
// 구조체를 할당하고 페이지 타입에 맞는 적절한 초기화 함수를 세팅함으로써 새로운 페이지를 초기화 한다. 그리고 유저 프로그램으로 제어권을 넘김
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{
		struct page *page = malloc(sizeof(struct page));
		if (!page)
		{
			goto err;
		}

		bool (*initializer)(struct page *, enum vm_type, void *);
		switch (VM_TYPE(type))
		{
		case VM_ANON:
			initializer = anon_initializer;
			break;
		case VM_FILE:
			initializer = file_backed_initializer;
			break;
		}
		uninit_new(page, upage, init, type, aux, initializer);
		page->writable = writable;
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct page *page = NULL;

	struct page pg;
	struct hash_elem *elem;
	pg.va = pg_round_down(va);
	elem = hash_find(spt, &pg.hash_elem);
	if (elem)
	{
		page = hash_entry(elem, struct page, hash_elem);
	}

	return page;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	if (hash_insert(&spt->hash_table, &page->hash_elem))
	{
		return false;
	}
	else
	{
		return true;
	}
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = calloc(1, sizeof *frame);

	if (!frame)
		PANIC("TODO!");
	frame->kva = palloc_get_page(PAL_USER | PAL_ZERO);

	if (!frame)
		PANIC("TODO!");

	list_push_back(&frame_table, &frame->frame_elem);
	frame->page = NULL;

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
	vm_alloc_page(VM_ANON, pg_round_down(addr), 1);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	void *rsp;
	if (addr == NULL || is_kernel_vaddr(addr) || !not_present)
	{
		return false;
	}
	if (addr >= USER_STACK_MIN && (addr >= f->rsp || addr == f->rsp - 8))
	{
		vm_stack_growth(addr); // 스택 성장을 처리
	}

	page = spt_find_page(spt, addr);

	if (!page || (write && !page->writable))
	{
		return false;
	}

	return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function */

	if (page = spt_find_page(&thread_current()->spt, va))
		return vm_do_claim_page(page);
	else
		return false;
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	struct thread *cur = thread_current();

	pml4_set_page(cur->pml4, page->va, frame->kva, page->writable);

	/* TODO: Insert page table entry to map page's VA to frame's PA. */

	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	hash_init(&spt->hash_table, page_hash, page_less, NULL);
}

uint64_t page_hash(const struct hash_elem *e, void *aux)
{
	struct page *page = hash_entry(e, struct page, hash_elem);
	return hash_bytes(&page->va, sizeof *page->va);
}

bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
	struct page *page_a = hash_entry(a, struct page, hash_elem);
	struct page *page_b = hash_entry(b, struct page, hash_elem);

	return page_a->va < page_b->va;
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	struct hash_iterator i;
	hash_first(&i, &src->hash_table);
	while (hash_next(&i))
	{
		struct page *src_page = hash_entry(hash_cur(&i), struct page, hash_elem);
		enum vm_type src_type = src_page->operations->type;
		if (src_type == VM_UNINIT)
		{
			vm_alloc_page_with_initializer(
				src_page->uninit.type,
				src_page->va,
				src_page->writable,
				src_page->uninit.init,
				src_page->uninit.aux);
		}
		else
		{
			if (vm_alloc_page(src_type, src_page->va, src_page->writable) && vm_claim_page(src_page->va))
			{
				struct page *dst_page = spt_find_page(dst, src_page->va);
				memcpy(dst_page->frame->kva, src_page->frame->kva, PGSIZE);
			}
		}
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	hash_clear(&spt->hash_table, hash_page_destroy);
}

static void hash_page_destroy(struct hash_elem *e, void *aux)
{
	struct page *page = hash_entry(e, struct page, hash_elem);

	if (page->frame)
	{
		free(page->frame);
	}
	destroy(page);
	free(page);
}
