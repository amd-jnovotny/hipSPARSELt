/*******************************************************************************
 *
 * MIT License
 *
 * Copyright (c) 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 *******************************************************************************/
#pragma once
#ifndef ROCSPARSELT_SPMM_UTILS_HPP
#define ROCSPARSELT_SPMM_UTILS_HPP
#include "handle.h"
#include "hipsparselt_ostream.hpp"
#include <cxxabi.h>

inline rocsparselt_status getOriginalSizes(rocsparselt_operation opA,
                                           rocsparselt_operation opB,
                                           int64_t               num_rows_a,
                                           int64_t               num_cols_a,
                                           int64_t               num_rows_b,
                                           int64_t               num_cols_b,
                                           int64_t&              m,
                                           int64_t&              n,
                                           int64_t&              k)
{
    // values of num_* are values after been transposed, redirect to before which been transposed.
    // initialized m,n,k by NN.
    m = num_rows_a, n = num_cols_b, k = num_cols_a;
    if(opA == rocsparselt_operation_transpose)
    {
        m = num_cols_a;
        k = num_rows_a;
    }
    if(opB == rocsparselt_operation_transpose)
    {
        n = num_rows_b;
        if(k != num_cols_b)
        {
            hipsparselt_cerr << "A, B matrix size are not matched" << std::endl;
            return rocsparselt_status_invalid_size;
        }
    }
    else if(k != num_rows_b)
    {
        hipsparselt_cerr << "A, B matrix size are not matched" << std::endl;
        return rocsparselt_status_invalid_size;
    }

    return rocsparselt_status_success;
}

/*******************************************************************************
 * Get the offset of the metatdata (in bytes)
 ******************************************************************************/
inline int64_t rocsparselt_metadata_offset_in_compressed_matrix(int64_t              num_cols,
                                                                int64_t              ld,
                                                                int                  num_batches,
                                                                rocsparselt_datatype type)
{
    int64_t batch_stride = ld * num_cols;

    auto datatype_bpe = [&] {
        switch(type)
        {
        case rocsparselt_datatype_f32_r:
            return 4;
        case rocsparselt_datatype_f16_r:
        case rocsparselt_datatype_bf16_r:
            return 2;
        case rocsparselt_datatype_f8_r:
        case rocsparselt_datatype_bf8_r:
        case rocsparselt_datatype_i8_r:
            return 1;
        default:
            return 0;
        }
    };

    auto    bpe    = datatype_bpe();
    int64_t offset = num_batches * batch_stride * bpe;
    return offset;
}

template <typename T>
inline rocsparselt_status validateSetAttributeDataSize(size_t dataSize,
                                                       size_t expectedSize = sizeof(T))
{
    if(expectedSize != dataSize)
    {
        int   status = -4;
        char* mname  = __cxxabiv1::__cxa_demangle(typeid(T).name(), NULL, NULL, &status);

        hipsparselt_cerr << "The parameter number 5 (dataSize) had an illegal value: "
                         << "expected " << expectedSize << " bytes(sizeof("
                         << (status == 0 ? mname : typeid(T).name()) << "))"
                         << ", current size " << dataSize << " bytes" << std::endl;

        if(status == 0)
            free(mname);
        return rocsparselt_status_invalid_size;
    }
    return rocsparselt_status_success;
}

template <>
inline rocsparselt_status validateSetAttributeDataSize<void>(size_t dataSize, size_t expectedSize)
{
    if(expectedSize > dataSize)
    {
        hipsparselt_cerr << "The parameter number 5 (dataSize) had an illegal value: "
                         << "at least " << expectedSize << " bytes, current size " << dataSize
                         << " bytes" << std::endl;
        return rocsparselt_status_invalid_size;
    }
    return rocsparselt_status_success;
}

template <typename T>
inline rocsparselt_status validateGetAttributeDataSize(size_t dataSize,
                                                       size_t expectedSize = sizeof(T))
{
    return validateGetAttributeDataSize<void>(dataSize, expectedSize);
}

template <>
inline rocsparselt_status validateGetAttributeDataSize<void>(size_t dataSize, size_t expectedSize)
{
    if(expectedSize > dataSize)
    {
        hipsparselt_cerr << "The parameter number 5 (dataSize) had an illegal value: expected "
                         << expectedSize << " bytes, current size " << dataSize << " bytes"
                         << std::endl;
        return rocsparselt_status_invalid_size;
    }
    return rocsparselt_status_success;
}

/*******************************************************************************
 * Validate Matrix Arguments - matrix init.
 ******************************************************************************/
inline rocsparselt_status validateMatrixArgs(const _rocsparselt_handle* handle,
                                             int64_t                    num_rows,
                                             int64_t                    num_cols,
                                             int64_t                    ld,
                                             uint32_t                   alignment,
                                             rocsparselt_datatype       valueType,
                                             rocsparselt_order          order,
                                             rocsparselt_matrix_type    matrixType)
{
    // handle must be valid
    if(handle == nullptr || !handle->isInit())
        return rocsparselt_status_invalid_handle;

    if(num_rows == 0 || num_cols == 0)
    {
        hipsparselt_cerr << "row and col cannot be zero, current are " << num_rows << " and "
                         << num_cols << std::endl;
        return rocsparselt_status_invalid_size;
    }

    if(num_rows < 8 || num_cols < 8)
    {
        hipsparselt_cerr << "row and col must larger than 8, current are " << num_rows << " and "
                         << num_cols << std::endl;
        return rocsparselt_status_not_implemented;
    }

    // leading dimensions must be valid
    if(num_rows > ld)
    {
        hipsparselt_cerr << "number of rows(" << num_rows << ") is larger than leading dimension("
                         << ld << ")" << std::endl;
        return rocsparselt_status_invalid_size;
    }

    if(order == rocsparselt_order_row)
        return rocsparselt_status_not_implemented;

    //TODO should support other datatype in the future.
    switch(valueType)
    {
    case rocsparselt_datatype_f16_r:
    case rocsparselt_datatype_bf16_r:
    case rocsparselt_datatype_i8_r:
        break;
    default:
        return rocsparselt_status_not_implemented;
    }
    return rocsparselt_status_success;
}

/*******************************************************************************
 * Validate Matmul Descr. init Arguments - matrix init.
 ******************************************************************************/
inline rocsparselt_status validateMatmulDescrArgs(const _rocsparselt_handle* handle,
                                                  rocsparselt_operation      opA,
                                                  rocsparselt_operation      opB,
                                                  int64_t                    num_rows_a,
                                                  int64_t                    num_cols_a,
                                                  int64_t                    lda,
                                                  int64_t                    num_rows_b,
                                                  int64_t                    num_cols_b,
                                                  int64_t                    ldb,
                                                  int64_t                    num_rows_c,
                                                  int64_t                    num_cols_c,
                                                  int64_t                    ldc,
                                                  int64_t                    num_rows_d,
                                                  int64_t                    num_cols_d,
                                                  int64_t                    ldd,
                                                  rocsparselt_datatype       type_a,
                                                  rocsparselt_datatype       type_b,
                                                  rocsparselt_datatype       type_c,
                                                  rocsparselt_datatype       type_d,
                                                  rocsparselt_compute_type   compute_type,
                                                  rocsparselt_matrix_type    matrix_type_a,
                                                  rocsparselt_matrix_type    matrix_type_b,
                                                  rocsparselt_matrix_type    matrix_type_c,
                                                  rocsparselt_matrix_type    matrix_type_d)
{
    // handle must be valid
    if(handle == nullptr || !handle->isInit())
        return rocsparselt_status_invalid_handle;

    auto is_op_valid = [](rocsparselt_operation op) {
        switch(op)
        {
        case rocsparselt_operation_none:
        case rocsparselt_operation_transpose:
            return true;
        default:
            return false;
        }
    };
    if(!is_op_valid(opA) || !is_op_valid(opB))
        return rocsparselt_status_invalid_value;
    // sizes of matrics A,B,C,D must fulfill the matrix multiplication rule.
    // D = A x B + C
    // values of num_* are values after been transposed, redirect to before which been transposed.
    int64_t m, n, k;
    auto    status
        = getOriginalSizes(opA, opB, num_rows_a, num_cols_a, num_rows_b, num_cols_b, m, n, k);
    if(status != rocsparselt_status_success)
        return status;

    if(m != (num_rows_c | num_rows_d) || n != (num_cols_c | num_cols_d))
    {
        hipsparselt_cerr << "matrix size is not valid" << std::endl;
        return rocsparselt_status_invalid_size;
    }

    // size of k must be a multiplication of 8
    if(k % 8 != 0)
    {
        hipsparselt_cerr << "k must be a multiplication of 8" << std::endl;
        return rocsparselt_status_invalid_size;
    }

    // data type of matrics must be the same
    if(type_a != (type_b | type_c | type_d))
        return rocsparselt_status_not_implemented;

    switch(type_a)
    {
    case rocsparselt_datatype_bf16_r:
    case rocsparselt_datatype_f16_r:
    case rocsparselt_datatype_f8_r:
    case rocsparselt_datatype_bf8_r:
        if(compute_type != rocsparselt_compute_f32)
            return rocsparselt_status_not_implemented;
        break;
    case rocsparselt_datatype_i8_r:
        if(compute_type != rocsparselt_compute_i32)
            return rocsparselt_status_not_implemented;
        break;
    default:
        return rocsparselt_status_not_implemented;
    }

    // Only matrix A can be structured matrix.
    if(matrix_type_a != rocsparselt_matrix_type_structured)
    {
        hipsparselt_cerr << " Matrix A must be structrured matrix." << std::endl;
        return rocsparselt_status_not_implemented;
    }

    if(matrix_type_b != rocsparselt_matrix_type_dense)
    {
        hipsparselt_cerr << " Matrix B cannot be structrured matrix." << std::endl;
        return rocsparselt_status_not_implemented;
    }

    if(matrix_type_c != rocsparselt_matrix_type_dense
       || matrix_type_d != rocsparselt_matrix_type_dense)
        return rocsparselt_status_invalid_value;

    return rocsparselt_status_success;
}
#endif