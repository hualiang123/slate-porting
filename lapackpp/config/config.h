// Copyright (c) 2017-2022, University of Tennessee. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// This program is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// -----------------------------------------------------------------------------
#if defined(FORTRAN_UPPER) || defined(BLAS_FORTRAN_UPPER) || defined(LAPACK_FORTRAN_UPPER)
    #define FORTRAN_NAME( lower, UPPER ) UPPER
#elif defined(FORTRAN_LOWER) || defined(BLAS_FORTRAN_LOWER) || defined(LAPACK_FORTRAN_LOWER)
    #define FORTRAN_NAME( lower, UPPER ) lower
#elif defined(FORTRAN_ADD_) || defined(BLAS_FORTRAN_ADD_) || defined(LAPACK_FORTRAN_ADD_)
    #define FORTRAN_NAME( lower, UPPER ) lower ## _
#else
    #error "must define one of FORTRAN_ADD_, FORTRAN_LOWER, FORTRAN_UPPER"
#endif

// -----------------------------------------------------------------------------
#if defined(BLAS_ILP64) || defined(LAPACK_ILP64)
    typedef int64_t blas_int;
    typedef int64_t lapack_int;
#else
    typedef int blas_int;
    typedef int lapack_int;
#endif

#endif // CONFIG_H
