/*
 * Copyright 2021 The DAPHNE Consortium
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

#ifndef SRC_RUNTIME_LOCAL_KERNELS_EWBINARYMAT_H
#define SRC_RUNTIME_LOCAL_KERNELS_EWBINARYMAT_H

#include <runtime/local/datastructures/CSRMatrix.h>
#include <runtime/local/datastructures/DataObjectFactory.h>
#include <runtime/local/datastructures/DenseMatrix.h>
#include <runtime/local/datastructures/Matrix.h>
#include <runtime/local/kernels/BinaryOpCode.h>
#include <runtime/local/kernels/EwBinarySca.h>

#include <cassert>
#include <cstddef>

// ****************************************************************************
// Struct for partial template specialization
// ****************************************************************************

template<class DTRes, class DTLhs, class DTRhs>
struct EwBinaryMat {
    static void apply(BinaryOpCode opCode, DTRes *& res, const DTLhs * lhs, const DTRhs * rhs) = delete;
};

// ****************************************************************************
// Convenience function
// ****************************************************************************

template<class DTRes, class DTLhs, class DTRhs>
void ewBinaryMat(BinaryOpCode opCode, DTRes *& res, const DTLhs * lhs, const DTRhs * rhs) {
    EwBinaryMat<DTRes, DTLhs, DTRhs>::apply(opCode, res, lhs, rhs);
}

// ****************************************************************************
// (Partial) template specializations for different data/value types
// ****************************************************************************

// ----------------------------------------------------------------------------
// DenseMatrix <- DenseMatrix, DenseMatrix
// ----------------------------------------------------------------------------

template<typename VT>
struct EwBinaryMat<DenseMatrix<VT>, DenseMatrix<VT>, DenseMatrix<VT>> {
    static void apply(BinaryOpCode opCode, DenseMatrix<VT> *& res, const DenseMatrix<VT> * lhs, const DenseMatrix<VT> * rhs) {
        const size_t numRows = lhs->getNumRows();
        const size_t numCols = lhs->getNumCols();
        assert((numRows == rhs->getNumRows()) && "lhs and rhs must have the same dimensions");
        assert((numCols == rhs->getNumCols()) && "lhs and rhs must have the same dimensions");
        
        if(res == nullptr)
            res = DataObjectFactory::create<DenseMatrix<VT>>(numRows, numCols, false);
        
        const VT * valuesLhs = lhs->getValues();
        const VT * valuesRhs = rhs->getValues();
        VT * valuesRes = res->getValues();
        
        EwBinaryScaFuncPtr<VT, VT, VT> func = getEwBinaryScaFuncPtr<VT, VT, VT>(opCode);
        
        for(size_t r = 0; r < numRows; r++) {
            for(size_t c = 0; c < numCols; c++)
                valuesRes[c] = func(valuesLhs[c], valuesRhs[c]);
            valuesLhs += lhs->getRowSkip();
            valuesRhs += rhs->getRowSkip();
            valuesRes += res->getRowSkip();
        }
    }
};

// ----------------------------------------------------------------------------
// CSRMatrix <- CSRMatrix, CSRMatrix
// ----------------------------------------------------------------------------

template<typename VT>
struct EwBinaryMat<CSRMatrix<VT>, CSRMatrix<VT>, CSRMatrix<VT>> {
    static void apply(BinaryOpCode opCode, CSRMatrix<VT> *& res, const CSRMatrix<VT> * lhs, const CSRMatrix<VT> * rhs) {
        const size_t numRows = lhs->getNumRows();
        const size_t numCols = rhs->getNumCols();
        assert((numRows == rhs->getNumRows()) && "lhs and rhs must have the same dimensions");
        assert((numCols == rhs->getNumCols()) && "lhs and rhs must have the same dimensions");
        
        size_t maxNnz = 0;
        switch(opCode) {
            case BinaryOpCode::ADD: // merge
                maxNnz = lhs->getNumNonZeros() + rhs->getNumNonZeros();
                break;
            case BinaryOpCode::MUL: // intersect
                maxNnz = std::min(lhs->getNumNonZeros(), rhs->getNumNonZeros());
                break;
            default:
                throw std::runtime_error("unknown BinaryOpCode");
        }
        
        if(res == nullptr)
            res = DataObjectFactory::create<CSRMatrix<VT>>(numRows, numCols, maxNnz, false);
        
        size_t * rowOffsetsRes = res->getRowOffsets();
        
        EwBinaryScaFuncPtr<VT, VT, VT> func = getEwBinaryScaFuncPtr<VT, VT, VT>(opCode);
        
        rowOffsetsRes[0] = 0;
        
        switch(opCode) {
            case BinaryOpCode::ADD: { // merge non-zero cells
                for(size_t rowIdx = 0; rowIdx < numRows; rowIdx++) {
                    size_t nnzRowLhs = lhs->getNumNonZeros(rowIdx);
                    size_t nnzRowRhs = rhs->getNumNonZeros(rowIdx);
                    if(nnzRowLhs && nnzRowRhs) {
                        // merge within row
                        const VT * valuesRowLhs = lhs->getValues(rowIdx);
                        const VT * valuesRowRhs = rhs->getValues(rowIdx);
                        VT * valuesRowRes = res->getValues(rowIdx);
                        const size_t * colIdxsRowLhs = lhs->getColIdxs(rowIdx);
                        const size_t * colIdxsRowRhs = rhs->getColIdxs(rowIdx);
                        size_t * colIdxsRowRes = res->getColIdxs(rowIdx);
                        size_t posLhs = 0;
                        size_t posRhs = 0;
                        size_t posRes = 0;
                        while(posLhs < nnzRowLhs && posRhs < nnzRowRhs) {
                            if(colIdxsRowLhs[posLhs] == colIdxsRowRhs[posRhs]) {
                                valuesRowRes[posRes] = func(valuesRowLhs[posLhs], valuesRowRhs[posRhs]);
                                colIdxsRowRes[posRes] = colIdxsRowLhs[posLhs];
                                posLhs++;
                                posRhs++;
                            }
                            else if(colIdxsRowLhs[posLhs] < colIdxsRowRhs[posRhs]) {
                                valuesRowRes[posRes] = valuesRowLhs[posLhs];
                                colIdxsRowRes[posRes] = colIdxsRowLhs[posLhs];
                                posLhs++;
                            }
                            else {
                                valuesRowRes[posRes] = valuesRowRhs[posRhs];
                                colIdxsRowRes[posRes] = colIdxsRowRhs[posRhs];
                                posRhs++;
                            }
                            posRes++;
                        }
                        // copy from left
                        const size_t restRowLhs = nnzRowLhs - posLhs;
                        memcpy(valuesRowRes + posRes, valuesRowLhs + posLhs, restRowLhs * sizeof(VT));
                        memcpy(colIdxsRowRes + posRes, colIdxsRowLhs + posLhs, restRowLhs * sizeof(size_t));
                        // copy from right
                        const size_t restRowRhs = nnzRowRhs - posRhs;
                        memcpy(valuesRowRes + posRes, valuesRowRhs + posRhs, restRowRhs * sizeof(VT));
                        memcpy(colIdxsRowRes + posRes, colIdxsRowRhs + posRhs, restRowRhs * sizeof(size_t));
                        
                        rowOffsetsRes[rowIdx + 1] = rowOffsetsRes[rowIdx] + posRes + restRowLhs + restRowRhs;
                    }
                    else if(nnzRowLhs) {
                        // copy from left
                        memcpy(res->getValues(rowIdx), lhs->getValues(rowIdx), nnzRowLhs * sizeof(VT));
                        memcpy(res->getColIdxs(rowIdx), lhs->getColIdxs(rowIdx), nnzRowLhs * sizeof(size_t));
                        rowOffsetsRes[rowIdx + 1] = rowOffsetsRes[rowIdx] + nnzRowLhs;
                    }
                    else if(nnzRowRhs) {
                        // copy from right
                        memcpy(res->getValues(rowIdx), rhs->getValues(rowIdx), nnzRowRhs * sizeof(VT));
                        memcpy(res->getColIdxs(rowIdx), rhs->getColIdxs(rowIdx), nnzRowRhs * sizeof(size_t));
                        rowOffsetsRes[rowIdx + 1] = rowOffsetsRes[rowIdx] + nnzRowRhs;
                    }
                    else
                        // empty row in result
                        rowOffsetsRes[rowIdx + 1] = rowOffsetsRes[rowIdx];
                }
                break;
            }
            case BinaryOpCode::MUL: { // intersect non-zero cells
                for(size_t rowIdx = 0; rowIdx < numRows; rowIdx++) {
                    size_t nnzRowLhs = lhs->getNumNonZeros(rowIdx);
                    size_t nnzRowRhs = rhs->getNumNonZeros(rowIdx);
                    if(nnzRowLhs && nnzRowRhs) {
                        // intersect within row
                        const VT * valuesRowLhs = lhs->getValues(rowIdx);
                        const VT * valuesRowRhs = rhs->getValues(rowIdx);
                        VT * valuesRowRes = res->getValues(rowIdx);
                        const size_t * colIdxsRowLhs = lhs->getColIdxs(rowIdx);
                        const size_t * colIdxsRowRhs = rhs->getColIdxs(rowIdx);
                        size_t * colIdxsRowRes = res->getColIdxs(rowIdx);
                        size_t posLhs = 0;
                        size_t posRhs = 0;
                        size_t posRes = 0;
                        while(posLhs < nnzRowLhs && posRhs < nnzRowRhs) {
                            if(colIdxsRowLhs[posLhs] == colIdxsRowRhs[posRhs]) {
                                valuesRowRes[posRes] = func(valuesRowLhs[posLhs], valuesRowRhs[posRhs]);
                                colIdxsRowRes[posRes] = colIdxsRowLhs[posLhs];
                                posLhs++;
                                posRhs++;
                                posRes++;
                            }
                            else if(colIdxsRowLhs[posLhs] < colIdxsRowRhs[posRhs])
                                posLhs++;
                            else
                                posRhs++;
                        }
                        rowOffsetsRes[rowIdx + 1] = rowOffsetsRes[rowIdx] + posRes;
                    }
                    else
                        // empty row in result
                        rowOffsetsRes[rowIdx + 1] = rowOffsetsRes[rowIdx];
                }
                break;
            }
            default:
                throw std::runtime_error("unknown BinaryOpCode");
        }
        
        // TODO Update number of non-zeros in result in the end.
    }
};

// ----------------------------------------------------------------------------
// Matrix <- Matrix, Matrix
// ----------------------------------------------------------------------------

template<typename VT>
struct EwBinaryMat<Matrix<VT>, Matrix<VT>, Matrix<VT>> {
    static void apply(BinaryOpCode opCode, Matrix<VT> *& res, const Matrix<VT> * lhs, const Matrix<VT> * rhs) {
        const size_t numRows = lhs->getNumRows();
        const size_t numCols = lhs->getNumCols();
        assert((numRows == rhs->getNumRows()) && "lhs and rhs must have the same dimensions");
        assert((numCols == rhs->getNumCols()) && "lhs and rhs must have the same dimensions");
        
        // TODO Choose matrix implementation depending on expected number of non-zeros.
        if(res == nullptr)
            res = DataObjectFactory::create<DenseMatrix<VT>>(numRows, numCols, false);
        
        EwBinaryScaFuncPtr<VT, VT, VT> func = getEwBinaryScaFuncPtr<VT, VT, VT>(opCode);
        
        res->prepareAppend();
        for(size_t r = 0; r < numRows; r++)
            for(size_t c = 0; c < numCols; c++)
                res->append(r, c) = func(lhs->get(r, c), rhs->get(r, c));
        res->finishAppend();
    }
};

#endif //SRC_RUNTIME_LOCAL_KERNELS_EWBINARYMAT_H