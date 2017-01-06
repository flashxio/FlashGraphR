#ifndef __ASSOCIATIVE_CACHE_H__
#define __ASSOCIATIVE_CACHE_H__

/*
 * Copyright 2014 Open Connectome Project (http://openconnecto.me)
 * Written by Da Zheng (zhengda1936@gmail.com)
 *
 * This file is part of SAFSlib.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <pthread.h>
#include <math.h>
#ifdef USE_NUMA
#include <numa.h>
#endif

#include <vector>
#include <memory>

#include "cache.h"
#include "concurrency.h"
#include "container.h"
#include "parameters.h"
#include "shadow_cell.h"
#include "safs_exception.h"
#include "comm_exception.h"
#include "compute_stat.h"

namespace safs
{

const int CACHE_LINE = 128;

/**
 * This data structure is to contain page data structures in the hash cell.
 * It has space large enough for maximal CELL_SIZE, but only some of them
 * are used. The actual number of pages in the data structure varies,
 * and has a minimal limit.
 */
template<class T>
class page_cell
{
	// to the point where we can evict a page in the buffer
	unsigned char idx;
	unsigned char num_pages;
	// There are gaps in the `buf' array but we expose a virtual array without
	// gaps, so this mapping is to help remove the gaps in the physical array.
	// The number of elements in `maps' is `num_pages'.
	unsigned char maps[CELL_SIZE];

	T buf[CELL_SIZE];			// a circular buffer to keep pages.

public:
	/*
	 * @size: the size of the page buffer
	 * @page_buf: the offset of the page array in the global page cache.
	 */
	page_cell() {
		idx = 0;
		num_pages = 0;
		memset(maps, 0, sizeof(maps));
	}

	bool contain(T *pg) const {
		return pg >= buf && pg < buf + num_pages;
	}

	void set_pages(char *pages[], int num, int node_id);

	void add_pages(char *pages[], int num, int node_id);

	void inject_pages(T pages[], int npages);

	/**
	 * Steal up to `npages' pages from the buffer.
	 */
	void steal_pages(T pages[], int &npages);

	/**
	 * Steal the specified number of pages and the pages are
	 * stored in the array.
	 */
	void steal_page(T *pg, bool rebuild = true) {
		assert(pg->get_ref() == 0);
		num_pages--;
		if (rebuild)
			rebuild_map();
	}

	void rebuild_map();

	unsigned int get_num_pages() const {
		return num_pages;
	}

	/**
	 * return an empty page.
	 * I expected the page will be filled with data,
	 * so I change the begin and end index of the circular buffer.
	 */
	T *get_empty_page() {
		T *ret;
		// the condition check in the while loop should be false
		// most of time, and it only iterates once.
		while (idx >= num_pages)
			idx -= num_pages;
		ret = get_page(idx);
		idx++;
		return ret;
	}

	/**
	 * return a page pointed by the iterator or after the iterator.
	 */
	T *get_page(int i) {
		int real_idx = maps[i];
		T *ret = &buf[real_idx];
		assert(ret->get_data());
		return ret;
	}

	const T *get_page(int i) const {
		int real_idx = maps[i];
		const T *ret = &buf[real_idx];
		assert(ret->get_data());
		return ret;
	}

	int get_idx(T *page) const {
		int idx = page - buf;
		assert (idx >= 0 && idx < num_pages);
		return idx;
	}

	void scale_down_hits() {
		for (int i = 0; i < num_pages; i++) {
			T *pg = get_page(i);
			pg->set_hits(pg->get_hits() / 2);
		}
	}

	/* For test. */
	void sanity_check() const;
	int get_num_used_pages() const;
};

class eviction_policy
{
public:
	// It predicts which pages are to be evicted.
	int predict_evicted_pages(page_cell<thread_safe_page> &buf,
			int num_pages, int set_flags, int clear_flags,
			std::map<off_t, thread_safe_page *> &pages) {
		throw unsupported_exception();
	}
	thread_safe_page *evict_page(page_cell<thread_safe_page> &buf);
	void access_page(thread_safe_page *pg,
			page_cell<thread_safe_page> &buf) {
		// We don't need to do anything if a page is accessed for many policies.
	}
};

class LRU_eviction_policy: public eviction_policy
{
	std::vector<int> pos_vec;
public:
	thread_safe_page *evict_page(page_cell<thread_safe_page> &buf);
	void access_page(thread_safe_page *pg,
			page_cell<thread_safe_page> &buf);
};

class clock_eviction_policy: public eviction_policy
{
	unsigned int clock_head;
public:
	clock_eviction_policy() {
		clock_head = 0;
	}

	thread_safe_page *evict_page(page_cell<thread_safe_page> &buf);
};

class gclock_eviction_policy: public eviction_policy
{
	unsigned int clock_head;
public:
	gclock_eviction_policy() {
		clock_head = 0;
	}

	thread_safe_page *evict_page(page_cell<thread_safe_page> &buf);
	int predict_evicted_pages(page_cell<thread_safe_page> &buf,
			int num_pages, int set_flags, int clear_flags,
			std::map<off_t, thread_safe_page *> &pages);
	void assign_flush_scores(page_cell<thread_safe_page> &buf);
};

class LFU_eviction_policy: public eviction_policy
{
public:
	thread_safe_page *evict_page(page_cell<thread_safe_page> &buf);
};

class FIFO_eviction_policy: public eviction_policy
{
public:
	thread_safe_page *evict_page(page_cell<thread_safe_page> &buf);
};

class associative_cache;

class hash_cell
{
	enum {
		CELL_OVERFLOW,
		IN_QUEUE,
	};
	// It's actually a virtual index of the cell on the hash table.
	int hash;
	atomic_flags<int> flags;

	spin_lock _lock;
	page_cell<thread_safe_page> buf;
	associative_cache *table;
#ifdef USE_LRU
	LRU_eviction_policy policy;
#elif defined USE_LFU
	LFU_eviction_policy policy;
#elif defined USE_FIFO
	FIFO_eviction_policy policy;
#elif defined USE_CLOCK
	clock_eviction_policy policy;
#elif defined USE_GCLOCK
	gclock_eviction_policy policy;
#endif
#ifdef USE_SHADOW_PAGE
	clock_shadow_cell shadow;
#endif

	long num_accesses;
	long num_evictions;

	thread_safe_page *get_empty_page();

	void init() {
		table = NULL;
		hash = -1;
		num_accesses = 0;
		num_evictions = 0;
	}

	hash_cell() {
		init();
	}

	~hash_cell() {
	}

public:
	static hash_cell *create_array(int node_id, int num) {
		assert(node_id >= 0);
#ifdef USE_NUMA
		void *addr = numa_alloc_onnode(sizeof(hash_cell) * num, node_id);
#else
		void *addr = malloc_aligned(sizeof(hash_cell) * num, PAGE_SIZE);
#endif
		hash_cell *cells = (hash_cell *) addr;
		for (int i = 0; i < num; i++)
			new(&cells[i]) hash_cell();
		return cells;
	}

	static void destroy_array(hash_cell *cells, int num) {
		for (int i = 0; i < num; i++)
			cells[i].~hash_cell();
#ifdef USE_NUMA
		numa_free(cells, sizeof(*cells) * num);
#else
		free(cells);
#endif
	}

	void init(associative_cache *cache, long hash, bool get_pages);

	void add_pages(char *pages[], int num);
	int add_pages_to_min(char *pages[], int num);

	void rebalance(hash_cell *cell);

	page *search(const page_id_t &pg_id, page_id_t &old_id);
	page *search(const page_id_t &pg_id);

	bool contain(thread_safe_page *pg) const {
		return buf.contain(pg);
	}

	/**
	 * this is to rehash the pages in the current cell
	 * to the cell in the parameter.
	 */
	void rehash(hash_cell *cell);
	/**
	 * Merge two cells and put all pages in the current cell.
	 * The other cell will contain no pages.
	 */
	void merge(hash_cell *cell);
	/**
	 * Steal pages from the cell, possibly the one to be evicted
	 * by the eviction policy. The page can't be referenced and dirty.
	 */
	void steal_pages(char *pages[], int &npages);

	/**
	 * This method returns a specified number of pages that contains
	 * set flags and don't contain clear flags.
	 */
	void get_pages(int num_pages, char set_flags, char clear_flags,
			std::map<off_t, thread_safe_page *> &pages);

	/**
	 * Predict the pages that are about to be evicted by the eviction policy,
	 * and return them to the invoker.
	 * The selected pages must have the set flags and don't have clear flags.
	 */
	void predict_evicted_pages(int num_pages, char set_flags, char clear_flags,
			std::map<off_t, thread_safe_page *> &pages);

	long get_hash() const {
		return hash;
	}

	bool is_in_queue() const {
		return flags.test_flag(IN_QUEUE);
	}

	bool set_in_queue(bool v) {
		if (v)
			return flags.set_flag(IN_QUEUE);
		else
			return flags.clear_flag(IN_QUEUE);
	}

	bool is_deficit() const {
		return buf.get_num_pages() < (unsigned) CELL_MIN_NUM_PAGES;
	}

	bool is_full() const {
		return buf.get_num_pages() == (unsigned) CELL_SIZE;
	}

	int num_pages(char set_flags, char clear_flags);
	int get_num_pages() const {
		return buf.get_num_pages();
	}

	/* For test. */
	void sanity_check();
	bool is_referenced() const {
		for (unsigned i = 0; i < buf.get_num_pages(); i++)
			if (buf.get_page(i)->get_ref() > 0)
				return true;
		return false;
	}

	long get_num_accesses() const {
		return num_accesses;
	}

	long get_num_evictions() const {
		return num_evictions;
	}

	void print_cell();
};

class dirty_page_flusher;
class memory_manager;

class associative_cache: public page_cache
{
	enum {
		TABLE_EXPANDING,
	};
	/* 
	 * this table contains cell arrays.
	 * each array contains N cells;
	 */
	std::vector<hash_cell*> cells_table;
	/*
	 * The index points to the cell that will expand next time.
	 */
	unsigned int expand_cell_idx;
	// The number of pages in the cache.
	// Cells may have different numbers of pages.
	atomic_integer cache_npages;
	int offset_factor;
	
	seq_lock table_lock;
	atomic_flags<int> flags;
	/* the initial number of cells in the table. */
	int init_ncells;

	memory_manager *manager;
	int node_id;

	bool expandable;
	int height;
	/* used for linear hashing */
	int level;
	int split;

	std::unique_ptr<dirty_page_flusher> _flusher;
	pthread_mutex_t init_mutex;

	associative_cache(long cache_size, long max_cache_size, int node_id,
			int offset_factor, int _max_num_pending_flush,
			bool expandable = false);

	void create_flusher(std::shared_ptr<io_interface> io, page_cache *global_cache);

	memory_manager *get_manager() {
		return manager;
	}

public:
	// The number of pages in the I/O queue waiting to be flushed.
	atomic_integer num_pending_flush;
	const int max_num_pending_flush;
	stat_max<long> recorded_max_num_pending;
	stat_mean<long> avg_num_pending;
#ifdef DEBUG
	atomic_integer num_dirty_pages;
#endif

	static page_cache::ptr create(long cache_size, long max_cache_size,
			int node_id, int offset_factor, int _max_num_pending_flush,
			bool expandable = false) {
		assert(node_id >= 0);
		return page_cache::ptr(new associative_cache(cache_size, max_cache_size,
				node_id, offset_factor, _max_num_pending_flush, expandable));
	}

	~associative_cache();

	int get_node_id() const {
		return node_id;
	}

	/* the hash function used for the current level. */
	int hash(const page_id_t &pg_id) {
		// The offset of pages in this cache may all be a multiple of
		// some value, so when we hash a page to a page set, we need
		// to adjust the offset.
		int num_cells = (init_ncells * (long) (1 << level));
		return universal_hash(pg_id.get_offset() / PAGE_SIZE / offset_factor
				+ pg_id.get_file_id(), num_cells);
	}

	/* the hash function used for the next level. */
	int hash1(const page_id_t &pg_id) {
		int num_cells = (init_ncells * (long) (1 << (level + 1)));
		return universal_hash(pg_id.get_offset() / PAGE_SIZE / offset_factor
				+ pg_id.get_file_id(), num_cells);
	}

	int hash1_locked(const page_id_t &pg_id) {
		unsigned long count;
		int ret;
		do {
			table_lock.read_lock(count);
			int num_cells = (init_ncells * (long) (1 << (level + 1)));
			ret = universal_hash(pg_id.get_offset() / PAGE_SIZE / offset_factor
					+ pg_id.get_file_id(), num_cells);
		} while (!table_lock.read_unlock(count));
		return ret;
	}

	/**
	 * This method searches for the specified page. If the page doesn't exist,
	 * it tries to evict a page. Therefore, it also triggers some code of
	 * maintaining eviction policy.
	 */
	page *search(const page_id_t &pg_id, page_id_t &old_id);
	/**
	 * This method just searches for the specified page, nothing more.
	 * So if the request isn't issued from the workload, and we don't need
	 * evict a page if the specified page doesn't exist, we should use
	 * this method.
	 */
	page *search(const page_id_t &pg_id);

	/**
	 * Expand the cache by `npages' pages, and return the actual number
	 * of pages that the cache has been expanded.
	 */
	int expand(int npages);
	bool shrink(int npages, char *pages[]);

	void print_cell(off_t off) {
		get_cell(off)->print_cell();
	}

	/**
	 * The size of allocated pages in the cache.
	 */
	long size() {
		return ((long) cache_npages.get()) * PAGE_SIZE;
	}

	hash_cell *get_cell(unsigned int global_idx) const {
		unsigned int cells_idx = global_idx / init_ncells;
		int idx = global_idx % init_ncells;
		assert(cells_idx < cells_table.size());
		hash_cell *cells = cells_table[cells_idx];
		if (cells)
			return &cells[idx];
		else
			return NULL;
	}

	hash_cell *get_cell_offset(const page_id_t &pg_id) {
		int global_idx;
		unsigned long count;
		hash_cell *cell = NULL;
		do {
			table_lock.read_lock(count);
			global_idx = hash(pg_id);
			if (global_idx < split)
				global_idx = hash1(pg_id);
			cell = get_cell(global_idx);
		} while (!table_lock.read_unlock(count));
		assert(cell);
		return cell;
	}

	bool is_expandable() const {
		return expandable;
	}

	/* Methods for flushing dirty pages. */

	void mark_dirty_pages(thread_safe_page *pages[], int num, io_interface &);
	virtual int flush_dirty_pages(page_filter *filter, int max_num);

	hash_cell *get_prev_cell(hash_cell *cell);
	hash_cell *get_next_cell(hash_cell *cell);

	int get_num_cells() const {
		return (1 << level) * init_ncells + split;
	}

	/* For test */
	int get_num_used_pages() const;
	virtual void sanity_check() const;

	int get_num_dirty_pages() const;

	virtual void init(std::shared_ptr<io_interface> underlying);

	friend class hash_cell;
#ifdef STATISTICS
	void print_stat() const {
		printf("SA-cache on node %d: ncells: %d, height: %d, split: %d, dirty pages: %d\n",
				node_id, get_num_cells(), height, split, get_num_dirty_pages());
		printf("\tmax pending flushes: %ld, avg: %ld, remaining pending: %d\n",
				recorded_max_num_pending.get(), (long) avg_num_pending.get(),
				num_pending_flush.get());
#ifdef DETAILED_STATISTICS
		for (int i = 0; i < get_num_cells(); i++)
			printf("cell %d: %ld accesses, %ld evictions\n", i,
					get_cell(i)->get_num_accesses(),
					get_cell(i)->get_num_evictions());
#endif
	}
#endif
};

}

#endif
