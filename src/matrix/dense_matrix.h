#ifndef __DENSE_MATRIX_H__
#define __DENSE_MATRIX_H__

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

#include <stdlib.h>

#include <vector>
#include <memory>

#include "safs_file.h"

#include "generic_type.h"
#include "matrix_header.h"
#include "bulk_operate.h"
#include "bulk_operate_ext.h"
#include "matrix_store.h"
#include "mem_matrix_store.h"
#include "virtual_matrix_store.h"
#include "materialize.h"

namespace fm
{

class bulk_operate;
class bulk_uoperate;
class set_operate;
class arr_apply_operate;
class vector;
class factor_col_vector;
class col_vec;
class data_frame;

/*
 * This class represents a dense matrix and is able to perform computation
 * on the matrix. However, this class can't modify the matrix data. The only
 * way to modify the matrix is to have the pointer point to another matrix.
 */
class dense_matrix
{
public:
	typedef std::shared_ptr<dense_matrix> ptr;
	typedef std::shared_ptr<const dense_matrix> const_ptr;
private:
	detail::matrix_store::const_ptr store;

	detail::matrix_store::const_ptr _conv_store(bool in_mem, int num_nodes) const;
	dense_matrix::ptr multiply_sparse_combined(const dense_matrix &mat,
			matrix_layout_t out_layout) const;
protected:
	dense_matrix(detail::matrix_store::const_ptr store) {
		this->store = store;
	}
	bool verify_inner_prod(const dense_matrix &m,
		const bulk_operate &left_op, const bulk_operate &right_op) const;
	bool verify_mapply2(const dense_matrix &m,
			const bulk_operate &op) const;
	bool verify_apply(matrix_margin margin, const arr_apply_operate &op) const;

	virtual dense_matrix::ptr inner_prod_tall(const dense_matrix &m,
			bulk_operate::const_ptr left_op, bulk_operate::const_ptr right_op,
			matrix_layout_t out_layout) const;
	virtual dense_matrix::ptr inner_prod_wide(const dense_matrix &m,
			bulk_operate::const_ptr left_op, bulk_operate::const_ptr right_op,
			matrix_layout_t out_layout) const;
public:
	static ptr create(size_t nrow, size_t ncol, matrix_layout_t layout,
			const scalar_type &type, int num_nodes = -1, bool in_mem = true,
			safs::safs_file_group::ptr group = NULL) {
		return create_const(type.create_scalar(), nrow, ncol, layout,
				num_nodes, in_mem, group);
	}
	static ptr create(size_t nrow, size_t ncol, matrix_layout_t layout,
			const scalar_type &type, const set_operate &op, int num_nodes = -1,
			bool in_mem = true, safs::safs_file_group::ptr group = NULL);
	static ptr create_const(scalar_variable::ptr val, size_t nrow, size_t ncol,
			matrix_layout_t layout, int num_nodes, bool in_mem,
			safs::safs_file_group::ptr group);
	static ptr create_seq(scalar_variable::ptr start, scalar_variable::ptr stride,
			size_t nrow, size_t ncol, matrix_layout_t layout, bool byrow,
			int num_nodes, bool in_mem, safs::safs_file_group::ptr group);
	static ptr create_repeat(std::shared_ptr<col_vec> vec, size_t nrow, size_t ncol,
			matrix_layout_t layout, bool byrow, int num_nodes = -1);
	static ptr create(detail::matrix_store::const_ptr store);

	static ptr create(std::shared_ptr<const data_frame> df);

	template<class T>
	static ptr create_randu(T min, T max, size_t nrow, size_t ncol,
			matrix_layout_t layout, int num_nodes = -1, bool in_mem = true,
			safs::safs_file_group::ptr group = NULL) {
		set_operate::const_ptr op = create_urand_init<T>(min, max);
		return create(nrow, ncol, layout, get_scalar_type<T>(), *op,
				num_nodes, in_mem, group);
	}
	template<class T>
	static ptr create_randn(T mean, T var, size_t nrow, size_t ncol,
			matrix_layout_t layout, int num_nodes = -1, bool in_mem = true,
			safs::safs_file_group::ptr group = NULL) {
		set_operate::const_ptr op = create_nrand_init<T>(mean, var);
		return create(nrow, ncol, layout, get_scalar_type<T>(), *op,
				num_nodes, in_mem, group);
	}

	template<class T>
	static ptr create_const(T _val, size_t nrow, size_t ncol,
			matrix_layout_t layout, int num_nodes = -1, bool in_mem = true,
			safs::safs_file_group::ptr group = NULL) {
		scalar_variable::ptr val(new scalar_variable_impl<T>(_val));
		return create_const(val, nrow, ncol, layout, num_nodes, in_mem, group);
	}

	template<class T>
	static ptr create_seq(T start, T stride, size_t nrow, size_t ncol,
			matrix_layout_t layout, bool byrow, int num_nodes = -1,
			bool in_mem = true, safs::safs_file_group::ptr group = NULL) {
		scalar_variable::ptr start_val(new scalar_variable_impl<T>(start));
		scalar_variable::ptr stride_val(new scalar_variable_impl<T>(stride));
		return create_seq(start_val, stride_val, nrow, ncol, layout, byrow,
				num_nodes, in_mem, group);
	}

	static ptr rbind(const std::vector<dense_matrix::ptr> &mats);
	static ptr cbind(const std::vector<dense_matrix::ptr> &mats);

	dense_matrix() {
	}
	dense_matrix(size_t nrow, size_t ncol, matrix_layout_t layout,
			const scalar_type &type, int num_nodes = -1, bool in_mem = true,
			safs::safs_file_group::ptr group = NULL);
	virtual ~dense_matrix() {
	}

	std::shared_ptr<vector> conv2vec() const;

	const detail::matrix_store &get_data() const {
		return *store;
	}

	detail::matrix_store::const_ptr get_raw_store() const {
		return store;
	}

	size_t get_entry_size() const {
		return store->get_entry_size();
	}

	size_t get_num_rows() const {
		return store->get_num_rows();
	}

	size_t get_num_cols() const {
		return store->get_num_cols();
	}

	const scalar_type &get_type() const {
		return store->get_type();
	}

	bool is_in_mem() const {
		return store->is_in_mem();
	}

	bool is_wide() const {
		return store->is_wide();
	}

	template<class T>
	bool is_type() const {
		return get_type() == get_scalar_type<T>();
	}

	virtual matrix_layout_t store_layout() const {
		return store->store_layout();
	}

	virtual bool is_virtual() const {
		return store->is_virtual();
	}

	virtual bool materialize_self() const;
	virtual void set_materialize_level(materialize_level level,
			detail::matrix_store::ptr materialize_buf = NULL);
	/*
	 * This returns some virtual matrices that we want to materialize.
	 */
	virtual std::vector<detail::virtual_matrix_store::const_ptr> get_compute_matrices() const;

	/*
	 * We can't change the matrix data that it points to, but we can change
	 * the pointer in the class so that it can point to another matrix data.
	 */
	virtual void assign(const dense_matrix &mat) {
		store = mat.store;
	}

	/*
	 * In these two versions, we get a small number of rows/cols from a matrix.
	 */
	virtual dense_matrix::ptr get_cols(const std::vector<off_t> &idxs) const;
	virtual dense_matrix::ptr get_rows(const std::vector<off_t> &idxs) const;

	/*
	 * In these two versions, we get a large number of rows/cols from a matrix.
	 */
	virtual dense_matrix::ptr get_cols(std::shared_ptr<col_vec> idxs) const;
	virtual dense_matrix::ptr get_rows(std::shared_ptr<col_vec> idxs) const;

	/*
	 * This method creates a new matrix whose columns specified by `idxs' are
	 * replaced by `cols'. This works only for a tall matrix. It returns NULL
	 * on a wide matrix.
	 */
	virtual dense_matrix::ptr set_cols(const std::vector<off_t> &idxs,
			dense_matrix::ptr cols);
	/*
	 * This method creates a new matrix whose rows specified by `idxs' are
	 * replaced by `rows'. This works only for a wide matrix. It returns NULL
	 * on a tall matrix.
	 */
	virtual dense_matrix::ptr set_rows(const std::vector<off_t> &idxs,
			dense_matrix::ptr rows);

	/*
	 * Clone the matrix.
	 * The class can't modify the matrix data that it points to, but it
	 * can modify the pointer. If someone changes in the pointer in the cloned
	 * matrix, it doesn't affect the current matrix.
	 */
	virtual dense_matrix::ptr clone() const {
		return ptr(new dense_matrix(get_raw_store()));
	}
	virtual dense_matrix::ptr deep_copy() const;

	virtual dense_matrix::ptr transpose() const;
	/*
	 * This converts the data layout of the dense matrix.
	 * It actually generates a virtual matrix that represents the matrix
	 * with required data layout.
	 */
	virtual dense_matrix::ptr conv2(matrix_layout_t layout) const;
	/*
	 * This method converts the storage media of the matrix.
	 * It can convert an in-memory matrix to an EM matrix, or vice versa.
	 * The output matrix is always materialized.
	 */
	virtual dense_matrix::ptr conv_store(bool in_mem, int num_nodes) const;
	virtual bool move_store(bool in_mem, int num_nodes) const;

	/*
	 * If the matrix store keeps data in cache, this method will remove
	 * the cache and return true. Otherwise, it returns false.
	 */
	virtual bool drop_cache();

	/*
	 * This returns the number of cached rows/columns in a matrix.
	 */
	virtual size_t get_num_cached() const;

	virtual dense_matrix::ptr inner_prod(const dense_matrix &m,
			bulk_operate::const_ptr left_op, bulk_operate::const_ptr right_op,
			matrix_layout_t out_layout = matrix_layout_t::L_NONE) const;
	virtual dense_matrix::ptr multiply(const dense_matrix &mat,
			matrix_layout_t out_layout = matrix_layout_t::L_NONE) const;
	/*
	 * Compute aggregation on the matrix.
	 * It can aggregate on rows, on columns or on all elements.
	 * By default, we compute aggregation lazily.
	 */
	virtual dense_matrix::ptr aggregate(matrix_margin margin,
			agg_operate::const_ptr op) const;
	virtual std::shared_ptr<scalar_variable> aggregate(
			agg_operate::const_ptr op) const;
	virtual std::shared_ptr<scalar_variable> aggregate(
			bulk_operate::const_ptr op) const;

	/*
	 * This operator groups rows based on the labels in the factor vector
	 * and aggregate the elements of each column.
	 * It outputs a dense matrix whose #cols == this->#cols and #rows == #levels.
	 * Each row of the output dense matrix is the aggregation of all rows in
	 * the input dense matrix that have the same factor.
	 */
	virtual dense_matrix::ptr groupby_row(
			std::shared_ptr<const factor_col_vector> labels,
			agg_operate::const_ptr) const;
	virtual dense_matrix::ptr groupby_row(
			std::shared_ptr<const factor_col_vector> labels,
			bulk_operate::const_ptr) const;

	virtual dense_matrix::ptr mapply_cols(std::shared_ptr<const col_vec> vals,
			bulk_operate::const_ptr op) const;
	virtual dense_matrix::ptr mapply_rows(std::shared_ptr<const col_vec> vals,
			bulk_operate::const_ptr op) const;
	virtual dense_matrix::ptr mapply2(const dense_matrix &m,
			bulk_operate::const_ptr op) const;
	virtual dense_matrix::ptr mapply2(const dense_matrix &m,
			basic_ops::op_idx) const;
	virtual dense_matrix::ptr sapply(bulk_uoperate::const_ptr op) const;
	virtual dense_matrix::ptr apply(matrix_margin margin,
			arr_apply_operate::const_ptr op) const;

	dense_matrix::ptr apply_scalar(scalar_variable::const_ptr var,
			bulk_operate::const_ptr) const;

	dense_matrix::ptr cast_ele_type(const scalar_type &type) const;

	dense_matrix::ptr scale_cols(std::shared_ptr<const col_vec> vals) const;
	dense_matrix::ptr scale_rows(std::shared_ptr<const col_vec> vals) const;

	dense_matrix::ptr add(const dense_matrix &mat) const {
		return this->mapply2(mat, basic_ops::op_idx::ADD);
	}
	dense_matrix::ptr minus(const dense_matrix &mat) const {
		return this->mapply2(mat, basic_ops::op_idx::SUB);
	}
	/*
	 * This performs element-wise multiplication between two matrices.
	 */
	dense_matrix::ptr multiply_ele(const dense_matrix &mat) const {
		return this->mapply2(mat, basic_ops::op_idx::MUL);
	}
	dense_matrix::ptr div(const dense_matrix &mat) const {
		return this->mapply2(mat, basic_ops::op_idx::DIV);
	}
	dense_matrix::ptr pmax(const dense_matrix &mat) const {
		return this->mapply2(mat, basic_ops::op_idx::MAX);
	}

	dense_matrix::ptr abs() const {
		bulk_uoperate::const_ptr op = bulk_uoperate::conv2ptr(
				*get_type().get_basic_uops().get_op(basic_uops::op_idx::ABS));
		return sapply(op);
	}

	dense_matrix::ptr logic_not() const;

	dense_matrix::ptr row_sum() const;
	dense_matrix::ptr col_sum() const;
	dense_matrix::ptr row_norm2() const;
	dense_matrix::ptr col_norm2() const;

	std::shared_ptr<scalar_variable> sum() const {
		if (get_type() == get_scalar_type<bool>()) {
			dense_matrix::ptr tmp = cast_ele_type(get_scalar_type<size_t>());
			return tmp->sum();
		}
		else
			return aggregate(bulk_operate::conv2ptr(
						get_type().get_basic_ops().get_add()));
	}

	std::shared_ptr<scalar_variable> max() const {
		return aggregate(bulk_operate::conv2ptr(
					*get_type().get_basic_ops().get_op(basic_ops::op_idx::MAX)));
	}

	template<class T>
	dense_matrix::ptr multiply_scalar(T val) const {
		scalar_variable::ptr var(new scalar_variable_impl<T>(val));
		bulk_operate::const_ptr op = bulk_operate::conv2ptr(
				var->get_type().get_basic_ops().get_multiply());
		return apply_scalar(var, op);
	}

	template<class T>
	dense_matrix::ptr add_scalar(T val) const {
		scalar_variable::ptr var(new scalar_variable_impl<T>(val));
		bulk_operate::const_ptr op = bulk_operate::conv2ptr(
				var->get_type().get_basic_ops().get_add());
		return apply_scalar(var, op);
	}

	template<class T>
	dense_matrix::ptr minus_scalar(T val) const {
		scalar_variable::ptr var(new scalar_variable_impl<T>(val));
		bulk_operate::const_ptr op = bulk_operate::conv2ptr(
				var->get_type().get_basic_ops().get_sub());
		return apply_scalar(var, op);
	}

	template<class T>
	dense_matrix::ptr lt_scalar(T val) const {
		scalar_variable::ptr var(new scalar_variable_impl<T>(val));
		bulk_operate::const_ptr op = bulk_operate::conv2ptr(
				*var->get_type().get_basic_ops().get_op(basic_ops::op_idx::LT));
		return apply_scalar(var, op);
	}

	template<class T>
	dense_matrix::ptr pmax_scalar(T val) const {
		scalar_variable::ptr var(new scalar_variable_impl<T>(val));
		bulk_operate::const_ptr op = bulk_operate::conv2ptr(
				*var->get_type().get_basic_ops().get_op(basic_ops::op_idx::MAX));
		return apply_scalar(var, op);
	}

	double norm2() const;
};

template<class T>
dense_matrix operator*(const dense_matrix &m, T val)
{
	dense_matrix::ptr ret = m.multiply_scalar<T>(val);
	assert(ret);
	// TODO I shouldn't materialize immediately.
	ret->materialize_self();
	return *ret;
}

template<class T>
dense_matrix operator*(T val, const dense_matrix &m)
{
	dense_matrix::ptr ret = m.multiply_scalar<T>(val);
	assert(ret);
	// TODO I shouldn't materialize immediately.
	ret->materialize_self();
	return *ret;
}

inline dense_matrix operator*(const dense_matrix &m1, const dense_matrix &m2)
{
	dense_matrix::ptr ret = m1.multiply(m2);
	assert(ret);
	// TODO I shouldn't materialize immediately.
	ret->materialize_self();
	return *ret;
}

inline dense_matrix operator+(const dense_matrix &m1, const dense_matrix &m2)
{
	dense_matrix::ptr ret = m1.add(m2);
	assert(ret);
	// TODO I shouldn't materialize immediately.
	ret->materialize_self();
	return *ret;
}

inline dense_matrix operator-(const dense_matrix &m1, const dense_matrix &m2)
{
	dense_matrix::ptr ret = m1.minus(m2);
	assert(ret);
	// TODO I shouldn't materialize immediately.
	ret->materialize_self();
	return *ret;
}

template<class T>
inline T as_scalar(const dense_matrix &m)
{
	assert(m.get_type() == get_scalar_type<T>());
	m.materialize_self();
	assert(m.is_in_mem());
	detail::mem_matrix_store::const_ptr mem_m
		= detail::mem_matrix_store::cast(m.get_raw_store());
	return mem_m->get<T>(0, 0);
}

inline dense_matrix t(const dense_matrix &m)
{
	dense_matrix::ptr ret = m.transpose();
	assert(ret);
	return *ret;
}

}

#endif
