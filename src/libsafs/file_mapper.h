#ifndef __FILE_BLOCK_MAPPER_H__
#define __FILE_BLOCK_MAPPER_H__

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

#include <vector>
#include <string>

#include "common.h"
#include "parameters.h"
#include "concurrency.h"
#include "safs_exception.h"
#include "comm_exception.h"
#include "safs_file.h"

namespace safs
{

const int FILE_CONST_A = 31;
const int FILE_CONST_P = 191;

struct block_identifier
{
	int idx;		// identify the file where the block is.
	off_t off;		// the location (in pages) in the file.
};

/*
 * The goal of this class is to map a chunk of data in an SAFS to its physical
 * location in a Linux file on an SSD. Each SAFS file has its own mapping.
 * It first maps a chunk of data in an SAFS to a location in a logical RAID
 * stripe and the location is identified by `block_identifier'.
 * The location in the stripe is further mapped to the physical location
 * in a file on an SSD.
 *
 * In practice, remote_io first uses map2file() and get_disk_id() to identify
 * the I/O thread that owns the disk where a chunk of data is stored.
 * Then an I/O thread locates the physical location of the data with
 * `logical_file_partition', which works with `file_mapper'.
 */
class file_mapper
{
	static atomic_integer file_id_gen;
	int file_id;
	std::vector<part_file_info> files;
	std::string file_name;
protected:
	const std::vector<part_file_info> &get_files() const {
		return files;
	}
public:
	const int STRIPE_BLOCK_SIZE;
public:
	typedef std::shared_ptr<file_mapper> ptr;
	typedef std::shared_ptr<const file_mapper> const_ptr;

	static ptr create(const safs_header &header,
			const std::vector<part_file_info> &files,
			const std::string &file_name);

	file_mapper(const std::string &name, const std::vector<part_file_info> &files,
			int block_size): STRIPE_BLOCK_SIZE(block_size) {
		this->file_name = name;
		ASSERT_TRUE(block_size > 0);
		this->files = files;
		file_id = file_id_gen.inc(1);
	}

	virtual ~file_mapper() {
	}

	int get_file_id() const {
		return file_id;
	}

	/*
	 * Return the name of the SAFS file.
	 */
	const std::string &get_name() const {
		return file_name;
	}

	/*
	 * The name of the physical file that stores the chunk `idx' in a stripe.
	 */
	std::string get_file_name(int idx) const {
		return files[idx].get_file_name();
	}

	/*
	 * The NUMA node of the disk where the chunk `idx' in a stripe locates.
	 */
	int get_file_node_id(int idx) const {
		return files[idx].get_node_id();
	}

	/*
	 * The disk Id in the RAID where the chunk `idx' in a stripe locates.
	 */
	int get_disk_id(int idx) const {
		return files[idx].get_disk_id();
	}

	int get_num_files() const {
		return (int) files.size();
	}

	/*
	 * This maps a chunk of data in the SAFS file to the location of a RAID
	 * stripe. It doesn't identify the physical location of the chunk of data.
	 */
	virtual void map(off_t, struct block_identifier &) const = 0;
	virtual int map2file(off_t) const = 0;

	// Given the SAFS file size, this calculates physical file sizes in
	// each disk. `size' is given in the number of pages.
	virtual std::vector<size_t> get_size_per_disk(size_t size) const;

	virtual file_mapper *clone() = 0;
};

int gen_RAID_rand_start(int num_files);

class RAID0_mapper: public file_mapper
{
	static int rand_start;
public:
	RAID0_mapper(const std::string &name, const std::vector<part_file_info> &files,
			int block_size): file_mapper(name, files, block_size) {
	}

	virtual void map(off_t off, struct block_identifier &bid) const {
		int idx_in_block = off % STRIPE_BLOCK_SIZE;
		off_t block_idx = off / STRIPE_BLOCK_SIZE;
		bid.idx = (int) ((block_idx + rand_start) % get_num_files());
		bid.off = block_idx / get_num_files() * STRIPE_BLOCK_SIZE
			+ idx_in_block;
	}

	virtual int map2file(off_t off) const {
		return (int) (((off / STRIPE_BLOCK_SIZE) + rand_start) % get_num_files());
	}

	virtual file_mapper *clone() {
		return new RAID0_mapper(get_name(), get_files(), STRIPE_BLOCK_SIZE);
	}
};

class RAID5_mapper: public file_mapper
{
	static int rand_start;
public:
	RAID5_mapper(const std::string &name, const std::vector<part_file_info> &files,
			int block_size): file_mapper(name, files, block_size) {
	}

	virtual void map(off_t off, struct block_identifier &bid) const {
		int idx_in_block = off % STRIPE_BLOCK_SIZE;
		off_t block_idx = off / STRIPE_BLOCK_SIZE;
		bid.idx = (int) (block_idx % get_num_files());
		bid.off = block_idx / get_num_files() * STRIPE_BLOCK_SIZE
			+ idx_in_block;
		int shift = (int) ((block_idx / get_num_files()) % get_num_files());
		bid.idx = (bid.idx + shift + rand_start) % get_num_files();
	}

	virtual int map2file(off_t off) const {
		off_t block_idx = off / STRIPE_BLOCK_SIZE;
		int shift = (int) ((block_idx / get_num_files()) % get_num_files());
		return (int) ((block_idx % get_num_files() + shift
					+ rand_start) % get_num_files());
	}

	virtual file_mapper *clone() {
		return new RAID5_mapper(get_name(), get_files(), STRIPE_BLOCK_SIZE);
	}
};

class hash_mapper: public file_mapper
{
	static const int CONST_A = FILE_CONST_A;
	static const int CONST_P = FILE_CONST_P;
	const int P_MOD_N;

	int cycle_size_in_bucket(int idx) const {
		return idx < P_MOD_N ? (CONST_P / get_num_files()
				+ 1) : (CONST_P / get_num_files());
	}
public:
	hash_mapper(const std::string &name, const std::vector<part_file_info> &files,
			int block_size): file_mapper(name, files, block_size), P_MOD_N(
				CONST_P % files.size()) {
	}

	virtual void map(off_t off, struct block_identifier &bid) const {
		int idx_in_block = off % STRIPE_BLOCK_SIZE;
		off_t block_idx = off / STRIPE_BLOCK_SIZE;
		off_t p_idx = (CONST_A * block_idx) % CONST_P;
		bid.idx = p_idx % get_num_files();
		off_t cycle_idx = block_idx / CONST_P;
		int cycle_len_in_bucket = cycle_size_in_bucket(bid.idx);
		// length of all previous cycles
		bid.off = (cycle_idx * cycle_len_in_bucket
				// location in the current cycle
				+ p_idx / get_num_files())
			* STRIPE_BLOCK_SIZE + idx_in_block;
	}

	virtual int map2file(off_t off) const {
		off_t block_idx = off / STRIPE_BLOCK_SIZE;
		off_t p_idx = (CONST_A * block_idx) % CONST_P;
		return p_idx % get_num_files();
	}

	virtual std::vector<size_t> get_size_per_disk(size_t size) const;

	virtual file_mapper *clone() {
		return new hash_mapper(get_name(), get_files(), STRIPE_BLOCK_SIZE);
	}
};

}

#endif
