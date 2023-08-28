// Copyright (c) 2017-2022, University of Tennessee. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause
// This program is free software: you can redistribute it and/or modify it under
// the terms of the BSD 3-Clause license. See the accompanying LICENSE file.

#include "slate/Matrix.hh"
#include "slate/TriangularMatrix.hh"
#include "slate/types.hh"
#include "slate/Tile_blas.hh"
#include "internal/internal.hh"
#include "internal/internal_batch.hh"

namespace slate {
namespace internal {

//------------------------------------------------------------------------------
/// Triangular solve matrix (multiple right-hand sides).
/// We assume A is a single tile and that the calling rank owns it
/// as well as it has [a copy of] all the tiles in B
/// Dispatches to target implementations.
/// @ingroup trsm_internal
///
template <Target target, typename scalar_t>
void trsmA(Side side,
           scalar_t alpha, TriangularMatrix<scalar_t>&& A,
                                     Matrix<scalar_t>&& B,
           int priority, Layout layout, int64_t queue_index,
           Options const& opts)
{
    assert( A.mt() == 1 );
    assert( side == Side::Left ? A.mt() == B.mt() : A.mt() == B.nt() );

    trsmA( internal::TargetType<target>(),
          side,
          alpha, A,
                 B,
          priority, layout, queue_index, opts );
}

//------------------------------------------------------------------------------
/// Triangular solve matrix (multiple right-hand sides).
/// Host OpenMP task implementation.
/// @ingroup trsm_internal
///
template <typename scalar_t>
void trsmA(internal::TargetType<Target::HostTask>,
           Side side,
           scalar_t alpha, TriangularMatrix<scalar_t>& A,
                                     Matrix<scalar_t>& B,
           int priority, Layout layout, int64_t queue_index,
           Options const& opts)
{

    // CPU assumes column major
    // todo: relax this assumption, by allowing Tile_blas.hh::trsm()
    //       to take layout param
    // todo: optimize for the number of layout conversions,
    //       by watching 'layout' and 'B(i, j).layout()'
    assert( layout == Layout::ColMajor );

    A.tileGetForReading( 0, 0, HostNum, LayoutConvert( layout ) );

    // alternatively, if (side == right), (conj)-transpose both A and B,
    // then assume side == left; see slate::trsm
    if (side == Side::Right) {
        #pragma omp taskgroup
        for (int64_t i = 0; i < B.mt(); ++i) {
            #pragma omp task slate_omp_default_none \
                shared( A, B ) \
                firstprivate(i, layout, side, alpha) priority(priority)
            {
                B.tileGetForWriting( i, 0, HostNum, LayoutConvert( layout ) );

                tile::trsm(
                    side, A.diag(),
                    alpha, A( 0, 0 ), B( i, 0 ) );
            }
        }
    }
    else {
        #pragma omp taskgroup
        for (int64_t j = 0; j < B.nt(); ++j) {
            #pragma omp task slate_omp_default_none \
                shared( A, B ) \
                firstprivate(j, layout, side, alpha) priority(priority)
            {
                B.tileGetForWriting( 0, j, HostNum, LayoutConvert( layout ) );

                tile::trsm(
                    side, A.diag(),
                    alpha, A( 0, 0 ), B( 0, j ) );
            }
        }
    }
}

//------------------------------------------------------------------------------
/// Triangular solve matrix (multiple right-hand sides).
/// Host nested OpenMP implementation.
/// @ingroup trsm_internal
///
template <typename scalar_t>
void trsmA(internal::TargetType<Target::HostNest>,
           Side side,
           scalar_t alpha, TriangularMatrix<scalar_t>& A,
                                     Matrix<scalar_t>& B,
           int priority, Layout layout, int64_t queue_index,
           Options const& opts)
{
    trsmA( internal::TargetType<Target::HostTask>(),
            side,
            alpha, A, B,
            priority, layout, queue_index, opts );
}

//------------------------------------------------------------------------------
/// Triangular solve matrix (multiple right-hand sides).
/// Host batched implementation.
/// @ingroup trsm_internal
///
template <typename scalar_t>
void trsmA(internal::TargetType<Target::HostBatch>,
           Side side,
           scalar_t alpha, TriangularMatrix<scalar_t>& A,
                                     Matrix<scalar_t>& B,
           int priority, Layout layout, int64_t queue_index,
           Options const& opts)
{
    trsmA( internal::TargetType<Target::HostTask>(),
            side,
            alpha, A, B,
            priority, layout, queue_index, opts );
}

//------------------------------------------------------------------------------
/// Triangular solve matrix (multiple right-hand sides).
/// GPU device batched cuBLAS implementation.
/// @ingroup trsm_internal
///
template <typename scalar_t>
void trsmA(internal::TargetType<Target::Devices>,
           Side side,
           scalar_t alpha, TriangularMatrix<scalar_t>& A,
                                     Matrix<scalar_t>& B,
           int priority, Layout layout, int64_t queue_index,
           Options const& opts)
{
    using std::swap;
    using blas::conj;
    using ij_tuple = typename BaseMatrix<scalar_t>::ij_tuple;

    // GPU assumes column major
    // todo:  relax this assumption, by allowing Tile_blas.hh::trsm() to take
    //        layout param
    // todo:  optimize for the number of layout conversions,
    //        by watching 'layout' and 'B(i, j).layout()'
    assert(layout == Layout::ColMajor);

    assert(B.num_devices() > 0);
    assert(B.uploPhysical() == Uplo::General);

    TileReleaseStrategy tile_release_strategy = get_option(
            opts, Option::TileReleaseStrategy, TileReleaseStrategy::All );

    Uplo uploA = A.uploPhysical();
    Diag diagA = A.diag();
    Op opA = A.op();
    Side sideA = side;

    if (B.op() != Op::NoTrans) {
        if (A.is_complex && opA != Op::NoTrans && opA != B.op())
            throw std::exception();

        // switch op(A) <=> op(B), side left <=> right, m <=> n
        sideA = (side == Side::Left ? Side::Right : Side::Left);
        if (opA == Op::NoTrans)
            opA = B.op();
        else if (opA == B.op() || A.is_real) {
            // A and B are both Trans or both ConjTrans;
            // Trans == ConjTrans if real
            opA = Op::NoTrans;
        }
        else
            throw std::exception();

        if (B.op() == Op::ConjTrans)
            alpha = conj(alpha);
    }

    // We know that the tile A( 0, 0 ) may be duplicated across multiple devices
    // so that when B has several block columns the trsm can be parallelized.
    // However, trsmA is designed for a tall and short B.
    // So is it relevant/needed?
    // We could just get the device where A( 0, 0 ) is and do the computation.
    #pragma omp taskgroup
    for (int device = 0; device < B.num_devices(); ++device) {
        #pragma omp task shared(A, B) priority(priority) \
            firstprivate(device, side, layout, sideA, uploA, opA, diagA) \
            firstprivate(alpha, queue_index)
        {
            std::set<ij_tuple> B_tiles_set;
            if (side == Side::Right) {
                for (int64_t i = 0; i < B.mt(); ++i) {
                    if (B.tileIsLocal( i, 0 )
                        || B.tileExists( i, 0, device ))
                    {
                        B_tiles_set.insert( { i, 0 } );
                    }
                }
            }
            else {
                for (int64_t j = 0; j < B.nt(); ++j) {
                    if (B.tileIsLocal( 0, j )
                        || B.tileExists( 0, j, device ))
                    {
                        B_tiles_set.insert( { 0, j } );
                    }
                }
            }

            int64_t batch_size = B_tiles_set.size();

            if (batch_size > 0) {
                A.tileGetForReading( 0, 0, device, LayoutConvert( layout ) );
                B.tileGetForWriting(
                    B_tiles_set, device, LayoutConvert( layout ) );

                // interior col or row
                std::vector<scalar_t*> a_array0;
                std::vector<scalar_t*> b_array0;
                a_array0.reserve( batch_size );
                b_array0.reserve( batch_size );

                // bottom-right tile
                // todo: replace batch trsm with plain trsm
                std::vector<scalar_t*> a_array1;
                std::vector<scalar_t*> b_array1;

                int64_t lda0 = 0;
                int64_t ldb0 = 0;
                int64_t lda1 = 0;
                int64_t ldb1 = 0;

                int64_t mb0 = B.tileMb(0);
                int64_t nb0 = B.tileNb(0);
                int64_t mb1 = B.tileMb(B.mt()-1);
                int64_t nb1 = B.tileNb(B.nt()-1);

                auto A00d = A( 0, 0, device );
                auto dAdata = A00d.data();
                lda1 = lda0 = A00d.stride();

                if (side == Side::Right) {
                    // TODO loop over B_tiles_set instead of looking for again.
                    for (int64_t i = 0; i < B.mt()-1; ++i) {
                        if (B.tileExists( i, 0, device ))
                        {
                            auto Bi0d = B( i, 0, device );
                            a_array0.push_back( dAdata );
                            b_array0.push_back( Bi0d.data() );
                            ldb0 = Bi0d.stride();
                        }
                    }
                    {
                        int64_t i = B.mt()-1;
                        if (B.tileExists( i, 0, device ))
                        {
                            auto Bi0d = B( i, 0, device );
                            a_array1.push_back( dAdata );
                            b_array1.push_back( Bi0d.data() );
                            ldb1 = Bi0d.stride();
                        }
                    }
                }
                else {
                    for (int64_t j = 0; j < B.nt()-1; ++j) {
                        if (B.tileExists( 0, j, device ))
                        {
                            auto B0jd = B( 0, j, device );
                            a_array0.push_back( dAdata );
                            b_array0.push_back( B0jd.data() );
                            ldb0 = B0jd.stride();
                        }
                    }
                    {
                        int64_t j = B.nt()-1;
                        if (B.tileExists( 0, j, device ))
                        {
                            auto B0jd = B( 0, j, device );
                            a_array1.push_back( dAdata );
                            b_array1.push_back( B0jd.data() );
                            ldb1 = B0jd.stride();
                        }
                    }
                }

                if (B.op() != Op::NoTrans) {
                    swap( mb0, nb0 );
                    swap( mb1, nb1 );
                }

                {
                    trace::Block trace_block("blas::batch::trsmA");

                    std::vector<Side>      side_( 1, sideA );
                    std::vector<Uplo>      uplo_( 1, uploA );
                    std::vector<Op>         opA_( 1, opA   );
                    std::vector<Diag>      diag_( 1, diagA );
                    std::vector<scalar_t> alpha_( 1, alpha );
                    std::vector<int64_t> info;

                    blas::Queue* queue = A.compute_queue( device, queue_index );
                    assert( queue != nullptr );

                    if (a_array0.size() > 0) {
                        std::vector<int64_t>    m( 1,  mb0 );
                        std::vector<int64_t>    n( 1,  nb0 );
                        std::vector<int64_t>  lda( 1, lda0 );
                        std::vector<int64_t>  ldb( 1, ldb0 );

                        blas::batch::trsm(
                            layout, side_, uplo_, opA_, diag_,
                            m, n,
                            alpha_, a_array0, lda,
                                    b_array0, ldb,
                            a_array0.size(), info, *queue);
                    }

                    if (a_array1.size() > 0) {
                        std::vector<int64_t>   m(1,  mb1);
                        std::vector<int64_t>   n(1,  nb1);
                        std::vector<int64_t> lda(1, lda1);
                        std::vector<int64_t> ldb(1, ldb1);

                        blas::batch::trsm(
                            layout, side_, uplo_, opA_, diag_,
                            m, n,
                            alpha_, a_array1, lda,
                                    b_array1, ldb,
                            a_array1.size(), info, *queue);
                    }

                    queue->sync();
                }

                if (tile_release_strategy == TileReleaseStrategy::Internal
                    || tile_release_strategy == TileReleaseStrategy::All)
                {
                    A.tileRelease( 0, 0, device );
                    for (auto i = 0; i < batch_size; ++i) {
                        A.tileTick( 0, 0 );
                    }
                }
            }
        }
    }
}

//------------------------------------------------------------------------------
// Explicit instantiations.
// ----------------------------------------
template
void trsmA<Target::HostTask, float>(
    Side side,
    float alpha, TriangularMatrix<float>&& A,
                           Matrix<float>&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA<Target::HostNest, float>(
    Side side,
    float alpha, TriangularMatrix<float>&& A,
                           Matrix<float>&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA<Target::HostBatch, float>(
    Side side,
    float alpha, TriangularMatrix<float>&& A,
                           Matrix<float>&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA<Target::Devices, float>(
    Side side,
    float alpha, TriangularMatrix<float>&& A,
                           Matrix<float>&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

// ----------------------------------------
template
void trsmA<Target::HostTask, double>(
    Side side,
    double alpha, TriangularMatrix<double>&& A,
                            Matrix<double>&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA<Target::HostNest, double>(
    Side side,
    double alpha, TriangularMatrix<double>&& A,
                            Matrix<double>&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA<Target::HostBatch, double>(
    Side side,
    double alpha, TriangularMatrix<double>&& A,
                            Matrix<double>&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA<Target::Devices, double>(
    Side side,
    double alpha, TriangularMatrix<double>&& A,
                            Matrix<double>&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

// ----------------------------------------
template
void trsmA< Target::HostTask, std::complex<float> >(
    Side side,
    std::complex<float> alpha, TriangularMatrix< std::complex<float> >&& A,
                                         Matrix< std::complex<float> >&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA< Target::HostNest, std::complex<float> >(
    Side side,
    std::complex<float> alpha, TriangularMatrix< std::complex<float> >&& A,
                                         Matrix< std::complex<float> >&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA< Target::HostBatch, std::complex<float> >(
    Side side,
    std::complex<float> alpha, TriangularMatrix< std::complex<float> >&& A,
                                         Matrix< std::complex<float> >&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA< Target::Devices, std::complex<float> >(
    Side side,
    std::complex<float> alpha, TriangularMatrix< std::complex<float> >&& A,
                                         Matrix< std::complex<float> >&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

// ----------------------------------------
template
void trsmA< Target::HostTask, std::complex<double> >(
    Side side,
    std::complex<double> alpha, TriangularMatrix< std::complex<double> >&& A,
                                          Matrix< std::complex<double> >&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA< Target::HostNest, std::complex<double> >(
    Side side,
    std::complex<double> alpha, TriangularMatrix< std::complex<double> >&& A,
                                          Matrix< std::complex<double> >&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA< Target::HostBatch, std::complex<double> >(
    Side side,
    std::complex<double> alpha, TriangularMatrix< std::complex<double> >&& A,
                                          Matrix< std::complex<double> >&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

template
void trsmA< Target::Devices, std::complex<double> >(
    Side side,
    std::complex<double> alpha, TriangularMatrix< std::complex<double> >&& A,
                                          Matrix< std::complex<double> >&& B,
    int priority, Layout layout, int64_t queue_index,
    Options const& opts);

} // namespace internal
} // namespace slate