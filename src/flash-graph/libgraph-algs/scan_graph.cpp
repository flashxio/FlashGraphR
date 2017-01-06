/*
 * Copyright 2014 Da Zheng
 *
 * This file is part of SA-GraphLib.
 *
 * SA-GraphLib is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SA-GraphLib is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SA-GraphLib.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <set>
#include <vector>

#include "graph_engine.h"
#include "graph_config.h"

#include "graphlab/cuckoo_set_pow2.hpp"
#include "scan_graph.h"

using namespace fg;

const double BIN_SEARCH_RATIO = 100;

size_t neighbor_list::count_edges_hash(const page_vertex *v,
		edge_iterator other_it, edge_iterator other_end,
		std::vector<vertex_id_t> *common_neighs) const
{
	size_t num_local_edges = 0;

	while (other_it != other_end) {
		vertex_id_t neigh_neighbor = *other_it;
		if (neigh_neighbor != v->get_id()
				&& neigh_neighbor != this->get_id()) {
			if (this->contains(neigh_neighbor)) {
#if 0
				num_local_edges += (*other_data_it).get_count();
#endif
				num_local_edges++;
				if (common_neighs)
					common_neighs->push_back(neigh_neighbor);
			}
		}
		++other_it;
	}
	return num_local_edges;
}

#if 0
int neighbor_list::count_edges_bin_search_this(const page_vertex *v,
		std::vector<attributed_neighbor>::const_iterator this_it,
		std::vector<attributed_neighbor>::const_iterator this_end,
		edge_iterator other_it, edge_iterator other_end,
		std::vector<vertex_id_t> &common_neighs) const
{
	int num_local_edges = 0;
	int num_v_edges = other_end - other_it;
	int size_log2 = log2(neighbors->size());
	num_rand_jumps += size_log2 * num_v_edges;
	scan_bytes += num_v_edges * sizeof(vertex_id_t);
	while (other_it != other_end) {
		vertex_id_t neigh_neighbor = *other_it;
		if (neigh_neighbor != v->get_id()
				&& neigh_neighbor != this->get_id()) {
			std::vector<attributed_neighbor>::const_iterator first
				= std::lower_bound(this_it, this_end,
						attributed_neighbor(neigh_neighbor), comp_edge());
			if (first != this_end && neigh_neighbor == first->get_id()) {
#if 0
				num_local_edges += (*other_data_it).get_count();
#endif
				num_local_edges++;
				common_neighs.push_back(first->get_id());
			}
		}
		++other_it;
#if 0
		++other_data_it;
#endif
	}
	return num_local_edges;
}
#endif

size_t neighbor_list::count_edges_bin_search_other(const page_vertex *v,
		neighbor_list::id_iterator this_it,
		neighbor_list::id_iterator this_end,
		edge_iterator other_it, edge_iterator other_end,
		std::vector<vertex_id_t> *common_neighs) const
{
	size_t num_local_edges = 0;

	for (; this_it != this_end; this_it++) {
		vertex_id_t this_neighbor = *this_it;
		// We need to skip loops.
		if (this_neighbor == v->get_id()
				|| this_neighbor == this->get_id()) {
			continue;
		}

		edge_iterator first = std::lower_bound(other_it, other_end,
				this_neighbor);
		// found it.
		if (first != other_end && !(this_neighbor < *first)) {
			int num_dups = 0;
			do {
#if 0
				safs::page_byte_array::const_iterator<edge_count> data_it
					= other_data_it;
				data_it += first - other_it;
				// Edges in the v's neighbor lists may duplicated.
				// The duplicated neighbors need to be counted
				// multiple times.
				num_local_edges += (*data_it).get_count();
				++data_it;
#endif
				num_dups++;
				num_local_edges++;
				++first;
			} while (first != other_end && this_neighbor == *first);
			if (common_neighs)
				common_neighs->push_back(this_neighbor);
		}
	}
	return num_local_edges;
}

size_t neighbor_list::count_edges_scan(const page_vertex *v,
		neighbor_list::id_iterator this_it,
		neighbor_list::id_iterator this_end,
		edge_seq_iterator other_it,
		std::vector<vertex_id_t> *common_neighs) const
{
	size_t num_local_edges = 0;
	while (other_it.has_next() && this_it != this_end) {
		vertex_id_t this_neighbor = *this_it;
		vertex_id_t neigh_neighbor = other_it.curr();
		if (neigh_neighbor == v->get_id()
				|| neigh_neighbor == this->get_id()) {
			other_it.next();
#if 0
			++other_data_it;
#endif
			continue;
		}
		if (this_neighbor == neigh_neighbor) {
			if (common_neighs)
				common_neighs->push_back(*this_it);
			do {
				// Edges in the v's neighbor lists may duplicated.
				// The duplicated neighbors need to be counted
				// multiple times.
#if 0
				num_local_edges += (*other_data_it).get_count();
				++other_data_it;
#endif
				num_local_edges++;
				other_it.next();
			} while (other_it.has_next() && this_neighbor == other_it.curr());
			++this_it;
		}
		else if (this_neighbor < neigh_neighbor) {
			++this_it;
		}
		else {
			other_it.next();
#if 0
			++other_data_it;
#endif
		}
	}
	return num_local_edges;
}

size_t neighbor_list::count_edges(const page_vertex *v, edge_type type,
		std::vector<vertex_id_t> *common_neighs) const
{
	size_t num_v_edges = v->get_num_edges(type);
	if (num_v_edges == 0)
		return 0;

#ifdef PV_STAT
	min_comps += min(num_v_edges, this->size());
#endif
	edge_iterator other_it = v->get_neigh_begin(type);
#if 0
	safs::page_byte_array::const_iterator<edge_count> other_data_it
		= v->get_edge_data_begin<edge_count>(type);
#endif
	edge_iterator other_end = std::lower_bound(other_it, v->get_neigh_end(type),
				v->get_id());
	num_v_edges = other_end - other_it;
	if (num_v_edges == 0)
		return 0;

	neighbor_list::id_iterator this_it = this->get_id_begin();
	neighbor_list::id_iterator this_end = this->get_id_end();
	this_end = std::lower_bound(this_it, this_end,
			v->get_id());

	if (num_v_edges / this->size() > BIN_SEARCH_RATIO) {
#ifdef PV_STAT
		int size_log2 = log2(num_v_edges);
		scan_bytes += this->size() * sizeof(vertex_id_t);
		rand_jumps += size_log2 * this->size();
#endif
		return count_edges_bin_search_other(v, this_it, this_end,
				other_it, other_end, common_neighs);
	}
	else if (this->size() / num_v_edges > 16) {
#ifdef PV_STAT
		scan_bytes += num_v_edges * sizeof(vertex_id_t);
		rand_jumps += num_v_edges;
#endif
		return count_edges_hash(v, other_it, other_end, common_neighs);
	}
	else {
#ifdef PV_STAT
		scan_bytes += num_v_edges * sizeof(vertex_id_t);
		scan_bytes += this->size() * sizeof(vertex_id_t);
#endif
		return count_edges_scan(v, this_it, this_end,
				v->get_neigh_seq_it(type, 0, num_v_edges), common_neighs);
	}
}

size_t neighbor_list::count_edges(const page_vertex *v)
{
	assert(!this->empty());
	if (v->get_num_edges(edge_type::BOTH_EDGES) == 0)
		return 0;

	size_t ret = count_edges(v, edge_type::IN_EDGE, NULL)
		+ count_edges(v, edge_type::OUT_EDGE, NULL);
	return ret;
}
