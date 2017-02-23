#ifndef __FACTOR_H__
#define __FACTOR_H__

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

#include "vector.h"
#include "col_vec.h"

/*
 * This factor is the same as the one in R.
 */

namespace fm
{

typedef uint32_t factor_value_t;

class factor
{
	size_t num_levels;
public:
	factor(size_t num_levels) {
		this->num_levels = num_levels;
	}

	size_t get_num_levels() const {
		return num_levels;
	}

	bool is_valid_level(factor_value_t v) const {
		return v >= 0 && (size_t) v < num_levels;
	}
};

/*
 * There are two factor vectors. One is for vectors and the other is for
 * dense matrices. Eventually, we should have only one factor vector.
 */

/*
 * This is a factor vector for vectors.
 */
class factor_vector: public vector
{
	factor f;

	factor_vector(const factor &_f,
			detail::vec_store::const_ptr store): vector(store), f(_f) {
	}

	factor_vector(const factor &_f, size_t len, int num_nodes, bool in_mem,
			const set_vec_operate &op): vector(detail::vec_store::create(len,
				get_scalar_type<factor_value_t>(), num_nodes, in_mem)), f(_f) {
		const_cast<detail::vec_store &>(get_data()).set_data(op);
	}
public:
	typedef std::shared_ptr<factor_vector> ptr;
	typedef std::shared_ptr<const factor_vector> const_ptr;

	static ptr create(const factor &f, detail::vec_store::const_ptr vec) {
		return ptr(new factor_vector(f, vec));
	}

	static ptr create(const factor &f, size_t length, int num_nodes,
			bool in_mem, const set_vec_operate &op) {
		return ptr(new factor_vector(f, length, num_nodes, in_mem, op));
	}

	const factor &get_factor() const {
		return f;
	}

	size_t get_num_levels() const {
		return f.get_num_levels();
	}
};

/*
 * This is a factor vector for dense matrices.
 */
class factor_col_vector: public col_vec
{
	factor f;
	detail::vec_store::const_ptr uniq_vals;
	detail::vec_store::const_ptr cnts;

	factor_col_vector(const factor &_f,
			detail::matrix_store::const_ptr store): col_vec(store), f(_f) {
	}

	factor_col_vector(const factor &_f, size_t len, int num_nodes, bool in_mem,
			const set_operate &op);
public:
	typedef std::shared_ptr<factor_col_vector> ptr;
	typedef std::shared_ptr<const factor_col_vector> const_ptr;

	static ptr create(const factor &f, dense_matrix::ptr mat);
	static ptr create(dense_matrix::ptr mat);

	static ptr create(const factor &f, size_t length, int num_nodes,
			bool in_mem, const set_operate &op) {
		return ptr(new factor_col_vector(f, length, num_nodes, in_mem, op));
	}

	const factor &get_factor() const {
		return f;
	}

	size_t get_num_levels() const {
		return f.get_num_levels();
	}

	detail::vec_store::const_ptr get_uniq_vals() const {
		return uniq_vals;
	}

	detail::vec_store::const_ptr get_counts() const {
		return cnts;
	}
};

}

#endif
