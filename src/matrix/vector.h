#ifndef __FM_VECTOR_H__
#define __FM_VECTOR_H__

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
#include <memory>

#include "generic_type.h"
#include "bulk_operate.h"
#include "vec_store.h"
#include "mem_vec_store.h"

namespace fm
{

class bulk_operate;
class data_frame;
class agg_operate;
class dense_matrix;

class vector
{
	detail::vec_store::const_ptr store;

	bool verify_groupby(const gr_apply_operate<local_vec_store> &op) const;
protected:
	vector(detail::vec_store::const_ptr store) {
		this->store = store;
	}
public:
	typedef std::shared_ptr<vector> ptr;
	typedef std::shared_ptr<const vector> const_ptr;

	static ptr create(detail::vec_store::const_ptr store) {
		return ptr(new vector(store));
	}
	static ptr create(size_t length, const scalar_type &type, int num_nodes,
			bool in_mem, const set_vec_operate &op);

	~vector() {
	}

	const detail::vec_store &get_data() const {
		return *store;
	}

	detail::vec_store::const_ptr get_raw_store() const {
		return store;
	}

	template<class T>
	std::vector<T> conv2std() const {
		assert(is_type<T>());
		std::vector<T> ret(get_length());
		store->copy_to((char *) ret.data(), ret.size());
		return ret;
	}

	bool is_in_mem() const {
		return store->is_in_mem();
	}

	// Normally the entry size is the type size. But a vector may also
	// contains vectors, and the entry size is 0, which is no longer
	// the type size.
	size_t get_entry_size() const {
		return store->get_entry_size();
	}
	size_t get_length() const {
		return store->get_length();
	}

	template<class T>
	bool is_type() const {
		return store->get_type().get_type() == fm::get_type<T>();
	}

	const scalar_type &get_type() const {
		return store->get_type();
	}

	bool is_sorted() const {
		return store->is_sorted();
	}

	bool equals(const vector &vec) const;

	vector::ptr sort() const;
	std::shared_ptr<data_frame> sort_with_index() const;
	std::shared_ptr<dense_matrix> conv2mat(size_t nrow, size_t ncol,
			bool byrow) const;

	/*
	 * This is a general version of groupby. It requires sorting on the entire
	 * vector. If `with_val' is true, this method returns a data frame with two
	 * columns: the first column is a vector of unique values in the vector;
	 * the second column is a vector of vectors and contains the aggregation
	 * result for each unique value in the first column.
	 * If `with_val' is false, this method returns a data frame with only
	 * one column, which is a vector of vectors and contains the aggregation
	 * result for each unique value.
	 */
	std::shared_ptr<data_frame> groupby(
			const gr_apply_operate<local_vec_store> &op, bool with_val) const;

	scalar_variable::ptr aggregate(const bulk_operate &op) const;
	scalar_variable::ptr dot_prod(const vector &vec) const;

	template<class T>
	T max() const {
		const bulk_operate &max_op = *get_type().get_basic_ops().get_op(
				basic_ops::op_idx::MAX);
		scalar_variable::ptr res = aggregate(max_op);
		return *(T *) res->get_raw();
	}
	template<class T>
	T sum() const {
		const bulk_operate &sum_op = *get_type().get_basic_ops().get_op(
				basic_ops::op_idx::ADD);
		scalar_variable::ptr res = aggregate(sum_op);
		return *(T *) res->get_raw();
	}

	bool export2(FILE *f) const;
};

/*
 * Create a sequence of values in [start, end]. `end' is inclusive.
 */
template<class EntryType>
vector::ptr create_seq_vector(EntryType start, EntryType end, EntryType stride,
		int num_nodes = -1, bool in_mem = true)
{
	detail::vec_store::ptr store = detail::create_seq_vec_store(start, end, stride,
			num_nodes, in_mem);
	if (store == NULL)
		return vector::ptr();
	return vector::create(store);
}

/*
 * Create a vector filled with a constant value.
 */
template<class EntryType>
vector::ptr create_rep_vector(size_t length, EntryType initv,
		int num_nodes = -1, bool in_mem = true)
{
	detail::vec_store::ptr store = detail::create_rep_vec_store(length, initv,
			num_nodes, in_mem);
	if (store == NULL)
		return vector::ptr();
	return vector::create(store);
}

}

#endif
