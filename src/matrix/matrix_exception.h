#ifndef __MATRIX_EXCEPTION_H__
#define __MATRIX_EXCEPTION_H__

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

namespace fm
{

class wrong_format: public std::exception
{
	std::string msg;
public:
	wrong_format(const std::string &msg) {
		this->msg = msg;
	}

	~wrong_format() throw() {
	}

	const char* what() const throw() {
		return msg.c_str();
	}
};

class alloc_error: public std::exception
{
	std::string msg;
public:
	alloc_error(const std::string &msg) {
		this->msg = msg;
	}

	~alloc_error() throw() {
	}

	const char* what() const throw() {
		return msg.c_str();
	}
};

}

#endif
