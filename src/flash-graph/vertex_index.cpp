/*
 * Copyright 2014 Open Connectome Project (http://openconnecto.me)
 * Written by Da Zheng (zhengda1936@gmail.com)
 *
 * This file is part of FlashGraph.
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

#include <boost/format.hpp>

#include "log.h"
#include "io_interface.h"
#include "safs_file.h"

#include "vertex_compute.h"
#include "vertex_index.h"
#include "vertex_index_reader.h"
#include "vertex_index_constructor.h"
#include "graph_exception.h"

using namespace safs;

namespace fg
{

const size_t compressed_vertex_entry::ENTRY_SIZE;
const size_t cundirected_vertex_index::ENTRY_SIZE;
const size_t cdirected_vertex_index::ENTRY_SIZE;

static void verify_index(vertex_index::ptr idx)
{
	if (!idx->get_graph_header().is_graph_file()
			|| !idx->get_graph_header().is_right_version())
		throw wrong_format("wrong index file or format version");

	bool verify_format;
	if (idx->get_graph_header().is_directed_graph()) {
		if (idx->is_compressed())
			verify_format = cdirected_vertex_index::cast(idx)->verify();
		else
			verify_format = directed_vertex_index::cast(idx)->verify();
	}
	else {
		if (idx->is_compressed())
			verify_format = cundirected_vertex_index::cast(idx)->verify();
		else
			verify_format = undirected_vertex_index::cast(idx)->verify();
	}
	if (!verify_format)
		throw wrong_format("wrong index format");
}

size_t vertex_index::get_index_size() const
{
	// compressed index for a directed graph
	if (is_compressed() && get_graph_header().is_directed_graph()) {
		return ((cdirected_vertex_index *) this)->cal_index_size();
	}
	// compressed index for an undirected graph
	else if (is_compressed() && !get_graph_header().is_directed_graph()) {
		return ((cundirected_vertex_index *) this)->cal_index_size();
	}
	// original index for a directed graph
	else if (!is_compressed() && get_graph_header().is_directed_graph()) {
		return ((directed_vertex_index *) this)->cal_index_size();
	}
	// original index for an undirected graph
	else {
		return ((undirected_vertex_index *) this)->cal_index_size();
	}
}

vertex_index::ptr vertex_index::load(const std::string &index_file)
{
	native_file local_f(index_file);
	if (!local_f.exist())
		throw io_exception(boost::str(boost::format(
						"the index file %1% doesn't exist") % index_file));
	ssize_t size = local_f.get_size();
	if (size <= 0 || (size_t) size < sizeof(vertex_index)) {
		throw wrong_format("the index file is smaller than expected");
	}
	char *buf = (char *) malloc(size);
	if (buf == NULL)
		throw oom_exception("can't allocate memory for vertex index");
	FILE *fd = fopen(index_file.c_str(), "r");
	if (fd == NULL)
		throw io_exception(std::string("can't open ") + index_file);
	if (fread(buf, size, 1, fd) != 1) {
		fclose(fd);
		throw io_exception(std::string("can't read from ") + index_file);
	}
	fclose(fd);

	vertex_index::ptr idx((vertex_index *) buf, destroy_index());
	if ((size_t) size < idx->get_index_size())
		throw wrong_format("the index file is smaller than expected");
	verify_index(idx);

	BOOST_LOG_TRIVIAL(info)
		<< boost::format("load vertex index: file size: %1%, index size: %2%")
		% size % idx->get_index_size();
	return idx;
}

vertex_index::ptr vertex_index::safs_load(const std::string &index_file)
{
	const int INDEX_HEADER_SIZE = PAGE_SIZE * 2;
	const int READ_SIZE = 100 * 1024 * 1024;

	safs_file safs_f(get_sys_RAID_conf(), index_file);
	if (!safs_f.exist())
		throw io_exception(boost::str(boost::format(
						"the index file %1% doesn't exist") % index_file));

	// Right now only the cached I/O can support async I/O
	file_io_factory::shared_ptr factory = create_io_factory(index_file,
			REMOTE_ACCESS);
	if (factory->get_file_size() < INDEX_HEADER_SIZE)
		throw wrong_format("the index file is smaller than expected");
	io_interface::ptr io = create_io(factory, thread::get_curr_thread());

	// Get the header of the index.
	char *tmp = NULL;
	int ret = posix_memalign((void **) &tmp, PAGE_SIZE, INDEX_HEADER_SIZE);
	if (ret != 0)
		throw oom_exception("can't allocate memory for vertex index");
	data_loc_t loc(factory->get_file_id(), 0);
	io_request req(tmp, loc, INDEX_HEADER_SIZE, READ);
	io->access(&req, 1);
	io->wait4complete(1);
	vertex_index *index = (vertex_index *) tmp;
	if (!index->get_graph_header().is_graph_file()
			|| !index->get_graph_header().is_right_version())
		throw wrong_format("wrong index file or format version");

	// Initialize the buffer for containing the index.
	size_t index_size = index->get_index_size();
	if (factory->get_file_size() < (ssize_t) index_size)
		throw wrong_format("the index file is smaller than expected");
	char *buf = NULL;
	BOOST_LOG_TRIVIAL(info)
		<< boost::format("allocate %1% bytes for vertex index") % index_size;
	ret = posix_memalign((void **) &buf, PAGE_SIZE,
			std::max(index_size, (size_t) INDEX_HEADER_SIZE));
	if (ret != 0)
		throw oom_exception("can't allocate memory for vertex index");
	off_t off = 0;
	memcpy(buf, tmp, INDEX_HEADER_SIZE);
	off += INDEX_HEADER_SIZE;
	free(tmp);

	// Read the index to the memory.
	size_t aligned_index_size = ROUND_PAGE(index_size);
	while ((size_t) off < aligned_index_size) {
		assert(off % PAGE_SIZE == 0);
		size_t size = min(READ_SIZE, aligned_index_size - off);
		data_loc_t loc(factory->get_file_id(), off);
		io_request req(buf + off, loc, size, READ);
		io->access(&req, 1);
		off += size;
		if (io->num_pending_ios() > 100)
			io->wait4complete(io->num_pending_ios() / 10);
	}
	io->wait4complete(io->num_pending_ios());

	// Read the last page.
	// The data may only occupy part of the page.
	if (aligned_index_size < index_size) {
		char *tmp = NULL;
		BOOST_VERIFY(posix_memalign((void **) &tmp, PAGE_SIZE, PAGE_SIZE) == 0);
		data_loc_t loc(factory->get_file_id(), aligned_index_size);
		io_request req(tmp, loc, PAGE_SIZE, READ);
		io->access(&req, 1);
		io->wait4complete(1);
		memcpy(buf + aligned_index_size, tmp, index_size - aligned_index_size);
		free(tmp);
	}

	vertex_index::ptr index_ptr((vertex_index *) buf, destroy_index());
	verify_index(index_ptr);
	return index_ptr;
}

compressed_directed_vertex_entry::compressed_directed_vertex_entry(
		const directed_vertex_entry offs[], size_t edge_data_size, size_t num)
{
	start_offs = offs[0];
	size_t num_vertices = std::min(num - 1, ENTRY_SIZE);
	for (size_t i = 0; i < num_vertices; i++) {
		vsize_t num_in_edges = ext_mem_undirected_vertex::vsize2num_edges(
					offs[i + 1].get_in_off() - offs[i].get_in_off(),
					edge_data_size);
		if (num_in_edges < LARGE_VERTEX_SIZE)
			edges[i].first = num_in_edges;
		else
			edges[i].first = LARGE_VERTEX_SIZE;

		vsize_t num_out_edges = ext_mem_undirected_vertex::vsize2num_edges(
				offs[i + 1].get_out_off() - offs[i].get_out_off(),
				edge_data_size);
		if (num_out_edges < LARGE_VERTEX_SIZE)
			edges[i].second = num_out_edges;
		else
			edges[i].second = LARGE_VERTEX_SIZE;
	}
	for (size_t i = num_vertices; i < ENTRY_SIZE; i++) {
		edges[i].first = edges[i].second = 0;
	}
}

compressed_directed_vertex_entry::compressed_directed_vertex_entry(
		const directed_vertex_entry offs, const vsize_t num_in_edges[],
		const vsize_t num_out_edges[], size_t num_vertices)
{
	start_offs = offs;
	for (size_t i = 0; i < num_vertices; i++) {
		if (num_in_edges[i] < LARGE_VERTEX_SIZE)
			edges[i].first = num_in_edges[i];
		else
			edges[i].first = LARGE_VERTEX_SIZE;

		if (num_out_edges[i] < LARGE_VERTEX_SIZE)
			edges[i].second = num_out_edges[i];
		else
			edges[i].second = LARGE_VERTEX_SIZE;
	}
	for (size_t i = num_vertices; i < ENTRY_SIZE; i++) {
		edges[i].first = edges[i].second = 0;
	}
}

compressed_undirected_vertex_entry::compressed_undirected_vertex_entry(
		const vertex_offset offs[], size_t edge_data_size, size_t num)
{
	start_offs = offs[0];
	size_t num_vertices = std::min(num - 1, ENTRY_SIZE);
	for (size_t i = 0; i < num_vertices; i++) {
		vsize_t num_edges = ext_mem_undirected_vertex::vsize2num_edges(
					offs[i + 1].get_off() - offs[i].get_off(),
					edge_data_size);
		if (num_edges < LARGE_VERTEX_SIZE)
			edges[i] = num_edges;
		else
			edges[i] = LARGE_VERTEX_SIZE;
	}
	for (size_t i = num_vertices; i < ENTRY_SIZE; i++) {
		edges[i] = 0;
	}
}

compressed_undirected_vertex_entry::compressed_undirected_vertex_entry(
		const vertex_offset off, const vsize_t num_edges[], size_t num_vertices)
{
	start_offs = off;
	for (size_t i = 0; i < num_vertices; i++) {
		if (num_edges[i] < LARGE_VERTEX_SIZE)
			edges[i] = num_edges[i];
		else
			edges[i] = LARGE_VERTEX_SIZE;
	}
	for (size_t i = num_vertices; i < ENTRY_SIZE; i++)
		edges[i] = 0;
}

void in_mem_cundirected_vertex_index::init(const undirected_vertex_index &index)
{
	BOOST_LOG_TRIVIAL(info) << "init from a regular vertex index";
	index.verify();
	edge_data_size = index.get_graph_header().get_edge_data_size();
	size_t num_entries = index.get_num_entries();
	num_vertices = num_entries - 1;
	entries.resize(ROUNDUP(num_vertices, ENTRY_SIZE) / ENTRY_SIZE);
	for (size_t off = 0; off < num_vertices; off += ENTRY_SIZE) {
		off_t entry_idx = off / ENTRY_SIZE;
		entries[entry_idx] = compressed_undirected_vertex_entry(
					index.get_data() + off, edge_data_size,
					std::min(ENTRY_SIZE + 1, num_entries - off));

		vertex_id_t id = off;
		for (size_t i = 0; i < ENTRY_SIZE; i++) {
			if (entries[entry_idx].is_large_vertex(i)) {
				ext_mem_vertex_info info = index.get_vertex_info(id + i);
				large_vmap.insert(vertex_map_t::value_type(id + i,
							ext_mem_undirected_vertex::vsize2num_edges(
								info.get_size(), edge_data_size)));
			}
		}
	}
}

void in_mem_cundirected_vertex_index::init(const cundirected_vertex_index &index)
{
	struct timeval start, end;
	gettimeofday(&start, NULL);
	BOOST_LOG_TRIVIAL(info) << "init from a compressed vertex index";
	index.verify();
	edge_data_size = index.get_graph_header().get_edge_data_size();
	num_vertices = index.get_graph_header().get_num_vertices();
	entries.insert(entries.end(), index.get_entries(),
			index.get_entries() + index.get_num_entries());

	const large_vertex_t *l_vertex_array = index.get_large_vertices();
	size_t num_large_vertices = index.get_num_large_vertices();
	for (size_t i = 0; i < num_large_vertices; i++) {
		large_vmap.insert(l_vertex_array[i]);
	}

	BOOST_LOG_TRIVIAL(info)
		<< boost::format("There are %1% large vertices") % num_large_vertices;
	gettimeofday(&end, NULL);
	BOOST_LOG_TRIVIAL(info)
		<< boost::format("init in-mem compressed index takes %1% seconds")
		% time_diff(start, end);
}

in_mem_cundirected_vertex_index::in_mem_cundirected_vertex_index(
		vertex_index &index): in_mem_query_vertex_index(
			index.get_graph_header().is_directed_graph(),
			true)
{
	if (index.is_compressed())
		init((const cundirected_vertex_index &) index);
	else
		init((const undirected_vertex_index &) index);
}

const size_t in_mem_cundirected_vertex_index::ENTRY_SIZE;
vertex_offset in_mem_cundirected_vertex_index::get_vertex(vertex_id_t id) const
{
	vertex_offset e = entries[id / ENTRY_SIZE].get_start_offs();
	int off = id % ENTRY_SIZE;
	off_t voff = e.get_off();
	vertex_id_t start_id = id & (~ENTRY_MASK);
	for (int i = 0; i < off; i++) {
		voff += get_size(start_id + i);
	}
	return vertex_offset(voff);
}

void in_mem_cundirected_vertex_index::verify_against(
		undirected_vertex_index &index)
{
#if 0
	index.verify();
	id_range_t range(10, std::min(100UL, index.get_num_vertices()));
	compressed_undirected_index_iterator it(*this, range);
	vertex_id_t id = range.first;
	while (it.has_next()) {
		ext_mem_vertex_info in_info = index.get_vertex_info_in(id);
		ext_mem_vertex_info out_info = index.get_vertex_info_out(id);
		assert(in_info.get_off() == it.get_curr_off());
		assert(out_info.get_off() == it.get_curr_out_off());
		id++;
		it.move_next();
	}
#endif
}

void in_mem_cdirected_vertex_index::init(const directed_vertex_index &index)
{
	BOOST_LOG_TRIVIAL(info) << "init from a regular vertex index";
	index.verify();
	edge_data_size = index.get_graph_header().get_edge_data_size();
	size_t num_entries = index.get_num_entries();
	num_vertices = num_entries - 1;
	entries.resize(ROUNDUP(num_vertices, ENTRY_SIZE) / ENTRY_SIZE);
	for (size_t off = 0; off < num_vertices;
			off += ENTRY_SIZE) {
		off_t entry_idx = off / ENTRY_SIZE;
		entries[entry_idx] = compressed_directed_vertex_entry(
					index.get_data() + off, edge_data_size,
					std::min(ENTRY_SIZE + 1, num_entries - off));

		vertex_id_t id = off;
		for (size_t i = 0; i < ENTRY_SIZE; i++) {
			if (entries[entry_idx].is_large_in_vertex(i)) {
				ext_mem_vertex_info info = index.get_vertex_info_in(id + i);
				large_in_vmap.insert(vertex_map_t::value_type(id + i,
							ext_mem_undirected_vertex::vsize2num_edges(
								info.get_size(), edge_data_size)));
			}
			if (entries[entry_idx].is_large_out_vertex(i)) {
				ext_mem_vertex_info info = index.get_vertex_info_out(id + i);
				large_out_vmap.insert(vertex_map_t::value_type(id + i,
							ext_mem_undirected_vertex::vsize2num_edges(
								info.get_size(), edge_data_size)));
			}
		}
	}
}

void in_mem_cdirected_vertex_index::init(const cdirected_vertex_index &index)
{
	struct timeval start, end;
	gettimeofday(&start, NULL);
	BOOST_LOG_TRIVIAL(info) << "init from a compressed vertex index";
	index.verify();
	edge_data_size = index.get_graph_header().get_edge_data_size();
	num_vertices = index.get_graph_header().get_num_vertices();
	entries.insert(entries.end(), index.get_entries(),
			index.get_entries() + index.get_num_entries());

	const large_vertex_t *l_in_vertex_array = index.get_large_in_vertices();
	size_t num_large_in_vertices = index.get_num_large_in_vertices();
	const large_vertex_t *l_out_vertex_array = index.get_large_out_vertices();
	size_t num_large_out_vertices = index.get_num_large_out_vertices();

	for (size_t i = 0; i < num_large_in_vertices; i++) {
		large_in_vmap.insert(l_in_vertex_array[i]);
	}

	for (size_t i = 0; i < num_large_out_vertices; i++) {
		large_out_vmap.insert(l_out_vertex_array[i]);
	}
	BOOST_LOG_TRIVIAL(info)
		<< boost::format("There are %1% large in-vertices and %2% large out-vertices")
		% num_large_in_vertices % num_large_out_vertices;
	gettimeofday(&end, NULL);
	BOOST_LOG_TRIVIAL(info)
		<< boost::format("init in-mem compressed index takes %1% seconds")
		% time_diff(start, end);
}

in_mem_cdirected_vertex_index::in_mem_cdirected_vertex_index(
		vertex_index &index): in_mem_query_vertex_index(
			index.get_graph_header().is_directed_graph(),
			true)
{
	if (index.is_compressed())
		init((const cdirected_vertex_index &) index);
	else
		init((const directed_vertex_index &) index);
}

const size_t in_mem_cdirected_vertex_index::ENTRY_SIZE;
directed_vertex_entry in_mem_cdirected_vertex_index::get_vertex(
		vertex_id_t id) const
{
	directed_vertex_entry e = entries[id / ENTRY_SIZE].get_start_offs();
	int off = id % ENTRY_SIZE;
	off_t in_off = e.get_in_off();
	off_t out_off = e.get_out_off();
	vertex_id_t start_id = id & (~ENTRY_MASK);
	for (int i = 0; i < off; i++) {
		in_off += get_in_size(start_id + i);
		out_off += get_out_size(start_id + i);
	}
	return directed_vertex_entry(in_off, out_off);
}

void in_mem_cdirected_vertex_index::verify_against(
		directed_vertex_index &index)
{
	index.verify();
	id_range_t range(10, std::min(100UL, index.get_num_vertices()));
	compressed_directed_index_iterator it(*this, range);
	vertex_id_t id = range.first;
	while (it.has_next()) {
		ext_mem_vertex_info in_info = index.get_vertex_info_in(id);
		ext_mem_vertex_info out_info = index.get_vertex_info_out(id);
		if (in_info.get_off() != it.get_curr_off()) {
			BOOST_LOG_TRIVIAL(error) << boost::format(
					"in off: %1% != curr off: %2%") % in_info.get_off()
				% it.get_curr_off();
			return;
		}
		if (out_info.get_off() == it.get_curr_out_off()) {
			BOOST_LOG_TRIVIAL(error) << boost::format(
					"out off: %1% != curr off: %2%") % out_info.get_off()
				% it.get_curr_out_off();
			return;
		}
		id++;
		it.move_next();
	}
}

cdirected_vertex_index::ptr cdirected_vertex_index::construct(
		directed_vertex_index &index)
{
	size_t edge_data_size = index.get_graph_header().get_edge_data_size();
	size_t num_entries = index.get_num_entries();
	size_t num_vertices = num_entries - 1;
	std::vector<large_vertex_t> large_in_vertices;
	std::vector<large_vertex_t> large_out_vertices;
	std::vector<entry_type> entries(
			ROUNDUP(num_vertices, ENTRY_SIZE) / ENTRY_SIZE);
	for (size_t off = 0; off < num_vertices; off += ENTRY_SIZE) {
		off_t entry_idx = off / ENTRY_SIZE;
		entries[entry_idx] = entry_type(index.get_data() + off, edge_data_size,
					std::min(ENTRY_SIZE + 1, num_entries - off));

		vertex_id_t id = off;
		for (size_t i = 0; i < ENTRY_SIZE; i++) {
			if (entries[entry_idx].is_large_in_vertex(i)) {
				ext_mem_vertex_info info = index.get_vertex_info_in(id + i);
				large_in_vertices.push_back(large_vertex_t(id + i,
							ext_mem_undirected_vertex::vsize2num_edges(
								info.get_size(), edge_data_size)));
			}
			if (entries[entry_idx].is_large_out_vertex(i)) {
				ext_mem_vertex_info info = index.get_vertex_info_out(id + i);
				large_out_vertices.push_back(large_vertex_t(id + i,
							ext_mem_undirected_vertex::vsize2num_edges(
								info.get_size(), edge_data_size)));
			}
		}
	}

	return construct(entries, large_in_vertices, large_out_vertices,
			index.get_graph_header());
}

cdirected_vertex_index::ptr cdirected_vertex_index::construct(
		const std::vector<entry_type> &entries,
		const std::vector<large_vertex_t> &large_in_vertices,
		const std::vector<large_vertex_t> &large_out_vertices,
		const graph_header &header)
{
	size_t tot_size = sizeof(cdirected_vertex_index)
		+ sizeof(entries[0]) * entries.size()
		+ sizeof(large_in_vertices[0]) * large_in_vertices.size()
		+ sizeof(large_out_vertices[0]) * large_out_vertices.size();
	char *buf = (char *) malloc(tot_size);
	memcpy(buf, &header, vertex_index::get_header_size());
	cdirected_vertex_index *cindex = (cdirected_vertex_index *) buf;
	cindex->h.data.entry_size = sizeof(entries[0]);
	cindex->h.data.num_entries = entries.size();
	cindex->h.data.out_part_loc = entries[0].get_start_out_off();
	cindex->h.data.compressed = true;
	cindex->h.data.num_large_in_vertices = large_in_vertices.size();
	cindex->h.data.num_large_out_vertices = large_out_vertices.size();
	assert(entries.size() * ENTRY_SIZE >= header.get_num_vertices());

	memcpy(cindex->entries, entries.data(),
			entries.size() * sizeof(entries[0]));
	memcpy(cindex->get_large_in_vertices(), large_in_vertices.data(),
			sizeof(large_in_vertices[0]) * large_in_vertices.size());
	memcpy(cindex->get_large_out_vertices(), large_out_vertices.data(),
			sizeof(large_out_vertices[0]) * large_out_vertices.size());
	return cdirected_vertex_index::ptr(cindex, destroy_index());
}

cdirected_vertex_index::ptr cdirected_vertex_index::construct(
		size_t num_vertices, const vsize_t num_in_edges[],
		const vsize_t num_out_edges[], const graph_header &header)
{
	// Get all the large vertices.
	std::vector<large_vertex_t> large_in_vertices;
	std::vector<large_vertex_t> large_out_vertices;
	for (size_t i = 0; i < num_vertices; i++) {
		if (num_in_edges[i] >= compressed_vertex_entry::LARGE_VERTEX_SIZE)
			large_in_vertices.push_back(large_vertex_t(i, num_in_edges[i]));
		if (num_out_edges[i] >= compressed_vertex_entry::LARGE_VERTEX_SIZE)
			large_out_vertices.push_back(large_vertex_t(i, num_out_edges[i]));
	}

	size_t num_entries = ROUNDUP(num_vertices, ENTRY_SIZE) / ENTRY_SIZE;
	size_t tot_size = sizeof(cdirected_vertex_index)
		+ sizeof(entry_type) * num_entries
		+ sizeof(large_in_vertices[0]) * large_in_vertices.size()
		+ sizeof(large_out_vertices[0]) * large_out_vertices.size();
	char *buf = (char *) malloc(tot_size);
	memcpy(buf, &header, vertex_index::get_header_size());
	cdirected_vertex_index *cindex = (cdirected_vertex_index *) buf;

	// Initialize the entries.
	int edge_data_size = header.get_edge_data_size();
	size_t in_size = 0;
	size_t out_size = 0;
	for (size_t vid = 0; vid < num_vertices; vid += ENTRY_SIZE) {
		off_t entry_idx = vid / ENTRY_SIZE;
		directed_vertex_entry dentry(in_size, out_size);
		size_t num_entry_vertices = std::min(ENTRY_SIZE, num_vertices - vid);
		cindex->entries[entry_idx] = entry_type(dentry, num_in_edges + vid,
				num_out_edges + vid, num_entry_vertices);
		for (size_t j = 0; j < num_entry_vertices; j++) {
			in_size += ext_mem_undirected_vertex::num_edges2vsize(
					num_in_edges[vid + j], edge_data_size);
			out_size += ext_mem_undirected_vertex::num_edges2vsize(
					num_out_edges[vid + j], edge_data_size);
		}
	}
	assert(in_size == out_size);
	// Adjust the offset of each compressed entry.
	for (size_t entry_idx = 0; entry_idx < num_entries; entry_idx++) {
		directed_vertex_entry e = cindex->entries[entry_idx].get_start_offs();
		cindex->entries[entry_idx].reset_start_offs(
				e.get_in_off() + sizeof(vertex_index),
				e.get_out_off() + sizeof(vertex_index) + in_size);
	}

	// Initialize the remaining part of the header.
	cindex->h.data.entry_size = sizeof(entry_type);
	cindex->h.data.num_entries = num_entries;
	cindex->h.data.out_part_loc = sizeof(vertex_index) + in_size;
	cindex->h.data.compressed = true;
	cindex->h.data.num_large_in_vertices = large_in_vertices.size();
	cindex->h.data.num_large_out_vertices = large_out_vertices.size();
	assert(num_entries * ENTRY_SIZE >= header.get_num_vertices());

	memcpy(cindex->get_large_in_vertices(), large_in_vertices.data(),
			sizeof(large_in_vertices[0]) * large_in_vertices.size());
	memcpy(cindex->get_large_out_vertices(), large_out_vertices.data(),
			sizeof(large_out_vertices[0]) * large_out_vertices.size());
	return cdirected_vertex_index::ptr(cindex, destroy_index());
}

cundirected_vertex_index::ptr cundirected_vertex_index::construct(
		undirected_vertex_index &index)
{
	size_t edge_data_size = index.get_graph_header().get_edge_data_size();
	size_t num_entries = index.get_num_entries();
	size_t num_vertices = num_entries - 1;
	std::vector<large_vertex_t> large_vertices;
	std::vector<entry_type> entries(
			ROUNDUP(num_vertices, ENTRY_SIZE) / ENTRY_SIZE);
	for (size_t off = 0; off < num_vertices; off += ENTRY_SIZE) {
		off_t entry_idx = off / ENTRY_SIZE;
		entries[entry_idx] = entry_type(
					index.get_data() + off, edge_data_size,
					std::min(ENTRY_SIZE + 1, num_entries - off));

		vertex_id_t id = off;
		for (size_t i = 0; i < ENTRY_SIZE; i++) {
			if (entries[entry_idx].is_large_vertex(i)) {
				ext_mem_vertex_info info = index.get_vertex_info(id + i);
				large_vertices.push_back(large_vertex_t(id + i,
							ext_mem_undirected_vertex::vsize2num_edges(
								info.get_size(), edge_data_size)));
			}
		}
	}

	return construct(entries, large_vertices, index.get_graph_header());
}

cundirected_vertex_index::ptr cundirected_vertex_index::construct(
		const std::vector<entry_type> &entries,
		const std::vector<large_vertex_t> &large_vertices,
		const graph_header &header)
{
	size_t tot_size = sizeof(cundirected_vertex_index)
		+ sizeof(entries[0]) * entries.size()
		+ sizeof(large_vertices[0]) * large_vertices.size();
	char *buf = (char *) malloc(tot_size);
	memcpy(buf, &header, vertex_index::get_header_size());
	cundirected_vertex_index *cindex = (cundirected_vertex_index *) buf;
	cindex->h.data.entry_size = sizeof(entries[0]);
	cindex->h.data.num_entries = entries.size();
	cindex->h.data.compressed = true;
	cindex->h.data.num_large_in_vertices = large_vertices.size();
	cindex->h.data.num_large_out_vertices = 0;

	memcpy(cindex->entries, entries.data(), entries.size() * sizeof(entries[0]));
	memcpy(cindex->get_large_vertices(), large_vertices.data(),
			sizeof(large_vertices[0]) * large_vertices.size());
	return ptr(cindex, destroy_index());
}

cundirected_vertex_index::ptr cundirected_vertex_index::construct(
		size_t num_vertices, const vsize_t num_edges[],
		const graph_header &header)
{
	// Get all the large vertices.
	std::vector<large_vertex_t> large_vertices;
	for (size_t i = 0; i < num_vertices; i++) {
		if (num_edges[i] >= compressed_vertex_entry::LARGE_VERTEX_SIZE)
			large_vertices.push_back(large_vertex_t(i, num_edges[i]));
	}

	size_t num_entries = ROUNDUP(num_vertices, ENTRY_SIZE) / ENTRY_SIZE;
	size_t tot_size = sizeof(cundirected_vertex_index)
		+ sizeof(entry_type) * num_entries
		+ sizeof(large_vertices[0]) * large_vertices.size();
	char *buf = (char *) malloc(tot_size);
	memcpy(buf, &header, vertex_index::get_header_size());
	cundirected_vertex_index *cindex = (cundirected_vertex_index *) buf;

	// Initialize the entries.
	int edge_data_size = header.get_edge_data_size();
	size_t size = graph_header::get_header_size();
	for (size_t vid = 0; vid < num_vertices; vid += ENTRY_SIZE) {
		off_t entry_idx = vid / ENTRY_SIZE;
		vertex_offset dentry(size);
		size_t num_entry_vertices = std::min(ENTRY_SIZE, num_vertices - vid);
		cindex->entries[entry_idx] = entry_type(dentry, num_edges + vid,
				num_entry_vertices);
		for (size_t j = 0; j < num_entry_vertices; j++) {
			size += ext_mem_undirected_vertex::num_edges2vsize(
					num_edges[vid + j], edge_data_size);
		}
	}

	// Initialize the remaining part of the header.
	cindex->h.data.entry_size = sizeof(entry_type);
	cindex->h.data.num_entries = num_entries;
	cindex->h.data.compressed = true;
	cindex->h.data.num_large_in_vertices = large_vertices.size();
	cindex->h.data.num_large_out_vertices = 0;
	assert(num_entries * ENTRY_SIZE >= header.get_num_vertices());

	memcpy(cindex->get_large_vertices(), large_vertices.data(),
			sizeof(large_vertices[0]) * large_vertices.size());
	return cundirected_vertex_index::ptr(cindex, destroy_index());
}

/*
 * For uncompressed vertex index, we can query on the original data structure
 * read from disks.
 */

class in_mem_query_directed_vertex_index: public in_mem_query_vertex_index
{
	directed_vertex_index::ptr index;
public:
	in_mem_query_directed_vertex_index(
			vertex_index::ptr index): in_mem_query_vertex_index(
				true, false) {
		assert(index->get_graph_header().is_directed_graph());
		assert(!index->is_compressed());
		this->index = directed_vertex_index::cast(index);
	}

	vsize_t get_num_in_edges(vertex_id_t id) const {
		ext_mem_vertex_info info = index->get_vertex_info_in(id);
		return ext_mem_undirected_vertex::vsize2num_edges(info.get_size(),
				index->get_graph_header().get_edge_data_size());
	}

	vsize_t get_num_out_edges(vertex_id_t id) const {
		ext_mem_vertex_info info = index->get_vertex_info_out(id);
		return ext_mem_undirected_vertex::vsize2num_edges(info.get_size(),
				index->get_graph_header().get_edge_data_size());
	}

	virtual vsize_t get_num_edges(vertex_id_t id, edge_type type) const {
		switch (type) {
			case edge_type::IN_EDGE:
				return get_num_in_edges(id);
			case edge_type::OUT_EDGE:
				return get_num_out_edges(id);
			case edge_type::BOTH_EDGES:
				return get_num_in_edges(id) + get_num_out_edges(id);
			default:
				return 0;
		}
	}

	virtual vertex_index::ptr get_raw_index() const {
		return index;
	}
};

class in_mem_query_undirected_vertex_index: public in_mem_query_vertex_index
{
	undirected_vertex_index::ptr index;
public:
	in_mem_query_undirected_vertex_index(
			vertex_index::ptr index): in_mem_query_vertex_index(
				false, false) {
		assert(!index->get_graph_header().is_directed_graph());
		assert(!index->is_compressed());
		this->index = undirected_vertex_index::cast(index);
	}

	virtual vsize_t get_num_edges(vertex_id_t id, edge_type type) const {
		ext_mem_vertex_info info = index->get_vertex_info(id);
		return ext_mem_undirected_vertex::vsize2num_edges(info.get_size(),
				index->get_graph_header().get_edge_data_size());
	}

	virtual vertex_index::ptr get_raw_index() const {
		return index;
	}
};

in_mem_query_vertex_index::ptr in_mem_query_vertex_index::create(
		vertex_index::ptr index, bool compress)
{
	if (index->is_compressed() || compress) {
		if (index->get_graph_header().is_directed_graph())
			return in_mem_cdirected_vertex_index::create(*index);
		else
			return in_mem_cundirected_vertex_index::create(*index);
	}
	else {
		if (index->get_graph_header().is_directed_graph())
			return ptr(new in_mem_query_directed_vertex_index(index));
		else
			return ptr(new in_mem_query_undirected_vertex_index(index));
	}
}

}
