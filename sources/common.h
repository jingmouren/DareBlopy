//   Copyright 2019-2020 Stanislav Pidhorskyi
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

#pragma once
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <stdarg.h>
#include <memory>
#include <exception>

#if defined(__GNUC__)
#define HIDDEN __attribute__ ((visibility("hidden")))
#else
#define HIDDEN
#endif

#define PYBIND11_NUMPY_OBJECT_DTYPE(Type) \
    namespace pybind11 { namespace detail { \
        template <> struct npy_format_descriptor<Type> { \
        public: \
            enum { value = npy_api::NPY_OBJECT_ }; \
            static pybind11::dtype dtype() { \
                if (auto ptr = npy_api::get().PyArray_DescrFromType_(value)) { \
                    return reinterpret_borrow<pybind11::dtype>(ptr); \
                } \
                pybind11_fail("Unsupported buffer format!"); \
            } \
            static constexpr auto name = _("object"); \
        }; \
    }}

namespace py = pybind11;

// We tell pybind11 that we want to handle ndarrays of bytes and python objects
PYBIND11_NUMPY_OBJECT_DTYPE(py::bytes)
PYBIND11_NUMPY_OBJECT_DTYPE(py::object)


typedef py::array_t<uint8_t, py::array::c_style> ndarray_uint8;
typedef py::array_t<int64_t, py::array::c_style> ndarray_int64;
typedef py::array_t<float, py::array::c_style> ndarray_float32;
typedef py::array_t<py::object, py::array::c_style> ndarray_object;


inline std::string string_format(const std::string fmt_str, va_list ap)
{
	int final_n, n = ((int)fmt_str.size()) * 2; /* Reserve two times as much as the length of the fmt_str */
	std::unique_ptr<char[]> formatted;
	while(1) {
		va_list ap_copy;
		va_copy(ap_copy, ap);
		formatted.reset(new char[n]); /* Wrap the plain char array into the unique_ptr */
		strcpy(&formatted[0], fmt_str.c_str());
		final_n = vsnprintf(&formatted[0], n, fmt_str.c_str(), ap_copy);
		if (final_n < 0 || final_n >= n)
			n += abs(final_n - n + 1);
		else
			break;
	}
	return std::string(formatted.get());
}

// Like snprintf, but does not need buffer and returns std::string
inline std::string string_format(const std::string fmt_str, ...)
{
	va_list ap;
	va_start(ap, fmt_str);
	std::string result = string_format(fmt_str, ap);
	va_end(ap);
	return result;
}

class runtime_error: public std::runtime_error
{
public:
	runtime_error(const std::string fmt_str, ...):
			std::runtime_error(string_format(fmt_str, (va_start(ap, fmt_str), ap)))
	{
		va_end(ap);
	}

	runtime_error(const runtime_error &) = default;
	runtime_error(runtime_error &&) = default;
	~runtime_error() = default;

private:
	va_list ap;
};


// Returns special allocator for reducing data copying, see https://github.com/pybind/pybind11/issues/1236#issuecomment-527730864
// It also creates a bytes object and stores pointer to newly created object in `bytesObject`
inline std::function<void*(size_t)> GetBytesAllocator(PyBytesObject*& bytesObject)
{
    // this lambda will create bytes object of the given size and
    // will return pointer to the allocated memory, like malloc would do
	auto alloc = [&bytesObject](size_t size)
	{
		// here we overallocate by sizeof(uint32_t) to allow sizeof(uint32_t) buffer overruns to enable subtle
		// optimizations.
		// e.g. readying a chunk of data with crc32c checksum in open `read` invocation. This might make a small
		// difference if the disk is network attached.
		bytesObject = (PyBytesObject*) PyObject_Malloc(offsetof(PyBytesObject, ob_sval) + size + 1 + sizeof(uint32_t));
		PyObject_INIT_VAR(bytesObject, &PyBytes_Type, size);
		bytesObject->ob_shash = -1;
		bytesObject->ob_sval[size] = '\0';
		return bytesObject->ob_sval;
	};
	return alloc;
}
