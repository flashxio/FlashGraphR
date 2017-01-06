#ifndef __EM_OBJECT_H__
#define __EM_OBJECT_H__

/*
 * Copyright 2014 Open Connectome Project (http://openconnecto.me)
 * Written by Da Zheng (zhengda1936@gmail.com)
 *
 * This file is part of FlashMatrix.
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
#include <unordered_map>

#include "safs_file.h"
#include "io_interface.h"
#include "local_vec_store.h"
#include "mem_worker_thread.h"

namespace fm
{

namespace detail
{

class EM_object
{
public:
	class file_holder {
		bool persistent;
		std::string file_name;
		file_holder(const std::string &name, bool persistent) {
			this->file_name = name;
			this->persistent = persistent;
		}
	public:
		typedef std::shared_ptr<file_holder> ptr;
		static ptr create_temp(const std::string &name, size_t num_bytes,
				safs::safs_file_group::ptr group);
		static ptr create(const std::string &name);

		~file_holder();
		std::string get_name() const {
			return file_name;
		}
		bool set_persistent(const std::string &new_name);
		void unset_persistent();
		bool is_persistent() const {
			return persistent;
		}
	};

	class io_set {
		safs::file_io_factory::shared_ptr factory;
		// This keeps an I/O instance for each thread.
		std::unordered_map<thread *, safs::io_interface::ptr> thread_ios;
		pthread_key_t io_key;
		pthread_spinlock_t io_lock;
	public:
		typedef std::shared_ptr<io_set> ptr;
		io_set(safs::file_io_factory::shared_ptr factory);
		~io_set();

		safs::io_interface::ptr create_io();
		// This returns the I/O instance for the curr thread.
		safs::io_interface &get_curr_io() const;
		// Test if the current thread has an I/O instance for the vector.
		bool has_io() const;
	};

	typedef std::shared_ptr<EM_object> ptr;
	typedef std::shared_ptr<const EM_object> const_ptr;
	/*
	 * This creates an I/O instance for the current thread.
	 */
	virtual std::vector<safs::io_interface::ptr> create_ios() const = 0;

	/*
	 * Make the EM object spersistent on SAFS.
	 */
	virtual bool set_persistent(const std::string &name) const {
		return true;
	}
	virtual void unset_persistent() const {
	}
};

template<class T>
T round_ele(T val, size_t alignment, size_t ele_size)
{
	assert(alignment % ele_size == 0);
	alignment = alignment / ele_size;
	return ROUND(val, alignment);
}

template<class T>
T roundup_ele(T val, size_t alignment, size_t ele_size)
{
	assert(alignment % ele_size == 0);
	alignment = alignment / ele_size;
	return ROUNDUP(val, alignment);
}

/*
 * This runs on the portion of the data in a data container when the portion
 * of data is available in memory.
 */
class portion_compute
{
public:
	typedef std::shared_ptr<portion_compute> ptr;

	virtual ~portion_compute() {
	}

	virtual void run(char *buf, size_t size) = 0;
};

class portion_callback: public safs::callback
{
	std::unordered_map<long, std::vector<portion_compute::ptr> > computes;
public:
	typedef std::shared_ptr<portion_callback> ptr;

	static long get_portion_key(const safs::io_request &req) {
		return (long) req.get_buf();
	}

	virtual ~portion_callback() {
		assert(computes.empty());
	}

	bool has_callback() const {
		return !computes.empty();
	}

	bool has_callback(const safs::io_request &req) const {
		auto it = computes.find(get_portion_key(req));
		return it != computes.end();
	}
	void add(const safs::io_request &req, portion_compute::ptr compute) {
		add(get_portion_key(req), compute);
	}
	void add(long key, portion_compute::ptr compute);
	virtual int invoke(safs::io_request *reqs[], int num);
};

class sync_read_compute: public portion_compute
{
	bool &ready;
public:
	sync_read_compute(bool &_ready): ready(_ready) {
	}
	virtual void run(char *buf, size_t size) {
		ready = true;
	}
};

/*
 * This task dispatcher enables 1D partitioning on the object.
 */
class EM_portion_dispatcher: public task_dispatcher
{
	size_t tot_len;
	off_t portion_idx;
	pthread_spinlock_t lock;
	size_t portion_size;
	// This is the number of portions.
	// When the number of remaining portions is smaller than this, we return
	// one portion at a time. Otherwise, we can return the number of portions
	// specified by the user.
	// The idea is that when a task is larger, we can issue larger I/O requests
	// to achieve better I/O performance. But larger tasks cause load
	// imbalancing. As such, we dynamically adapt the task size to achieve both.
	size_t balance_thres;
	// This indicates the number of portions in a task.
	size_t num_portions_task;
public:
	EM_portion_dispatcher(size_t tot_len, size_t portion_size,
			size_t balance_thres = 0, size_t num_portions_task = 1) {
		pthread_spin_init(&lock, PTHREAD_PROCESS_PRIVATE);
		portion_idx = 0;
		this->tot_len = tot_len;
		this->portion_size = portion_size;
		this->balance_thres = balance_thres;
		this->num_portions_task = num_portions_task;
	}

	size_t get_task_size() const {
		return num_portions_task;
	}

	size_t get_portion_size() const {
		return portion_size;
	}

	virtual bool issue_task();

	virtual void create_task(off_t global_start, size_t length) = 0;
};

}

}

#endif
