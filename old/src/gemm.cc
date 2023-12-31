// Copyright (c) 2017-2020, University of Tennessee. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// This program is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.

#include "slate/slate.hh"
#include "internal/internal.hh"
#include "internal/internal_batch.hh"
#include "auxiliary/Debug.hh"

#include <list>
#include <tuple>

namespace slate {

// specialization namespace differentiates, e.g.,
// internal::gemm from internal::specialization::gemm
namespace internal {
namespace specialization {

//------------------------------------------------------------------------------
/// @internal
/// Distributed parallel general matrix-matrix multiplication.
/// Generic implementation for any target.
/// Dependencies enforce the following behavior:
/// - bcast communications are serialized,
/// - gemm operations are serialized,
/// - bcasts can get ahead of gemms by the value of lookahead.
/// ColMajor layout is assumed
///
/// @ingroup gemm_specialization
///
template <Target target, typename scalar_t>
void gemm(slate::internal::TargetType<target>,
          scalar_t alpha, Matrix<scalar_t>& A,
                          Matrix<scalar_t>& B,
          scalar_t beta,  Matrix<scalar_t>& C,
          int64_t lookahead)
{
    using BcastListTag = typename Matrix<scalar_t>::BcastListTag;

    // Assumes column major
    const Layout layout = Layout::ColMajor;

    // OpenMP needs pointer types, but vectors are exception safe
    std::vector<uint8_t> bcast_vector(A.nt());
    std::vector<uint8_t> gemm_vector(A.nt());
    std::vector<uint8_t> c_vector(1);
    uint8_t* bcast = bcast_vector.data();
    uint8_t* gemm  =  gemm_vector.data();
    uint8_t* c     =     c_vector.data();

    if (target == Target::Devices) {
        C.allocateBatchArrays();
        C.reserveDeviceWorkspace();
    }

    // set min number for omp nested active parallel regions
    slate::OmpSetMaxActiveLevels set_active_levels( MinOmpActiveLevels );

    #pragma omp parallel
    #pragma omp master
    {
        if (target == Target::Devices) {
            // fetch C matrix tiles into devices in parallel with first MPI broadcast
            #pragma omp task depend(out:c[0])
            {
                trace::Block trace_block("fetch_C");
                C.tileGetAllForWritingOnDevices(LayoutConvert(layout));
            }
        }

        // send first block col of A and block row of B
        #pragma omp task depend(out:bcast[0])
        {
            // broadcast A(i, 0) to ranks owning block row C(i, :)
            BcastListTag bcast_list_A;
            for (int64_t i = 0; i < A.mt(); ++i)
                bcast_list_A.push_back({i, 0, {C.sub(i, i, 0, C.nt()-1)}, i});
            A.template listBcastMT<target>(bcast_list_A, layout);

            // broadcast B(0, j) to ranks owning block col C(:, j)
            BcastListTag bcast_list_B;
            for (int64_t j = 0; j < B.nt(); ++j)
                bcast_list_B.push_back({0, j, {C.sub(0, C.mt()-1, j, j)}, j});
            B.template listBcastMT<target>(bcast_list_B, layout);
        }

        // send next lookahead block cols of A and block rows of B
        for (int64_t k = 1; k < lookahead+1 && k < A.nt(); ++k) {
            #pragma omp task depend(in:bcast[k-1]) \
                             depend(out:bcast[k])
            {
                // broadcast A(i, k) to ranks owning block row C(i, :)
                BcastListTag bcast_list_A;
                for (int64_t i = 0; i < A.mt(); ++i)
                    bcast_list_A.push_back({i, k, {C.sub(i, i, 0, C.nt()-1)}, i});
                A.template listBcastMT<target>(bcast_list_A, layout);

                // broadcast B(k, j) to ranks owning block col C(:, j)
                BcastListTag bcast_list_B;
                for (int64_t j = 0; j < B.nt(); ++j)
                    bcast_list_B.push_back({k, j, {C.sub(0, C.mt()-1, j, j)}, j});
                B.template listBcastMT<target>(bcast_list_B, layout);
            }
        }

        // multiply alpha A(:, 0) B(0, :) + beta C
        #pragma omp task depend(in:bcast[0]) \
                         depend(in:c[0]) \
                         depend(out:gemm[0])
        {
            internal::gemm<target>(
                    alpha, A.sub(0, A.mt()-1, 0, 0),
                           B.sub(0, 0, 0, B.nt()-1),
                    beta,  std::move(C),
                    layout);
        }

        for (int64_t k = 1; k < A.nt(); ++k) {

            // send next block col of A and block row of B
            if (k+lookahead < A.nt()) {
                #pragma omp task depend(in:gemm[k-1]) \
                                 depend(in:bcast[k+lookahead-1]) \
                                 depend(out:bcast[k+lookahead])
                {
                    // broadcast A(i, k+la) to ranks owning block row C(i, :)
                    BcastListTag bcast_list_A;
                    for (int64_t i = 0; i < A.mt(); ++i) {
                        bcast_list_A.push_back(
                            {i, k+lookahead, {C.sub(i, i, 0, C.nt()-1)}, i});
                    }
                    A.template listBcastMT<target>(bcast_list_A, layout);

                    // broadcast B(k+la, j) to ranks owning block col C(:, j)
                    BcastListTag bcast_list_B;
                    for (int64_t j = 0; j < B.nt(); ++j) {
                        bcast_list_B.push_back(
                            {k+lookahead, j, {C.sub(0, C.mt()-1, j, j)}, j});
                    }
                    B.template listBcastMT<target>(bcast_list_B, layout);
                }
            }

            // multiply alpha A(:, k) B(k, :) + C, no beta
            #pragma omp task depend(in:bcast[k]) \
                             depend(in:gemm[k-1]) \
                             depend(out:gemm[k])
            {
                internal::gemm<target>(
                    alpha,         A.sub(0, A.mt()-1, k, k),
                                   B.sub(k, k, 0, B.nt()-1),
                    scalar_t(1.0), std::move(C),
                    layout);
            }
        }
        #pragma omp taskwait
        C.tileUpdateAllOrigin();
    }

    C.releaseWorkspace();
}

//------------------------------------------------------------------------------
/// @internal
/// Distributed parallel general matrix-matrix multiplication.
/// Device target implementation.
/// Dependencies enforce the following behavior:
/// - bcast communications are serialized,
/// - gemm operations are serialized,
/// - bcasts can get ahead of gemms by the value of lookahead.
/// ColMajor layout is assumed
///
/// @ingroup gemm_specialization
///
template <typename scalar_t>
void gemm(slate::internal::TargetType<Target::Devices>,
          scalar_t alpha, Matrix<scalar_t>& A,
                          Matrix<scalar_t>& B,
          scalar_t beta,  Matrix<scalar_t>& C,
          int64_t lookahead)
{
    using BcastListTag = typename Matrix<scalar_t>::BcastListTag;

    // Assumes column major
    const Layout layout = Layout::ColMajor;

    // OpenMP needs pointer types, but vectors are exception safe
    std::vector<uint8_t> bcast_vector(A.nt());
    std::vector<uint8_t>  gemm_vector(A.nt());
    uint8_t* bcast = bcast_vector.data();
    uint8_t* gemm  =  gemm_vector.data();

    std::vector< GemmBatchArrays<scalar_t> > gemmBatch(
                    lookahead+1,
                    GemmBatchArrays<scalar_t>(A.num_devices()));

    // set min number for omp nested active parallel regions
    slate::OmpSetMaxActiveLevels set_active_levels( MinOmpActiveLevels );

    #pragma omp parallel
    #pragma omp master
    {
        C.reserveDeviceWorkspace();

        // send first block col of A and block row of B
        #pragma omp task depend(out:bcast[0])
        {
            // broadcast A(i, 0) to ranks owning block row C(i, :)
            {
                BcastListTag bcast_list_A;
                for (int64_t i = 0; i < A.mt(); ++i)
                    bcast_list_A.push_back({i, 0, {C.sub(i, i, 0, C.nt()-1)}, i});
                A.template listBcastMT<Target::Devices>(bcast_list_A, layout);
            }

            // broadcast B(0, j) to ranks owning block col C(:, j)
            {
                BcastListTag bcast_list_B;
                for (int64_t j = 0; j < B.nt(); ++j)
                    bcast_list_B.push_back({0, j, {C.sub(0, C.mt()-1, j, j)}, j});
                B.template listBcastMT<Target::Devices>(bcast_list_B, layout);
            }

            // prepare first internal::gemm pointer's arrays and prefetch its data
            internal::gemmPrep<Target::Devices>(
                    alpha, A.sub(0, A.mt()-1, 0, 0),
                           B.sub(0, 0, 0, B.nt()-1),
                    beta,  std::move(C),
                    std::move(&gemmBatch[0]),
                    layout);
        }

        // send next lookahead block cols of A and block rows of B
        for (int64_t k = 1; k < lookahead+1 && k < A.nt(); ++k) {
            #pragma omp task depend(in:bcast[k-1]) \
                             depend(out:bcast[k])
            {
                // broadcast A(i, k) to ranks owning block row C(i, :)
                {

                    BcastListTag bcast_list_A;
                    for (int64_t i = 0; i < A.mt(); ++i)
                        bcast_list_A.push_back({i, k, {C.sub(i, i, 0, C.nt()-1)}, i});
                    A.template listBcastMT<Target::Devices>(bcast_list_A, layout);
                }

                // broadcast B(k, j) to ranks owning block col C(:, j)
                {
                    BcastListTag bcast_list_B;
                    for (int64_t j = 0; j < B.nt(); ++j)
                        bcast_list_B.push_back({k, j, {C.sub(0, C.mt()-1, j, j)}, j});
                    B.template listBcastMT<Target::Devices>(bcast_list_B, layout);
                }

                // prepare lookahead internal::gemm pointer's arrays
                // data is already prefetched
                internal::gemmPrep<Target::Devices>(
                        alpha,         A.sub(0, A.mt()-1, k, k),
                                       B.sub(k, k, 0, B.nt()-1),
                        scalar_t(1.0), std::move(C),
                        std::move(&gemmBatch[k % (lookahead+1)]),
                        layout, true);
            }
        }

        // multiply alpha A(:, 0) B(0, :) + beta C
        #pragma omp task depend(in:bcast[0]) \
                         depend(out:gemm[0])
        {
            internal::gemmExec<Target::Devices>(
                    alpha, A.sub(0, A.mt()-1, 0, 0),
                           B.sub(0, 0, 0, B.nt()-1),
                    beta,  std::move(C),
                    std::move(&gemmBatch[0]),
                    layout);
        }

        for (int64_t k = 1; k < A.nt(); ++k) {

            // send next block col of A and block row of B
            if (k+lookahead < A.nt()) {
                #pragma omp task depend(in:gemm[k-1]) \
                                 depend(in:bcast[k+lookahead-1]) \
                                 depend(out:bcast[k+lookahead])
                {
                    // broadcast A(i, k+la) to ranks owning block row C(i, :)
                    {
                        BcastListTag bcast_list_A;
                        for (int64_t i = 0; i < A.mt(); ++i) {
                            bcast_list_A.push_back(
                                {i, k+lookahead, {C.sub(i, i, 0, C.nt()-1)}, i});
                        }
                        A.template listBcastMT<Target::Devices>(bcast_list_A, layout);
                    }

                    // broadcast B(k+la, j) to ranks owning block col C(:, j)
                    {
                        BcastListTag bcast_list_B;
                        for (int64_t j = 0; j < B.nt(); ++j) {
                            bcast_list_B.push_back(
                                {k+lookahead, j, {C.sub(0, C.mt()-1, j, j)}, j});
                        }
                        B.template listBcastMT<Target::Devices>(bcast_list_B, layout);
                    }

                    // prepare lookahead internal::gemm pointer's arrays
                    // data is already prefetched
                    internal::gemmPrep<Target::Devices>(
                        alpha,         A.sub(0, A.mt()-1, k+lookahead, k+lookahead),
                                       B.sub(k+lookahead, k+lookahead, 0, B.nt()-1),
                        scalar_t(1.0), std::move(C),
                        std::move(&gemmBatch[(k+lookahead) % (lookahead+1)]),
                        layout, true);
                }
            }

            // multiply alpha A(:, k) B(k, :) + C, no beta
            #pragma omp task depend(in:bcast[k]) \
                             depend(in:gemm[k-1]) \
                             depend(out:gemm[k])
            {
                internal::gemmExec<Target::Devices>(
                    alpha,         A.sub(0, A.mt()-1, k, k),
                                   B.sub(k, k, 0, B.nt()-1),
                    scalar_t(1.0), std::move(C),
                    std::move(&gemmBatch[k % (lookahead+1)]),
                    layout);
            }
        }

        #pragma omp taskwait
        C.tileUpdateAllOrigin();
    }

    C.releaseWorkspace();
}

} // namespace specialization
} // namespace internal

//------------------------------------------------------------------------------
/// Version with target as template parameter.
/// @ingroup gemm_specialization
///
template <Target target, typename scalar_t>
void gemm(scalar_t alpha, Matrix<scalar_t>& A,
                          Matrix<scalar_t>& B,
          scalar_t beta,  Matrix<scalar_t>& C,
          Options const& opts)
{
    int64_t lookahead = get_option<int64_t>( opts, Option::Lookahead, 1 );

    internal::specialization::gemm(internal::TargetType<target>(),
                                   alpha, A,
                                          B,
                                   beta,  C,
                                   lookahead);
}

//------------------------------------------------------------------------------
/// Distributed parallel general matrix-matrix multiplication.
/// Performs the matrix-matrix operation
/// \[
///     C = \alpha A B + \beta C,
/// \]
/// where alpha and beta are scalars, and $A$, $B$, and $C$ are matrices, with
/// $A$ an m-by-k matrix, $B$ a k-by-n matrix, and $C$ an m-by-n matrix.
/// The matrices can be transposed or conjugate-transposed beforehand, e.g.,
///
///     auto AT = slate::transpose( A );
///     auto BT = slate::conj_transpose( B );
///     slate::gemm( alpha, AT, BT, beta, C );
///
//------------------------------------------------------------------------------
/// @tparam scalar_t
///         One of float, double, std::complex<float>, std::complex<double>.
//------------------------------------------------------------------------------
/// @param[in] alpha
///         The scalar alpha.
///
/// @param[in] A
///         The m-by-k matrix A.
///
/// @param[in] B
///         The k-by-n matrix B.
///
/// @param[in] beta
///         The scalar beta.
///
/// @param[in,out] C
///         On entry, the m-by-n matrix C.
///         On exit, overwritten by the result $\alpha A B + \beta C$.
///
/// @param[in] opts
///         Additional options, as map of name = value pairs. Possible options:
///         - Option::Lookahead:
///           Number of blocks to overlap communication and computation.
///           lookahead >= 0. Default 1.
///         - Option::Target:
///           Implementation to target. Possible values:
///           - HostTask:  OpenMP tasks on CPU host [default].
///           - HostNest:  nested OpenMP parallel for loop on CPU host.
///           - HostBatch: batched BLAS on CPU host.
///           - Devices:   batched BLAS on GPU device.
///
/// @ingroup gemm
///
template <typename scalar_t>
void gemm(scalar_t alpha, Matrix<scalar_t>& A,
                          Matrix<scalar_t>& B,
          scalar_t beta,  Matrix<scalar_t>& C,
          Options const& opts)
{
    Target target = get_option( opts, Option::Target, Target::HostTask );

    switch (target) {
        case Target::Host:
        case Target::HostTask:
            gemm<Target::HostTask>(alpha, A, B, beta, C, opts);
            break;
        case Target::HostNest:
            gemm<Target::HostNest>(alpha, A, B, beta, C, opts);
            break;
        case Target::HostBatch:
            gemm<Target::HostBatch>(alpha, A, B, beta, C, opts);
            break;
        case Target::Devices:
            gemm<Target::Devices>(alpha, A, B, beta, C, opts);
            break;
    }
}

//------------------------------------------------------------------------------
// Explicit instantiations.
template
void gemm<float>(
    float alpha, Matrix<float>& A,
                 Matrix<float>& B,
    float beta,  Matrix<float>& C,
    Options const& opts);

template
void gemm<double>(
    double alpha, Matrix<double>& A,
                  Matrix<double>& B,
    double beta,  Matrix<double>& C,
    Options const& opts);

template
void gemm< std::complex<float> >(
    std::complex<float> alpha, Matrix< std::complex<float> >& A,
                               Matrix< std::complex<float> >& B,
    std::complex<float> beta,  Matrix< std::complex<float> >& C,
    Options const& opts);

template
void gemm< std::complex<double> >(
    std::complex<double> alpha, Matrix< std::complex<double> >& A,
                                Matrix< std::complex<double> >& B,
    std::complex<double> beta,  Matrix< std::complex<double> >& C,
    Options const& opts);

} // namespace slate
