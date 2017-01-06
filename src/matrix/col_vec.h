#ifndef __FM_COL_VEC_H__
#define __FM_COL_VEC_H__

/*
 * Copyright 2016 Open Connectome Project (http://openconnecto.me)
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

#include "dense_matrix.h"
#include "local_matrix_store.h"

namespace fm
{

class vector;

/*
 * This represents a vector with a one-col matrix.
 * As such, a vector can contain data that doesn't physically exist.
 */
class col_vec: public dense_matrix
{
protected:
	col_vec(detail::matrix_store::const_ptr mat): dense_matrix(mat) {
		assert(mat->get_num_cols() == 1);
	}
public:
	typedef std::shared_ptr<col_vec> ptr;
	typedef std::shared_ptr<const col_vec> const_ptr;

	static ptr create(std::shared_ptr<const vector> vec);

	template<class T>
	static ptr create_randn(size_t len) {
		dense_matrix::ptr mat = dense_matrix::create_randn<T>(0, 1, len, 1,
				matrix_layout_t::L_COL);
		return ptr(new col_vec(mat->get_raw_store()));
	}
	template<class T>
	static ptr create_randu(size_t len) {
		dense_matrix::ptr mat = dense_matrix::create_randu<T>(0, 1, len, 1,
				matrix_layout_t::L_COL);
		return ptr(new col_vec(mat->get_raw_store()));
	}

	static ptr create(dense_matrix::ptr mat);
	static ptr create(detail::matrix_store::ptr store);

	col_vec(): dense_matrix(NULL) {
	}

	col_vec(size_t len, const scalar_type &type): dense_matrix(len, 1,
			matrix_layout_t::L_COL, type) {
	}

	template<class T>
	std::vector<T> conv2std() const {
		assert(is_type<T>());
		std::vector<T> ret(get_length());
		size_t num_portions = get_data().get_num_portions();
		size_t num_eles = 0;
		for (size_t i = 0; i < num_portions; i++) {
			auto portion = get_data().get_portion(i);
			assert(portion->get_raw_arr());
			assert(portion->get_num_cols() == 1);
			detail::local_col_matrix_store::const_ptr col_portion
				= std::dynamic_pointer_cast<const detail::local_col_matrix_store>(
						portion);
			assert(col_portion);
			size_t num_port_eles
				= portion->get_num_rows() * portion->get_num_cols();
			memcpy(ret.data() + num_eles,
					col_portion->get_col(0), num_port_eles * sizeof(T));
			num_eles += num_port_eles;
		}
		assert(num_eles == ret.size());
		return ret;
	}

	size_t get_length() const {
		return get_num_rows();
	}

	col_vec operator=(const dense_matrix &mat) {
		assert(mat.get_num_cols() == 1);
		assign(mat);
		return *this;
	}

	/*
	 * This version of groupby runs aggregation on each group. It only needs
	 * to scan the vector once. If `with_val' is true, this method returns
	 * a data frame with two columns: the first column is a vector of unique
	 * values in the vector; the second column is a vector of aggregation
	 * result for each unique value in the first column.
	 * If `with_val' is false, this method returns a data frame with only
	 * one column, which contains the aggregation result for each unique value.
	 */
	std::shared_ptr<data_frame> groupby(
			std::shared_ptr<const agg_operate> op, bool with_val) const;
};

}

#endif
