#ifndef __LOCAL_MATRIX_STORE_H__
#define __LOCAL_MATRIX_STORE_H__

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
#include <boost/format.hpp>

#include "comm_exception.h"
#include "log.h"

#include "local_vec_store.h"
#include "matrix_store.h"
#include "raw_data_array.h"

namespace fm
{

class bulk_operate;
class bulk_uoperate;
class agg_operate;
class arr_apply_operate;

namespace detail
{

/*
 * This is a base class that represents part of a matrix.
 * The original matrix can be an SMP matrix, a NUMA matrix,
 * an external-memory matrix or a distributed-memory matrix.
 * It is guaranteed that this matrix stores the portion of the original
 * matrix in the local memory.
 * This class has very similar interface as mem_matrix_store, but it's
 * used for different purpose.
 */
class local_matrix_store: public matrix_store
{
	// These information is created when the local matrix store is created.
	// But we can resize the matrix store, so it exposes part of the original
	// local matrix store.
	off_t global_start_row;
	off_t global_start_col;
	size_t num_rows;
	size_t num_cols;

	// The start row and column exposed to users. They are relative to
	// the original local matrix store.
	off_t local_start_row;
	off_t local_start_col;
	// Which node the matrix data is stored.
	int node_id;
protected:
	struct matrix_info {
		off_t start_row;
		off_t start_col;
		size_t num_rows;
		size_t num_cols;
	};

	struct matrix_info get_global_transpose_info() const {
		matrix_info info;
		info.start_row = global_start_col;
		info.start_col = global_start_row;
		info.num_rows = num_cols;
		info.num_cols = num_rows;
		return info;
	}
	struct matrix_info get_local_transpose_info() const {
		matrix_info info;
		info.start_row = local_start_col;
		info.start_col = local_start_row;
		info.num_rows = get_num_cols();
		info.num_cols = get_num_rows();
		return info;
	}
	void resize_transpose(local_matrix_store &store) const;

	template<class RES_TYPE, class THIS_TYPE>
	local_matrix_store::ptr create_transpose(const THIS_TYPE &this_store) const {
		struct matrix_info t_info = get_global_transpose_info();
		local_matrix_store *store
			= new RES_TYPE(this_store.get_data(),
					t_info.start_row, t_info.start_col, t_info.num_rows,
					t_info.num_cols, get_type(), get_node_id());
		resize_transpose(*store);
		return matrix_store::ptr(store);
	}

	template<class RES_TYPE, class THIS_TYPE>
	local_matrix_store::ptr create_transpose(THIS_TYPE &this_store) {
		struct matrix_info t_info = get_global_transpose_info();
		local_matrix_store *store
			= new RES_TYPE(this_store.get_data(),
					t_info.start_row, t_info.start_col, t_info.num_rows,
					t_info.num_cols, get_type(), get_node_id());
		resize_transpose(*store);
		return matrix_store::ptr(store);
	}
public:
	typedef std::shared_ptr<local_matrix_store> ptr;
	typedef std::shared_ptr<const local_matrix_store> const_ptr;

	struct exposed_area {
		off_t local_start_row;
		off_t local_start_col;
		size_t num_rows;
		size_t num_cols;
	};

	static ptr cast(matrix_store::ptr store) {
		// TODO do I need to check the store object.
		return std::static_pointer_cast<local_matrix_store>(store);
	}

	static const_ptr cast(matrix_store::const_ptr store) {
		// TODO do I need to check the store object.
		return std::static_pointer_cast<const local_matrix_store>(store);
	}

	local_matrix_store(off_t global_start_row, off_t global_start_col,
			size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): matrix_store(nrow, ncol, true, type) {
		this->global_start_row = global_start_row;
		this->global_start_col = global_start_col;
		this->num_rows = nrow;
		this->num_cols = ncol;
		this->local_start_row = 0;
		this->local_start_col = 0;
		this->node_id = node_id;
	}

	size_t get_orig_num_rows() const {
		return num_rows;
	}
	size_t get_orig_num_cols() const {
		return num_cols;
	}

	/*
	 * Test if the matrix store exposes its entire data.
	 */
	bool is_whole() const {
		return local_start_row == 0 && local_start_col == 0
			&& num_rows == get_num_rows() && num_cols == get_num_cols();
	}

	int get_node_id() const {
		return node_id;
	}

	off_t get_local_start_row() const {
		return local_start_row;
	}
	off_t get_local_start_col() const {
		return local_start_col;
	}

	off_t get_global_start_row() const {
		return global_start_row + get_local_start_row();
	}
	off_t get_global_start_col() const {
		return global_start_col + get_local_start_col();
	}

	/*
	 * This determines the shape of the global matrix.
	 */
	virtual bool is_wide() const {
		// A local matrix can be resized. We can use the original size to
		// determine the shape of the global matrix. We can also use
		// the location of the local matrix in the global matrix to determine
		// the shape.
		if (global_start_row == 0 && global_start_col == 0)
			return num_cols > num_rows;
		else
			return global_start_col != 0;
	}

	virtual std::string get_name() const {
		return (boost::format("local_mat(%1%,%2%)") % get_num_rows()
			% get_num_cols()).str();
	}
	virtual std::unordered_map<size_t, size_t> get_underlying_mats() const {
		return std::unordered_map<size_t, size_t>();
	}

	exposed_area get_exposed_area() const {
		exposed_area area;
		area.local_start_row = get_local_start_row();
		area.local_start_col = get_local_start_col();
		area.num_rows = get_num_rows();
		area.num_cols = get_num_cols();
		return area;
	}

	void restore_size(const exposed_area &area) {
		resize(area.local_start_row, area.local_start_col,
				area.num_rows, area.num_cols);
	}

	bool large_copy_from(const local_matrix_store &store);

	// This method is useful for virtual local matrix.
	// By default, it doesn't do anything.
	virtual void materialize_self() const {
	}

	virtual bool resize(off_t local_start_row, off_t local_start_col,
			size_t local_num_rows, size_t local_num_cols);
	virtual void reset_size() {
		local_start_row = 0;
		local_start_col = 0;
		matrix_store::resize(num_rows, num_cols);
	}
	virtual local_matrix_store::ptr conv2(matrix_layout_t layout) const;
	virtual size_t get_all_rows(std::vector<const char *> &rows) const;
	virtual size_t get_all_cols(std::vector<const char *> &cols) const;
	virtual size_t get_all_rows(std::vector<char *> &rows);
	virtual size_t get_all_cols(std::vector<char *> &cols);

	virtual bool hold_orig_data() const = 0;
	virtual const local_raw_array &get_data_ref() const = 0;

	virtual bool read_only() const = 0;
	virtual const char *get_raw_arr() const = 0;
	virtual char *get_raw_arr() = 0;
	virtual const char *get(size_t row, size_t col) const = 0;
	virtual char *get(size_t row, size_t col) = 0;

	virtual bool copy_from(const local_matrix_store &store) = 0;

	virtual matrix_store::const_ptr transpose() const = 0;
	virtual matrix_store::ptr transpose() = 0;

	////// Unsupported methods.

	virtual size_t get_data_id() const {
		return INVALID_MAT_ID;
	}
	virtual bool share_data(const matrix_store &store) const {
		return false;
	}

	virtual std::pair<size_t, size_t> get_portion_size() const {
		assert(0);
		return std::pair<size_t, size_t>(0, 0);
	}
	virtual async_cres_t get_portion_async(
			size_t start_row, size_t start_col, size_t num_rows,
			size_t num_cols, std::shared_ptr<portion_compute> compute) const {
		assert(0);
		return async_cres_t();
	}
	virtual void write_portion_async(
			std::shared_ptr<const local_matrix_store> portion,
			off_t start_row, off_t start_col) {
		assert(0);
	}

	template<class Type>
	Type get(size_t row, size_t col) const {
		return *(const Type *) get(row, col);
	}

	template<class Type>
	void set(size_t row, size_t col, Type val) {
		*(Type *) get(row, col) = val;
	}
};

class local_col_matrix_store: public local_matrix_store
{
	// This is to hold the pointer to the original data so it won't be free'd.
	local_raw_array orig_data_ref;

protected:
	void set_orig_data(const local_raw_array &data_ref) {
		this->orig_data_ref = data_ref;
	}
public:
	typedef std::shared_ptr<local_col_matrix_store> ptr;
	typedef std::shared_ptr<const local_col_matrix_store> const_ptr;

	static ptr cast(local_matrix_store::ptr store) {
		if (store->store_layout() != matrix_layout_t::L_COL) {
			BOOST_LOG_TRIVIAL(error) << "the local matrix store isn't col major";
			return ptr();
		}
		return std::static_pointer_cast<local_col_matrix_store>(store);
	}
	static const_ptr cast(local_matrix_store::const_ptr store) {
		if (store->store_layout() != matrix_layout_t::L_COL) {
			BOOST_LOG_TRIVIAL(error) << "the local matrix store isn't col major";
			return const_ptr();
		}
		return std::static_pointer_cast<const local_col_matrix_store>(store);
	}

	local_col_matrix_store(off_t global_start_row, off_t global_start_col,
			size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_matrix_store(global_start_row, global_start_col,
				nrow, ncol, type, node_id) {
	}
	local_col_matrix_store(const local_raw_array &data_ref, off_t global_start_row,
			off_t global_start_col, size_t nrow, size_t ncol,
			const scalar_type &type, int node_id): local_matrix_store(
				global_start_row, global_start_col, nrow, ncol, type, node_id) {
		this->orig_data_ref = data_ref;
	}

	virtual const local_raw_array &get_data_ref() const {
		return orig_data_ref;
	}

	virtual bool hold_orig_data() const {
		return orig_data_ref.get_raw() != NULL;
	}

	// Get the offset of the entry in the original local matrix store.
	off_t get_orig_offset(off_t row, off_t col) const {
		return (col + get_local_start_col()) * get_orig_num_rows()
			+ row + get_local_start_row();
	}

	virtual void reset_data();
	virtual void set_data(const set_operate &op);
	virtual bool copy_from(const local_matrix_store &store);

	virtual const char *get_col(size_t col) const = 0;
	virtual char *get_col(size_t col) = 0;

	virtual const char *get(size_t row, size_t col) const {
		return get_col(col) + row * get_entry_size();
	}

	virtual char *get(size_t row, size_t col) {
		return get_col(col) + row * get_entry_size();
	}

	virtual matrix_layout_t store_layout() const {
		return matrix_layout_t::L_COL;
	}

	virtual local_matrix_store::const_ptr get_portion(
			size_t local_start_row, size_t local_start_col, size_t num_rows,
			size_t num_cols) const;
	virtual local_matrix_store::ptr get_portion(
			size_t local_start_row, size_t local_start_col, size_t num_rows,
			size_t num_cols);
	virtual int get_portion_node_id(size_t id) const {
		return orig_data_ref.get_node_id();
	}
};

class local_row_matrix_store: public local_matrix_store
{
	// This is to hold the pointer to the original data so it won't be free'd.
	local_raw_array orig_data_ref;

protected:
	void set_orig_data(const local_raw_array &data_ref) {
		this->orig_data_ref = data_ref;
	}
public:
	typedef std::shared_ptr<local_row_matrix_store> ptr;
	typedef std::shared_ptr<const local_row_matrix_store> const_ptr;

	static ptr cast(local_matrix_store::ptr store) {
		if (store->store_layout() != matrix_layout_t::L_ROW) {
			BOOST_LOG_TRIVIAL(error) << "the local matrix store isn't row major";
			return ptr();
		}
		return std::static_pointer_cast<local_row_matrix_store>(store);
	}
	static const_ptr cast(local_matrix_store::const_ptr store) {
		if (store->store_layout() != matrix_layout_t::L_ROW) {
			BOOST_LOG_TRIVIAL(error) << "the local matrix store isn't row major";
			return const_ptr();
		}
		return std::static_pointer_cast<const local_row_matrix_store>(store);
	}

	local_row_matrix_store(off_t global_start_row, off_t global_start_col,
			size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_matrix_store(global_start_row, global_start_col,
				nrow, ncol, type, node_id) {
	}
	local_row_matrix_store(const local_raw_array &data_ref, off_t global_start_row,
			off_t global_start_col, size_t nrow, size_t ncol,
			const scalar_type &type, int node_id): local_matrix_store(
				global_start_row, global_start_col, nrow, ncol, type, node_id) {
		this->orig_data_ref = data_ref;
	}

	virtual const local_raw_array &get_data_ref() const {
		return orig_data_ref;
	}

	virtual bool hold_orig_data() const {
		return orig_data_ref.get_raw() != NULL;
	}

	// Get the offset of the entry in the original local matrix store.
	off_t get_orig_offset(off_t row, off_t col) const {
		return (row + get_local_start_row()) * get_orig_num_cols()
			+ col + get_local_start_col();
	}

	virtual void reset_data();
	virtual void set_data(const set_operate &op);
	virtual bool copy_from(const local_matrix_store &store);

	virtual const char *get_row(size_t row) const = 0;
	virtual char *get_row(size_t row) = 0;

	virtual matrix_layout_t store_layout() const {
		return matrix_layout_t::L_ROW;
	}

	virtual const char *get(size_t row, size_t col) const {
		return get_row(row) + col * get_entry_size();
	}
	virtual char *get(size_t row, size_t col) {
		return get_row(row) + col * get_entry_size();
	}

	virtual local_matrix_store::const_ptr get_portion(
			size_t local_start_row, size_t local_start_col, size_t num_rows,
			size_t num_cols) const;
	virtual local_matrix_store::ptr get_portion(
			size_t local_start_row, size_t local_start_col, size_t num_rows,
			size_t num_cols);
	virtual int get_portion_node_id(size_t id) const {
		return orig_data_ref.get_node_id();
	}
};

/*
 * A matrix owns data to store portion of data in a column-major matrix.
 */
class local_buf_col_matrix_store: public local_col_matrix_store
{
	local_raw_array data;
public:
	local_buf_col_matrix_store(off_t global_start_row, off_t global_start_col,
			size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_col_matrix_store(global_start_row, global_start_col,
				nrow, ncol, type, node_id) {
		if (nrow * ncol > 0) {
			data = local_raw_array(nrow * ncol * type.get_size());
			set_orig_data(data);
		}
	}

	local_buf_col_matrix_store(const local_raw_array &data,
			off_t global_start_row, off_t global_start_col, size_t nrow,
			size_t ncol, const scalar_type &type,
			int node_id): local_col_matrix_store(data, global_start_row,
				global_start_col, nrow, ncol, type, node_id) {
		this->data = data;
	}

	const local_raw_array &get_data() const {
		return data;
	}

	virtual bool read_only() const {
		return false;
	}

	virtual const char *get_raw_arr() const {
		// If this matrix has only one column, then all data is stored
		// contigously.
		if (get_num_cols() > 1 && (get_local_start_row() > 0
					|| get_num_rows() < get_orig_num_rows()))
			return NULL;
		else
			return data.get_raw() + get_orig_offset(0, 0) * get_entry_size();
	}

	virtual char *get_raw_arr() {
		// If this matrix has only one column, then all data is stored
		// contigously.
		if (get_num_cols() > 1 && (get_local_start_row() > 0
					|| get_num_rows() < get_orig_num_rows()))
			return NULL;
		else
			return data.get_raw() + get_orig_offset(0, 0) * get_entry_size();
	}

	virtual const char *get_col(size_t col) const {
		return data.get_raw() + get_orig_offset(0, col) * get_entry_size();
	}
	virtual char *get_col(size_t col) {
		return data.get_raw() + get_orig_offset(0, col) * get_entry_size();
	}

	virtual matrix_store::const_ptr transpose() const;
	virtual matrix_store::ptr transpose();
};

/*
 * A matrix owns data to store portion of data in a row-major matrix.
 */
class local_buf_row_matrix_store: public local_row_matrix_store
{
	local_raw_array data;
public:
	local_buf_row_matrix_store(off_t global_start_row, off_t global_start_col,
			size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_row_matrix_store(global_start_row, global_start_col,
				nrow, ncol, type, node_id) {
		if (nrow * ncol > 0) {
			data = local_raw_array(nrow * ncol * type.get_size());
			set_orig_data(data);
		}
	}

	local_buf_row_matrix_store(const local_raw_array &data, off_t global_start_row,
			off_t global_start_col, size_t nrow, size_t ncol,
			const scalar_type &type, int node_id): local_row_matrix_store(data,
				global_start_row, global_start_col, nrow, ncol, type, node_id) {
		this->data = data;
	}

	const local_raw_array &get_data() const {
		return data;
	}

	virtual bool read_only() const {
		return false;
	}

	virtual const char *get_raw_arr() const {
		// If this matrix has only one row, then all data is stored
		// contigously.
		if (get_num_rows() > 1 && (get_local_start_col() > 0
					|| get_num_cols() < get_orig_num_cols()))
			return NULL;
		else
			return data.get_raw() + get_orig_offset(0, 0) * get_entry_size();
	}

	virtual char *get_raw_arr() {
		// If this matrix has only one row, then all data is stored
		// contigously.
		if (get_num_rows() > 1 && (get_local_start_col() > 0
					|| get_num_cols() < get_orig_num_cols()))
			return NULL;
		else
			return data.get_raw() + get_orig_offset(0, 0) * get_entry_size();
	}

	virtual const char *get_row(size_t row) const {
		return data.get_raw() + get_orig_offset(row, 0) * get_entry_size();
	}

	virtual char *get_row(size_t row) {
		return data.get_raw() + get_orig_offset(row, 0) * get_entry_size();
	}

	virtual matrix_store::const_ptr transpose() const;
	virtual matrix_store::ptr transpose();
};

/*
 * A matrix that references portion of data in another column-major matrix.
 * The referenced data is stored contiguously.
 */
class local_ref_contig_col_matrix_store: public local_col_matrix_store
{
	char *data;
public:
	local_ref_contig_col_matrix_store(char *data, off_t global_start_row,
			off_t global_start_col, size_t nrow, size_t ncol,
			const scalar_type &type, int node_id): local_col_matrix_store(
				global_start_row, global_start_col, nrow, ncol, type, node_id) {
		this->data = data;
	}

	local_ref_contig_col_matrix_store(const local_raw_array &data_ref, char *data,
			off_t global_start_row, off_t global_start_col, size_t nrow, size_t ncol,
			const scalar_type &type, int node_id): local_col_matrix_store(data_ref,
				global_start_row, global_start_col, nrow, ncol, type, node_id) {
		this->data = data;
	}

	char *get_data() {
		return data;
	}

	const char *get_data() const {
		return data;
	}

	virtual bool read_only() const {
		return false;
	}

	virtual const char *get_raw_arr() const {
		// If this matrix has only one column, then all data is stored
		// contigously.
		if (get_num_cols() > 1 && (get_local_start_row() > 0
					|| get_num_rows() < get_orig_num_rows()))
			return NULL;
		else
			return data + get_orig_offset(0, 0) * get_entry_size();
	}

	virtual char *get_raw_arr() {
		// If this matrix has only one column, then all data is stored
		// contigously.
		if (get_num_cols() > 1 && (get_local_start_row() > 0
					|| get_num_rows() < get_orig_num_rows()))
			return NULL;
		else
			return data + get_orig_offset(0, 0) * get_entry_size();
	}

	virtual const char *get_col(size_t col) const {
		return data + get_orig_offset(0, col) * get_entry_size();
	}
	virtual char *get_col(size_t col) {
		return data + get_orig_offset(0, col) * get_entry_size();
	}

	virtual matrix_store::const_ptr transpose() const;
	virtual matrix_store::ptr transpose();
};

/*
 * A matrix that references portion of data in another row-major matrix.
 * The referenced data is stored contiguously.
 */
class local_ref_contig_row_matrix_store: public local_row_matrix_store
{
	char *data;
public:
	local_ref_contig_row_matrix_store(char *data, off_t global_start_row,
			off_t global_start_col, size_t nrow, size_t ncol,
			const scalar_type &type, int node_id): local_row_matrix_store(
				global_start_row, global_start_col, nrow, ncol, type, node_id) {
		this->data = data;
	}

	local_ref_contig_row_matrix_store(const local_raw_array &data_ref, char *data,
			off_t global_start_row, off_t global_start_col, size_t nrow, size_t ncol,
			const scalar_type &type, int node_id): local_row_matrix_store(data_ref,
				global_start_row, global_start_col, nrow, ncol, type, node_id) {
		this->data = data;
	}

	char *get_data() {
		return data;
	}

	const char *get_data() const {
		return data;
	}

	virtual bool read_only() const {
		return false;
	}

	virtual const char *get_raw_arr() const {
		// If this matrix has only one row, then all data is stored contigously.
		if (get_num_rows() > 1 && (get_local_start_col() > 0
					|| get_num_cols() < get_orig_num_cols()))
			return NULL;
		else
			return data + get_orig_offset(0, 0) * get_entry_size();
	}

	virtual char *get_raw_arr() {
		// If this matrix has only one row, then all data is stored contigously.
		if (get_num_rows() > 1 && (get_local_start_col() > 0
					|| get_num_cols() < get_orig_num_cols()))
			return NULL;
		else
			return data + get_orig_offset(0, 0) * get_entry_size();
	}

	virtual const char *get_row(size_t row) const {
		return data + get_orig_offset(row, 0) * get_entry_size();
	}

	virtual char *get_row(size_t row) {
		return data + get_orig_offset(row, 0) * get_entry_size();
	}

	virtual matrix_store::const_ptr transpose() const;
	virtual matrix_store::ptr transpose();
};

/*
 * A matrix that references portion of data in another column-major matrix.
 * The referenced data isn't guaranteed to be stored contiguously.
 */
class local_ref_col_matrix_store: public local_col_matrix_store
{
	std::vector<char *> cols;
public:
	local_ref_col_matrix_store(const std::vector<char *> &cols,
			off_t global_start_row, off_t global_start_col,
			size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_col_matrix_store(global_start_row,
				global_start_col, nrow, cols.size(), type, node_id) {
		this->cols = cols;
		assert(cols.size() == ncol);
	}

	local_ref_col_matrix_store(const local_raw_array &data_ref,
			const std::vector<char *> &cols, off_t global_start_row,
			off_t global_start_col, size_t nrow, size_t ncol,
			const scalar_type &type, int node_id): local_col_matrix_store(
				data_ref, global_start_row, global_start_col, nrow, cols.size(),
				type, node_id) {
		this->cols = cols;
		assert(cols.size() == ncol);
	}

	const std::vector<char *> &get_data() {
		return cols;
	}

	std::vector<const char *> get_data() const {
		return std::vector<const char *>(cols.begin(), cols.end());
	}

	virtual bool read_only() const {
		return false;
	}

	virtual const char *get_raw_arr() const {
		if (cols.size() == 1)
			return get_col(0);
		else
			return NULL;
	}

	virtual char *get_raw_arr() {
		if (cols.size() == 1)
			return get_col(0);
		else
			return NULL;
	}

	virtual const char *get_col(size_t col) const {
		return cols[col + get_local_start_col()]
			+ get_local_start_row() * get_entry_size();
	}
	virtual char *get_col(size_t col) {
		return cols[col + get_local_start_col()]
			+ get_local_start_row() * get_entry_size();
	}

	virtual matrix_store::const_ptr transpose() const;
	virtual matrix_store::ptr transpose();
};

/*
 * A matrix that references portion of data in another row-major matrix.
 * The referenced data isn't guaranteed to be stored contiguously.
 */
class local_ref_row_matrix_store: public local_row_matrix_store
{
	std::vector<char *> rows;
public:
	local_ref_row_matrix_store(const std::vector<char *> &rows,
			off_t global_start_row, off_t global_start_col,
			size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_row_matrix_store(global_start_row,
				global_start_col, rows.size(), ncol, type, node_id) {
		this->rows = rows;
		assert(rows.size() == nrow);
	}

	local_ref_row_matrix_store(const local_raw_array &data_ref,
			const std::vector<char *> &rows, off_t global_start_row,
			off_t global_start_col, size_t nrow, size_t ncol,
			const scalar_type &type, int node_id): local_row_matrix_store(
				data_ref, global_start_row, global_start_col, rows.size(), ncol,
				type, node_id) {
		this->rows = rows;
		assert(rows.size() == nrow);
	}

	const std::vector<char *> &get_data() {
		return rows;
	}

	std::vector<const char *> get_data() const {
		return std::vector<const char *>(rows.begin(), rows.end());
	}

	virtual bool read_only() const {
		return false;
	}

	virtual const char *get_raw_arr() const {
		if (rows.size() == 1)
			return get_row(0);
		else
			return NULL;
	}

	virtual char *get_raw_arr() {
		if (rows.size() == 1)
			return get_row(0);
		else
			return NULL;
	}

	virtual const char *get_row(size_t row) const {
		return rows[row + get_local_start_row()]
			+ get_local_start_col() * get_entry_size();
	}

	virtual char *get_row(size_t row) {
		return rows[row + get_local_start_row()]
			+ get_local_start_col() * get_entry_size();
	}

	virtual matrix_store::const_ptr transpose() const;
	virtual matrix_store::ptr transpose();
};

/*
 * A matrix that references portion of const data in another column-major matrix.
 * The referenced data is stored contiguously.
 */
class local_cref_contig_col_matrix_store: public local_col_matrix_store
{
	const char *data;
public:
	local_cref_contig_col_matrix_store(const char *data, off_t global_start_row,
			off_t global_start_col, size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_col_matrix_store(global_start_row,
				global_start_col, nrow, ncol, type, node_id) {
		this->data = data;
	}

	local_cref_contig_col_matrix_store(const local_raw_array &data_ref, const char *data,
			off_t global_start_row, off_t global_start_col, size_t nrow, size_t ncol,
			const scalar_type &type, int node_id): local_col_matrix_store(data_ref,
				global_start_row, global_start_col, nrow, ncol, type, node_id) {
		this->data = data;
	}

	const char *get_data() const {
		return data;
	}

	virtual bool read_only() const {
		return true;
	}

	virtual const char *get_raw_arr() const {
		// If this matrix has only one column, then all data is stored
		// contigously.
		if (get_num_cols() > 1 && (get_local_start_row() > 0
					|| get_num_rows() < get_orig_num_rows()))
			return NULL;
		else
			return data + get_orig_offset(0, 0) * get_entry_size();
	}
	virtual const char *get_col(size_t col) const {
		return data + get_orig_offset(0, col) * get_entry_size();
	}

	virtual char *get_raw_arr() {
		assert(0);
		return NULL;
	}
	virtual char *get_col(size_t col) {
		assert(0);
		return NULL;
	}

	virtual matrix_store::const_ptr transpose() const;
	virtual matrix_store::ptr transpose();
};

/*
 * A matrix that references portion of const data in another row-major matrix.
 * The referenced data is stored contiguously.
 */
class local_cref_contig_row_matrix_store: public local_row_matrix_store
{
	const char *data;
public:
	local_cref_contig_row_matrix_store(const char *data, off_t global_start_row,
			off_t global_start_col, size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_row_matrix_store(global_start_row,
				global_start_col, nrow, ncol, type, node_id) {
		this->data = data;
	}

	local_cref_contig_row_matrix_store(const local_raw_array &data_ref, const char *data,
			off_t global_start_row, off_t global_start_col, size_t nrow, size_t ncol,
			const scalar_type &type, int node_id): local_row_matrix_store(data_ref,
				global_start_row, global_start_col, nrow, ncol, type, node_id) {
		this->data = data;
	}

	const char *get_data() const {
		return data;
	}

	virtual bool read_only() const {
		return true;
	}

	virtual const char *get_raw_arr() const {
		// If this matrix has only one row, then all data is stored contigously.
		if (get_num_rows() > 1 && (get_local_start_col() > 0
					|| get_num_cols() < get_orig_num_cols()))
			return NULL;
		else
			return data + get_orig_offset(0, 0) * get_entry_size();
	}
	virtual const char *get_row(size_t row) const {
		return data + get_orig_offset(row, 0) * get_entry_size();
	}
	virtual char *get_raw_arr() {
		assert(0);
		return NULL;
	}
	virtual char *get_row(size_t row) {
		assert(0);
		return NULL;
	}

	virtual matrix_store::const_ptr transpose() const;
	virtual matrix_store::ptr transpose();
};

/*
 * A matrix that references portion of data in another column-major matrix.
 * The referenced data isn't guaranteed to be stored contiguously.
 */
class local_cref_col_matrix_store: public local_col_matrix_store
{
	std::vector<const char *> cols;
public:
	local_cref_col_matrix_store(const std::vector<const char *> &cols,
			off_t global_start_row, off_t global_start_col,
			size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_col_matrix_store(global_start_row,
				global_start_col, nrow, cols.size(), type, node_id) {
		this->cols = cols;
		assert(cols.size() == ncol);
	}

	local_cref_col_matrix_store(const local_raw_array &data_ref,
			const std::vector<const char *> &cols, off_t global_start_row,
			off_t global_start_col, size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_col_matrix_store(data_ref, global_start_row,
				global_start_col, nrow, cols.size(), type, node_id) {
		this->cols = cols;
		assert(cols.size() == ncol);
	}

	const std::vector<const char *> get_data() const {
		return cols;
	}

	virtual bool read_only() const {
		return true;
	}

	virtual const char *get_col(size_t col) const {
		return cols[col + get_local_start_col()]
			+ get_local_start_row() * get_entry_size();
	}

	virtual const char *get_raw_arr() const {
		if (cols.size() == 1)
			return get_col(0);
		else
			return NULL;
	}
	virtual char *get_raw_arr() {
		assert(0);
		return NULL;
	}
	virtual char *get_col(size_t col) {
		assert(0);
		return NULL;
	}

	virtual matrix_store::const_ptr transpose() const;
	virtual matrix_store::ptr transpose();
};

/*
 * A matrix that references portion of data in another row-major matrix.
 * The referenced data isn't guaranteed to be stored contiguously.
 */
class local_cref_row_matrix_store: public local_row_matrix_store
{
	std::vector<const char *> rows;
public:
	local_cref_row_matrix_store(const std::vector<const char *> &rows,
			off_t global_start_row, off_t global_start_col,
			size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_row_matrix_store(global_start_row,
				global_start_col, rows.size(), ncol, type, node_id) {
		this->rows = rows;
		assert(rows.size() == nrow);
	}

	local_cref_row_matrix_store(const local_raw_array &data_ref,
			const std::vector<const char *> &rows, off_t global_start_row,
			off_t global_start_col, size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_row_matrix_store(data_ref, global_start_row,
				global_start_col, rows.size(), ncol, type, node_id) {
		this->rows = rows;
		assert(rows.size() == nrow);
	}

	const std::vector<const char *> get_data() const {
		return rows;
	}

	virtual bool read_only() const {
		return true;
	}

	virtual const char *get_row(size_t row) const {
		return rows[row + get_local_start_row()]
			+ get_local_start_col() * get_entry_size();
	}

	virtual const char *get_raw_arr() const {
		if (rows.size() == 1)
			return get_row(0);
		else
			return NULL;
	}
	virtual char *get_raw_arr() {
		assert(0);
		return NULL;
	}
	virtual char *get_row(size_t row) {
		assert(0);
		return NULL;
	}

	virtual matrix_store::const_ptr transpose() const;
	virtual matrix_store::ptr transpose();
};

class lvirtual_col_matrix_store: public local_col_matrix_store
{
public:
	lvirtual_col_matrix_store(off_t global_start_row, off_t global_start_col,
			size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_col_matrix_store(global_start_row,
				global_start_col, nrow, ncol, type, node_id) {
	}

	virtual bool is_virtual() const {
		return true;
	}

	virtual bool read_only() const {
		return true;
	}

	using local_col_matrix_store::get_raw_arr;
	virtual char *get_raw_arr() {
		return NULL;
	}

	virtual char *get(size_t row, size_t col) {
		return NULL;
	}

	virtual bool copy_from(const local_matrix_store &store) {
		return false;
	}

	virtual void reset_data() {
	}

	virtual void set_data(const set_operate &op) {
	}

	using local_col_matrix_store::transpose;
	virtual matrix_store::ptr transpose() {
		return matrix_store::ptr();
	}

	using local_col_matrix_store::get_col;
	virtual char *get_col(size_t col) {
		return NULL;
	}

	virtual local_matrix_store::const_ptr get_portion(
			size_t local_start_row, size_t local_start_col, size_t num_rows,
			size_t num_cols) const {
		assert(0);
		return local_matrix_store::const_ptr();
	}
	virtual local_matrix_store::ptr get_portion(
			size_t local_start_row, size_t local_start_col, size_t num_rows,
			size_t num_cols) {
		assert(0);
		return local_matrix_store::ptr();
	}
};

class lvirtual_row_matrix_store: public local_row_matrix_store
{
public:
	lvirtual_row_matrix_store(off_t global_start_row, off_t global_start_col,
			size_t nrow, size_t ncol, const scalar_type &type,
			int node_id): local_row_matrix_store(global_start_row,
				global_start_col, nrow, ncol, type, node_id) {
	}

	virtual bool is_virtual() const {
		return true;
	}

	virtual bool read_only() const {
		return true;
	}

	using local_row_matrix_store::get_raw_arr;
	virtual char *get_raw_arr() {
		return NULL;
	}

	virtual char *get(size_t row, size_t col) {
		return NULL;
	}

	virtual bool copy_from(const local_matrix_store &store) {
		return false;
	}

	virtual void reset_data() {
	}

	virtual void set_data(const set_operate &op) {
	}

	using local_row_matrix_store::transpose;
	virtual matrix_store::ptr transpose() {
		return matrix_store::ptr();
	}

	using local_row_matrix_store::get_row;
	virtual char *get_row(size_t row) {
		return NULL;
	}

	virtual local_matrix_store::const_ptr get_portion(
			size_t local_start_row, size_t local_start_col, size_t num_rows,
			size_t num_cols) const {
		assert(0);
		return local_matrix_store::const_ptr();
	}
	virtual local_matrix_store::ptr get_portion(
			size_t local_start_row, size_t local_start_col, size_t num_rows,
			size_t num_cols) {
		assert(0);
		return local_matrix_store::ptr();
	}
};

enum part_dim_t
{
	// No need for partition.
	PART_NONE,
	// Partition on the first dimension. i.e., we break up columns into parts.
	PART_DIM1,
	// Partition on the second dimension.
	PART_DIM2,
};

/*
 * These are the general operations on the local matrix store.
 */
void aggregate(const local_matrix_store &store, const agg_operate &op,
		int margin, part_dim_t dim, local_matrix_store &res);
void mapply2(const local_matrix_store &m1, const local_matrix_store &m2,
			const bulk_operate &op, part_dim_t dim, local_matrix_store &res);
void sapply(const local_matrix_store &store, const bulk_uoperate &op,
		part_dim_t dim, local_matrix_store &res);
void apply(int margin, const arr_apply_operate &op,
		const local_matrix_store &in_mat, local_matrix_store &out_mat);
/*
 * We perform inner product differently based on the shape of the left matrix.
 * We expect inner product on a tall left matrix to output a tall matrix
 * and inner product on a wide left matrix to output a small matrix.
 */
void inner_prod_tall(const local_matrix_store &m1, const local_matrix_store &m2,
		const bulk_operate &left_op, const bulk_operate &right_op,
		local_matrix_store &res);
void inner_prod_wide(const local_matrix_store &m1, const local_matrix_store &m2,
		const bulk_operate &left_op, const bulk_operate &right_op,
		local_matrix_store &res);
void mapply_cols(const local_matrix_store &m1, const local_vec_store &vals,
		const bulk_operate &op, local_matrix_store &m2);
void mapply_rows(const local_matrix_store &m1, const local_vec_store &vals,
		const bulk_operate &op, local_matrix_store &m2);
/*
 * This group by rows/columns on a matrix.
 */
bool groupby(const detail::local_matrix_store &labels,
		const detail::local_matrix_store &mat, const agg_operate &op,
		matrix_margin margin, part_dim_t dim,
		detail::local_matrix_store &results, std::vector<bool> &agg_flags);

/*
 * BLAS matrix multiplication: a tall matrix * a small matrix.
 * It may create two buffer matrices to store data from the input and
 * output tall matrices if data in the input and output matrices isn't
 * stored in contiguous memory.
 */
void matrix_tall_multiply(const local_matrix_store &left,
		const local_matrix_store &right, local_matrix_store &out,
		std::pair<local_matrix_store::ptr, local_matrix_store::ptr> &bufs);
/*
 * BLAS matrix multiplication: a wide matrix * a tall matrix.
 * It may create two buffer matrices to store data from the input matrices
 * if data in the input matrices isn't stored in contiguous memory.
 */
void matrix_wide_multiply(const local_matrix_store &left,
		const local_matrix_store &right, part_dim_t dim, local_matrix_store &out,
		std::pair<local_matrix_store::ptr, local_matrix_store::ptr> &bufs);

void materialize_tall(const std::vector<detail::local_matrix_store::const_ptr> &ins);
void materialize_wide(const std::vector<detail::local_matrix_store::const_ptr> &ins);

/*
 * This returns the length in the partitioned dimension for a matrix partition
 * that can fit in CPU cache.
 */
size_t get_part_dim_len(const local_matrix_store &mat, part_dim_t dim);
/*
 * This returns the length in the long dimension for a partition of the matrix
 * that can fit in CPU cache.
 */
size_t get_long_dim_len(const local_matrix_store &mat);
/*
 * Some computation takes two matrices as input. We use the matrix with
 * the larger length in the short dimension to determine the length of
 * the long dimension.
 */
size_t get_long_dim_len(const local_matrix_store &mat1,
		const local_matrix_store &mat2);

}

}

#endif
