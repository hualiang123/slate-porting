// Copyright (c) 2017-2022, University of Tennessee. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// This program is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.

#include "blas/device_blas.hh"

#include "device_internal.hh"

#include <limits>

namespace blas {

//==============================================================================
namespace impl {

//------------------------------------------------------------------------------
/// Mid-level templated wrapper checks and converts arguments,
/// then calls low-level wrapper.
/// @ingroup scal_internal
///
template <typename scalar_t>
void scal(
    int64_t n,
    scalar_t alpha,
    scalar_t* x, int64_t incx,
    blas::Queue& queue )
{
#ifndef BLAS_HAVE_DEVICE
    throw blas::Error( "device BLAS not available", __func__ );
#else
    // check arguments
    blas_error_if( n < 0 );      // standard BLAS returns, doesn't fail
    blas_error_if( incx <= 0 );  // standard BLAS returns, doesn't fail

    // convert arguments
    device_blas_int n_    = to_device_blas_int( n );
    device_blas_int incx_ = to_device_blas_int( incx );

    blas::internal_set_device( queue.device() );

    // call low-level wrapper
    internal::scal( n_, alpha, x, incx_, queue );
#endif
}

}  // namespace impl

//==============================================================================
// High-level overloaded wrappers call mid-level templated wrapper.

//------------------------------------------------------------------------------
/// GPU device, float version.
/// @ingroup scal
void scal(
    int64_t n,
    float alpha,
    float* x, int64_t incx,
    blas::Queue& queue )
{
    impl::scal( n, alpha, x, incx, queue );
}

//------------------------------------------------------------------------------
/// GPU device, double version.
/// @ingroup scal
void scal(
    int64_t n,
    double alpha,
    double* x, int64_t incx,
    blas::Queue& queue )
{
    impl::scal( n, alpha, x, incx, queue );
}

//------------------------------------------------------------------------------
/// GPU device, complex<float> version.
/// @ingroup scal
void scal(
    int64_t n,
    std::complex<float> alpha,
    std::complex<float>* x, int64_t incx,
    blas::Queue& queue )
{
    impl::scal( n, alpha, x, incx, queue );
}

//------------------------------------------------------------------------------
/// GPU device, complex<double> version.
/// @ingroup scal
void scal(
    int64_t n,
    std::complex<double> alpha,
    std::complex<double>* x, int64_t incx,
    blas::Queue& queue )
{
    impl::scal( n, alpha, x, incx, queue );
}

}  // namespace blas
