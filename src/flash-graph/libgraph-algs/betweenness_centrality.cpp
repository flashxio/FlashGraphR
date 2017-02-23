/**
 * Copyright 2014 Open Connectome Project (http://openconnecto.me)
 * Written by Disa Mhembere (disa@jhu.edu)
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
#ifdef PROFILER
#include <gperftools/profiler.h>
#endif

#include <vector>

#include "thread.h"
#include "io_interface.h"
#include "container.h"
#include "concurrency.h"

#include "vertex_index.h"
#include "graph_engine.h"
#include "graph_config.h"
#include "FGlib.h"
#include "save_result.h"

using namespace fg;

namespace {
short bfs_max_dist;

/* `update` phase is where BC is updated */ 
vertex_id_t g_source_vertex;
enum btwn_phase_t
{
	bfs,
	back_prop,
	bc_summation,
};

btwn_phase_t g_alg_phase = bfs;

class betweenness_vertex: public compute_directed_vertex
{
	float btwn_cent; // per-vertex btwn_cent
	float delta;
	int sigma;
	short dist;
	bool bfs_visited;

	public:
	betweenness_vertex(vertex_id_t id): compute_directed_vertex(id) {
		btwn_cent = 0;
		delta = 0; 
		sigma = 0 ;
		dist = -1; 
		bfs_visited = false;
	}

	void init(int sigma, short dist) {
		this->sigma = sigma;
		this->dist = dist;	
		this->delta = 0;
		this->bfs_visited = false;
	}

	// Used for save_query join
	float get_result() const {
		return btwn_cent;
	}

	short get_dist() const {
		return dist;
	}

	void set_dist(short dist) {
		this->dist = dist;
	}

	void set_sigma(int sigma) {
		this->sigma = sigma;
	}

	bool is_bfs_visited() const {
		return bfs_visited;
	} 
	void set_visited(bool visited) {
		this->bfs_visited = visited;
	}

	void run(vertex_program &prog);
	void run(vertex_program &prog, const page_vertex &vertex);
	void run_on_message(vertex_program &, const vertex_message &msg1);
};

typedef std::shared_ptr<std::vector<vertex_id_t> > vertex_set_ptr;
// Store activated vertex IDs per iteration in the bfs phase
typedef std::map<int, std::vector<vertex_set_ptr> > vertex_map_t;

class bfs_vertex_program: public vertex_program_impl<betweenness_vertex>
{
	std::vector<vertex_set_ptr> bfs_visited_vertices; // Vertex set visited from this thread
	short max_dist; // Keep track of max dist so we can activate greatest when bp-ing
	public:
	bfs_vertex_program() {
		max_dist = 0;
	}

	typedef std::shared_ptr<bfs_vertex_program> ptr;

	static ptr cast2(vertex_program::ptr prog) {
		return std::static_pointer_cast<bfs_vertex_program, vertex_program>(prog);
	}

	void add_visited_bfs(vertex_id_t vid) {
		max_dist = get_graph().get_curr_level();
		// BOOST_LOG_TRIVIAL(info) << "The current per thread max_dist is " << max_dist << "\n";
		assert(max_dist == ((short)bfs_visited_vertices.size()) - 1);
		bfs_visited_vertices.back()->push_back(vid);
	}

	virtual void run_on_engine_start() {
		bfs_visited_vertices.push_back(vertex_set_ptr(
					new std::vector<vertex_id_t>()));
	}

	virtual void run_on_iteration_end() {
		bfs_visited_vertices.push_back(vertex_set_ptr(
					new std::vector<vertex_id_t>()));
	}

	void collect_vertices(vertex_map_t &vertices) {
		vertex_map_t::const_iterator it = vertices.find(get_partition_id());
		if (it != vertices.end())
			BOOST_LOG_TRIVIAL(info) << "part" << it->first << "already exists";
		assert(it == vertices.end());
		vertices.insert(vertex_map_t::value_type(get_partition_id(),
					bfs_visited_vertices));
	}

	short get_max_dist() const {
		return max_dist;
	}
};

class bp_vertex_program: public vertex_program_impl<betweenness_vertex>
{
	std::shared_ptr<vertex_map_t> all_vertices;
	std::vector<vertex_set_ptr> bfs_visited_vertices;
	public:
	bp_vertex_program(std::shared_ptr<vertex_map_t> vertices) {
		this->all_vertices = vertices;
	}

	virtual void run_on_engine_start() {
		vertex_map_t::const_iterator it = all_vertices->find(get_partition_id());
		assert(it != all_vertices->end());
		bfs_visited_vertices = it->second;

		// Drop the empty set added at the end of the last iteration in bfs.
		assert(bfs_visited_vertices.back()->empty());
		bfs_visited_vertices.pop_back();
		// Drop the last set of visited vertices because we have already activated them.
		while ((short)bfs_visited_vertices.size() > bfs_max_dist) {
			bfs_visited_vertices.pop_back();
		}

		assert((short)bfs_visited_vertices.size() == bfs_max_dist);
	}

	virtual void run_on_iteration_end() {
		if (!bfs_visited_vertices.empty()) {
			vertex_set_ptr vertices = bfs_visited_vertices.back();
			activate_vertices(vertices->data(), vertices->size());
			bfs_visited_vertices.pop_back();
		}
	}
};

class bfs_vertex_program_creater: public vertex_program_creater
{
	public:
		vertex_program::ptr create() const {
			return vertex_program::ptr(new bfs_vertex_program());
		}
};

class bp_vertex_program_creater: public vertex_program_creater
{
	std::shared_ptr<vertex_map_t> all_vertices;
	public:
	bp_vertex_program_creater() {
		all_vertices = std::shared_ptr<vertex_map_t>(new vertex_map_t());
	}

	vertex_map_t &get_vertex_map() {
		return *all_vertices;
	}

	vertex_program::ptr create() const {
		return vertex_program::ptr(new bp_vertex_program(all_vertices));
	}
};

class bfs_message: public vertex_message
{
	vertex_id_t sender_id;
	short parent_dist;
	int parent_sigma;

	public:
	bfs_message(vertex_id_t id, short sender_dist, int sigma): 
		vertex_message(sizeof(bfs_message), true) {
			sender_id = id;
			parent_dist = sender_dist;
			parent_sigma = sigma;
		}

	const vertex_id_t get_sender_id() const {
		return sender_id;
	}

	const short get_parent_dist() const {
		return parent_dist;
	} 

	const int get_parent_sigma() const {
		return parent_sigma;
	}
};

/** Back propagate message */
class bp_message: public vertex_message
{
	float delta;
	int sigma;
	short dist;

	public:
	bp_message(short dist, float delta, int sigma): 
		vertex_message(sizeof(bp_message), false) {
			this->delta = delta;
			this->sigma = sigma;
			this->dist = dist;
		}

	const float get_sender_delta() const {
		return delta;
	} 

	const int get_sender_sigma() const {
		return sigma;
	}

	const short get_sender_dist() const {
		return dist;
	}
};

void betweenness_vertex::run(vertex_program &prog) { 
	switch (g_alg_phase) {
		case btwn_phase_t::bfs:  
			{
				if (is_bfs_visited()) 
					return; 
				directed_vertex_request req(prog.get_vertex_id(*this), edge_type::OUT_EDGE);
				request_partial_vertices(&req, 1);
				((bfs_vertex_program&)prog).
					add_visited_bfs(prog.get_vertex_id(*this));
				break;
			}
		case btwn_phase_t::back_prop: 
			{
				directed_vertex_request req(prog.get_vertex_id(*this), edge_type::IN_EDGE);
				request_partial_vertices(&req, 1);
				break;
			}
		case btwn_phase_t::bc_summation:
			{
				if (prog.get_vertex_id(*this) != g_source_vertex)
					btwn_cent += delta;	
				break;
			}
		default:
			assert(0);
	}
}

void betweenness_vertex::run(vertex_program &prog, const page_vertex &vertex)
{
	switch (g_alg_phase) {
		case btwn_phase_t::bfs :
			{
				bfs_visited = true;
				int num_dests = vertex.get_num_edges(OUT_EDGE);
				if (num_dests == 0) return;

				edge_seq_iterator it = vertex.get_neigh_seq_it(OUT_EDGE, 0, num_dests);
				bfs_message msg(vertex.get_id(), this->dist, this->sigma);
				prog.multicast_msg(it, msg);
				break;
			}
		case btwn_phase_t::back_prop :
			{
				/* NOTE: Sending to all in_neighs instead of only P's ... */
				int num_dests = vertex.get_num_edges(IN_EDGE); 
				edge_seq_iterator it = vertex.get_neigh_seq_it(IN_EDGE, 0, num_dests);
				bp_message msg(this->dist, this->delta, this->sigma);
				prog.multicast_msg(it, msg);
				break;
			}
		default:
			assert(0);
	}
}

void betweenness_vertex::run_on_message(vertex_program &prog, const vertex_message &msg1) {
	switch (g_alg_phase) {
		case btwn_phase_t::bfs:
			{
				const bfs_message &msg = (const bfs_message &) msg1;
				if (this->dist < 0) { 
					this->dist = msg.get_parent_dist() + 1;
				}
				if (this->dist == msg.get_parent_dist() + 1) {
					this->sigma = this->sigma + msg.get_parent_sigma();
				}
				break;
			}
		case btwn_phase_t::back_prop:
			{
				const bp_message &msg = (const bp_message &) msg1;
				// Ignore this message if you're not a parent on the path
				if (this->dist != msg.get_sender_dist() - 1) {
					return;
				}

				// Now we know it's only parents
				if (msg.get_sender_sigma() != 0) { // If msg.get_sender_sigma() == 0 do nothing
					delta = delta + (((float)sigma/msg.get_sender_sigma()) 
							* (1 + msg.get_sender_delta()));
				} 
				break;
			}
		default:
			assert(0);
	}
}

class btwn_initializer: public vertex_initializer
{
	compute_vertex* source_vertex;
	public:
	btwn_initializer(compute_vertex *v) {
		source_vertex = v;
	}

	virtual void init(compute_vertex &v) {
		betweenness_vertex &bv = (betweenness_vertex &) v;
		&v == source_vertex ? bv.init(1,0) : bv.init(0,-1); // sigma, dist
	}
};

/** For back prop phase where we activate vertices 
  with only dist = max_dist. Also resets the bfs bit
  to unvisited.
  */
class activate_by_dist_filter: public vertex_filter {
	short dist;

	public:
	activate_by_dist_filter (short dist) {
		this->dist = dist;
	} 
	bool keep(vertex_program &prog, compute_vertex &v) {
		betweenness_vertex &bv = (betweenness_vertex &) v;
		bool is_max_v = bv.get_dist() == dist;

		return is_max_v;
	}
};
}

namespace fg 
{
fm::vector::ptr compute_betweenness_centrality(FG_graph::ptr fg,
		const std::vector<vertex_id_t>& ids)
{
	bool directed = fg->get_graph_header().is_directed_graph();
	if (!directed) {
		BOOST_LOG_TRIVIAL(error)
			<< "This algorithm currently works on a directed graph";
		return fm::vector::ptr();
	}

	graph_index::ptr index = NUMA_graph_index<betweenness_vertex>::create(
			fg->get_graph_header());
	graph_engine::ptr graph = fg->create_engine(index);

	BOOST_LOG_TRIVIAL(info) << "Starting Betweenness Centrality ...";
	BOOST_LOG_TRIVIAL(info) << "prof_file: " << graph_conf.get_prof_file().c_str();
#ifdef PROFILER
	if (!graph_conf.get_prof_file().empty())
		ProfilerStart(graph_conf.get_prof_file().c_str());
#endif

	struct timeval start, end;
	gettimeofday(&start, NULL);

	BOOST_FOREACH (vertex_id_t id , ids) {
		if (!graph->get_num_edges(id))
		   continue;	

		g_source_vertex = id;
		bfs_max_dist = 0; // Must reset bfs dist for each vertex
		// BFS phase. Inintialize start vert(ex)(ices)
		g_alg_phase = btwn_phase_t::bfs;
		BOOST_LOG_TRIVIAL(info) << "Starting BFS for vertex: " << g_source_vertex;
		graph->init_all_vertices(vertex_initializer::ptr(
					new btwn_initializer(&(graph->get_vertex(g_source_vertex)))));
		graph->start(&g_source_vertex, 1, vertex_initializer::ptr(),
				vertex_program_creater::ptr(new bfs_vertex_program_creater()));
		graph->wait4complete();

		std::vector<vertex_program::ptr> programs;
		graph->get_vertex_programs(programs);
		bp_vertex_program_creater *bp_prog_creater_ptr = new bp_vertex_program_creater();
		vertex_program_creater::ptr bp_prog_creater
			= vertex_program_creater::ptr(bp_prog_creater_ptr);

		BOOST_FOREACH(vertex_program::ptr prog, programs) {
			bfs_vertex_program::cast2(prog)->collect_vertices(
					bp_prog_creater_ptr->get_vertex_map());
			bfs_max_dist = std::max(bfs_max_dist,
					bfs_vertex_program::cast2(prog)->get_max_dist());
		}

		BOOST_LOG_TRIVIAL(info) << "Max dist for bfs is: " << bfs_max_dist << "...";

		if (bfs_max_dist > 0) {
			// Back propagation phase
			BOOST_LOG_TRIVIAL(info) << "Starting back_prop phase for vertex: " 
					<< g_source_vertex << "...";
			g_alg_phase = btwn_phase_t::back_prop;

			std::shared_ptr<vertex_filter> filter =
				std::shared_ptr<vertex_filter>(new activate_by_dist_filter(bfs_max_dist));

			graph->start(filter, std::move(bp_prog_creater));
			graph->wait4complete();
			BOOST_LOG_TRIVIAL(info) << "BC summation step";
			g_alg_phase = bc_summation;
			graph->start_all();
			graph->wait4complete();
		}
	}

	gettimeofday(&end, NULL);
	fm::detail::mem_vec_store::ptr res_store = fm::detail::mem_vec_store::create(
			fg->get_num_vertices(), safs::params.get_num_nodes(),
			fm::get_scalar_type<float>());
	graph->query_on_all(vertex_query::ptr(
				new save_query<float, betweenness_vertex>(res_store)));

#if 0
	BOOST_LOG_TRIVIAL(info) << "Printing betweenness vector:";
	ret->print(); 
#endif

#ifdef PROFILER
	if (!graph_conf.get_prof_file().empty())
		ProfilerStop();
#endif

	BOOST_LOG_TRIVIAL(info) << boost::format("It takes %1% seconds")
		% time_diff(start, end);

	return fm::vector::create(res_store);
}
}
