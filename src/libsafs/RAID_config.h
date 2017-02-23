#ifndef __RAID_CONFIG_H__
#define __RAID_CONFIG_H__

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
#include <set>
#include <memory>

#include "safs_file.h"

namespace safs
{

class file_mapper;

enum {
	RAID0,
	RAID5,
	HASH,
};

class RAID_config
{
	/*
	 * These are default values for the RAID config.
	 * A per-file config can overwrite these parameters.
	 */
	int RAID_mapping_option;
	int RAID_block_size;

	std::vector<part_file_info> root_paths;
public:
	typedef std::shared_ptr<RAID_config> ptr;

	static ptr create(const std::string &conf_file, int mapping_option,
			int block_size);

	/**
	 * Create a file mapper for the RAID directories.
	 */
	std::shared_ptr<file_mapper> create_file_mapper() const;
	/**
	 * Create a file mapper for a file in the RAID.
	 */
	std::shared_ptr<file_mapper> create_file_mapper(
			const std::string &file_name) const;

	/**
	 * This returns the nodes where the RAID attaches to.
	 */
	std::set<int> get_node_ids() const;

	const part_file_info &get_disk(int idx) const {
		return root_paths[idx];
	}

	std::vector<part_file_info> get_disks() const {
		return root_paths;
	}

	int get_num_disks() const {
		return root_paths.size();
	}

	int get_mapping_option() const {
		return RAID_mapping_option;
	}

	int get_block_size() const {
		return RAID_block_size;
	}
};

}

#endif
