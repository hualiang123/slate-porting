// Copyright (c) 2017-2020, University of Tennessee. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// This program is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.

#include "lapack/defines.h"

#if defined(LAPACK_HAVE_SYCL)

#include "onemkl_common.hh"

//==============================================================================
namespace lapack {

//------------------------------------------------------------------------------
// Wrapper around workspace query.
// dA is only for templating scalar_t; it isn't referenced.
template <typename scalar_t>
void geqrf_work_size_bytes(
    int64_t m, int64_t n,
    scalar_t* dA, int64_t ldda,
    size_t* dev_work_size, size_t* host_work_size,
    lapack::Queue& queue )
{
    auto solver = queue.stream();

    int64_t lwork = 0;
    blas_dev_call(
        lwork = oneapi::mkl::lapack::geqrf_scratchpad_size<scalar_t>(
            solver, m, n, ldda ));
    *dev_work_size = lwork * sizeof(scalar_t);
    *host_work_size = 0;
}

//------------------------------------------------------------------------------
// Wrapper around oneMKL.
// This is async. Once finished, the return info is in dev_info on the device.
// In oneMKL, there is no info. todo: set it to 0?
template <typename scalar_t>
void geqrf(
    int64_t m, int64_t n,
    scalar_t* dA, int64_t ldda, scalar_t* dtau,
    void*  dev_work, size_t  dev_work_size,
    void* host_work, size_t host_work_size,
    device_info_int* dev_info, lapack::Queue& queue )
{
    auto solver = queue.stream();

    // for cuda, rocm, call set_device; for oneapi, do nothing.
    blas::internal_set_device( queue.device() );

    int64_t lwork = dev_work_size / sizeof(scalar_t);
    blas_dev_call(
        oneapi::mkl::lapack::geqrf(
            solver, m, n, dA, ldda, dtau, (scalar_t*)dev_work, lwork ));

    // todo: default info returned
    blas::device_memset( dev_info, 0, 1, queue );
}

//------------------------------------------------------------------------------
// Explicit instantiations.
template
void geqrf_work_size_bytes(
    int64_t m, int64_t n,
    float* dA, int64_t ldda,
    size_t* dev_work_size, size_t* host_work_size,
    lapack::Queue& queue );

template
void geqrf_work_size_bytes(
    int64_t m, int64_t n,
    double* dA, int64_t ldda,
    size_t* dev_work_size, size_t* host_work_size,
    lapack::Queue& queue );

template
void geqrf_work_size_bytes(
    int64_t m, int64_t n,
    std::complex<float>* dA, int64_t ldda,
    size_t* dev_work_size, size_t* host_work_size,
    lapack::Queue& queue );

template
void geqrf_work_size_bytes(
    int64_t m, int64_t n,
    std::complex<double>* dA, int64_t ldda,
    size_t* dev_work_size, size_t* host_work_size,
    lapack::Queue& queue );

//--------------------
template
void geqrf(
    int64_t m, int64_t n,
    float* dA, int64_t ldda, float* dtau,
    void*  dev_work, size_t  dev_work_size,
    void* host_work, size_t host_work_size,
    device_info_int* dev_info, lapack::Queue& queue );

template
void geqrf(
    int64_t m, int64_t n,
    double* dA, int64_t ldda, double* dtau,
    void*  dev_work, size_t  dev_work_size,
    void* host_work, size_t host_work_size,
    device_info_int* dev_info, lapack::Queue& queue );

template
void geqrf(
    int64_t m, int64_t n,
    std::complex<float>* dA, int64_t ldda, std::complex<float>* dtau,
    void*  dev_work, size_t  dev_work_size,
    void* host_work, size_t host_work_size,
    device_info_int* dev_info, lapack::Queue& queue );

template
void geqrf(
    int64_t m, int64_t n,
    std::complex<double>* dA, int64_t ldda, std::complex<double>* dtau,
    void*  dev_work, size_t  dev_work_size,
    void* host_work, size_t host_work_size,
    device_info_int* dev_info, lapack::Queue& queue );

} // namespace lapack

#endif // LAPACK_HAVE_SYCL
