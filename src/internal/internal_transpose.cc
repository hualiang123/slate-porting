// Copyright (c) 2017-2022, University of Tennessee. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// This program is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.

#include "slate/internal/device.hh"
#include "internal/internal_batch.hh"
#include "internal/internal.hh"
#include "slate/internal/util.hh"
#include "internal/Tile_lapack.hh"
#include "slate/types.hh"

#include <vector>

namespace slate {

//------------------------------------------------------------------------------
// On macOS, nvcc using clang++ generates a different C++ name mangling
// (std::__1::complex) than g++ for std::complex. This solution is to use
// cu*Complex in .cu files, and cast from std::complex here.
namespace device {

// CUBLAS/ROCBLAS need complex translation, others do not
#if ! defined( SLATE_HAVE_OMPTARGET)

template <>
void transpose(
    bool is_conj,
    int64_t n,
    std::complex<float>* A, int64_t lda,
    blas::Queue& queue)
{
#if defined( BLAS_HAVE_CUBLAS )
    transpose(is_conj, n, (cuFloatComplex*) A, lda, queue);

#elif defined( BLAS_HAVE_ROCBLAS )
    transpose(is_conj, n, (hipFloatComplex*) A, lda, queue);
#endif
}

template <>
void transpose(
    bool is_conj,
    int64_t n,
    std::complex<double>* A, int64_t lda,
    blas::Queue& queue)
{
#if defined( BLAS_HAVE_CUBLAS )
    transpose(is_conj, n, (cuDoubleComplex*) A, lda, queue);

#elif defined( BLAS_HAVE_ROCBLAS )
    transpose(is_conj, n, (hipDoubleComplex*) A, lda, queue);
#endif
}

//--------------------
template <>
void transpose_batch(
    bool is_conj,
    int64_t n,
    std::complex<float>** Aarray, int64_t lda,
    int64_t batch_count,
    blas::Queue& queue)
{
#if defined( BLAS_HAVE_CUBLAS )
    transpose_batch(is_conj, n, (cuFloatComplex**) Aarray, lda, batch_count, queue);

#elif defined( BLAS_HAVE_ROCBLAS )
    transpose_batch(is_conj, n, (hipFloatComplex**) Aarray, lda, batch_count, queue);
#endif
}

template <>
void transpose_batch(
    bool is_conj,
    int64_t n,
    std::complex<double>** Aarray, int64_t lda,
    int64_t batch_count,
    blas::Queue& queue)
{
#if defined( BLAS_HAVE_CUBLAS )
    transpose_batch(is_conj, n, (cuDoubleComplex**) Aarray, lda, batch_count, queue);

#elif defined( BLAS_HAVE_ROCBLAS )
    transpose_batch(is_conj, n, (hipDoubleComplex**) Aarray, lda, batch_count, queue);
#endif
}

//--------------------
template <>
void transpose(
    bool is_conj,
    int64_t m, int64_t n,
    std::complex<float>* A, int64_t lda,
    std::complex<float>* AT, int64_t ldat,
    blas::Queue& queue)
{
#if defined( BLAS_HAVE_CUBLAS )
    transpose(is_conj, m, n,
              (cuFloatComplex*) A, lda,
              (cuFloatComplex*) AT, ldat, queue);

#elif defined( BLAS_HAVE_ROCBLAS )
    transpose(is_conj, m, n,
              (hipFloatComplex*) A, lda,
              (hipFloatComplex*) AT, ldat, queue);
#endif
}

template <>
void transpose(
    bool is_conj,
    int64_t m, int64_t n,
    std::complex<double>* A, int64_t lda,
    std::complex<double>* AT, int64_t ldat,
    blas::Queue& queue)
{
#if defined( BLAS_HAVE_CUBLAS )
    transpose(is_conj, m, n,
              (cuDoubleComplex*) A, lda,
              (cuDoubleComplex*) AT, ldat, queue);

#elif defined( BLAS_HAVE_ROCBLAS )
    transpose(is_conj, m, n,
              (hipDoubleComplex*) A, lda,
              (hipDoubleComplex*) AT, ldat, queue);
#endif
}

//--------------------
template <>
void transpose_batch(
    bool is_conj,
    int64_t m, int64_t n,
    std::complex<float>** Aarray, int64_t lda,
    std::complex<float>** ATarray, int64_t ldat,
    int64_t batch_count,
    blas::Queue& queue)
{
#if defined( BLAS_HAVE_CUBLAS )
    transpose_batch(is_conj, m, n,
                    (cuFloatComplex**) Aarray, lda,
                    (cuFloatComplex**) ATarray, ldat, batch_count, queue);

#elif defined( BLAS_HAVE_ROCBLAS )
    transpose_batch(is_conj, m, n,
                    (hipFloatComplex**) Aarray, lda,
                    (hipFloatComplex**) ATarray, ldat, batch_count, queue);
#endif
}

template <>
void transpose_batch(
    bool is_conj,
    int64_t m, int64_t n,
    std::complex<double>** Aarray, int64_t lda,
    std::complex<double>** ATarray, int64_t ldat,
    int64_t batch_count,
    blas::Queue& queue)
{
#if defined( BLAS_HAVE_CUBLAS )
    transpose_batch(is_conj, m, n,
                    (cuDoubleComplex**) Aarray, lda,
                    (cuDoubleComplex**) ATarray, ldat, batch_count, queue);

#elif defined( BLAS_HAVE_ROCBLAS )
    transpose_batch(is_conj, m, n,
                    (hipDoubleComplex**) Aarray, lda,
                    (hipDoubleComplex**) ATarray, ldat, batch_count, queue);
#endif
}

#endif // ! defined( SLATE_HAVE_OMPTARGET)

//------------------------------------------------------------------------------
#if ! defined( SLATE_HAVE_DEVICE )
// Specializations to allow compilation without CUDA.
template <>
void transpose(
    bool is_conj,
    int64_t n,
    float* A, int64_t lda,
    blas::Queue& queue)
{
}

template <>
void transpose(
    bool is_conj,
    int64_t n,
    double* A, int64_t lda,
    blas::Queue& queue)
{
}

//--------------------
template <>
void transpose_batch(
    bool is_conj,
    int64_t n,
    float** Aarray, int64_t lda,
    int64_t batch_count,
    blas::Queue& queue)
{
}

template <>
void transpose_batch(
    bool is_conj,
    int64_t n,
    double** Aarray, int64_t lda,
    int64_t batch_count,
    blas::Queue& queue)
{
}

//--------------------
template <>
void transpose(
    bool is_conj,
    int64_t m, int64_t n,
    float* A, int64_t lda,
    float* AT, int64_t ldat,
    blas::Queue& queue)
{
}

template <>
void transpose(
    bool is_conj,
    int64_t m, int64_t n,
    double* A, int64_t lda,
    double* AT, int64_t ldat,
    blas::Queue& queue)
{
}

//--------------------
template <>
void transpose_batch(
    bool is_conj,
    int64_t m, int64_t n,
    float** Aarray, int64_t lda,
    float** ATarray, int64_t ldat,
    int64_t batch_count,
    blas::Queue& queue)
{
}

template <>
void transpose_batch(
    bool is_conj,
    int64_t m, int64_t n,
    double** Aarray, int64_t lda,
    double** ATarray, int64_t ldat,
    int64_t batch_count,
    blas::Queue& queue)
{
}
#endif // not SLATE_HAVE_DEVICE

} // namespace device
} // namespace slate
