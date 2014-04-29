/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2013, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file     TComPattern.cpp
    \brief    neighboring pixel access classes
*/

#include "TComPic.h"
#include "TComPattern.h"
#include "TComDataCU.h"

using namespace x265;

//! \ingroup TLibCommon
//! \{

// ====================================================================================================================
// Public member functions (TComPattern)
// ====================================================================================================================

void TComPattern::initAdiPattern(TComDataCU* cu, uint32_t zOrderIdxInPart, uint32_t partDepth, pixel* adiBuf,
                                 int strideOrig, int heightOrig)
{
    pixel* roiOrigin;
    pixel* adiTemp;
    uint32_t cuWidth = cu->getCUSize(0) >> partDepth;
    uint32_t cuHeight = cu->getCUSize(0) >> partDepth;
    uint32_t cuWidth2 = cuWidth << 1;
    uint32_t cuHeight2 = cuHeight << 1;

    uint32_t width;
    uint32_t height;
    int  picStride = cu->getPic()->getStride();
    bool bNeighborFlags[4 * MAX_NUM_SPU_W + 1];
    int  numIntraNeighbor = 0;

    uint32_t partIdxLT, partIdxRT, partIdxLB;

    cu->deriveLeftRightTopIdxAdi(partIdxLT, partIdxRT, zOrderIdxInPart, partDepth);

    int  partIdxStride   = cu->getPic()->getNumPartInCUSize();
    int  baseUnitSize    = g_maxCUSize >> g_maxCUDepth;
    int  unitWidth       = baseUnitSize;
    int  unitHeight      = baseUnitSize;
    int  cuHeightInUnits = cuHeight / unitHeight;
    int  cuWidthInUnits  = cuWidth / unitWidth;
    int  iAboveUnits     = cuWidthInUnits << 1;
    int  leftUnits       = cuHeightInUnits << 1;
    partIdxLB            = g_rasterToZscan[g_zscanToRaster[partIdxLT] + ((cuHeightInUnits - 1) * partIdxStride)];

    if (!cu->getSlice()->getPPS()->getConstrainedIntraPred())
    {
        bNeighborFlags[leftUnits] = isAboveLeftAvailable(cu, partIdxLT);
        numIntraNeighbor += (int)(bNeighborFlags[leftUnits]);
        numIntraNeighbor += isAboveAvailable(cu, partIdxLT, partIdxRT, (bNeighborFlags + leftUnits + 1));
        numIntraNeighbor += isAboveRightAvailable(cu, partIdxLT, partIdxRT, (bNeighborFlags + leftUnits + 1 + cuWidthInUnits));
        numIntraNeighbor += isLeftAvailable(cu, partIdxLT, partIdxLB, (bNeighborFlags + leftUnits - 1));
        numIntraNeighbor += isBelowLeftAvailable(cu, partIdxLT, partIdxLB, (bNeighborFlags + leftUnits   - 1 - cuHeightInUnits));
    }
    else
    {
        bNeighborFlags[leftUnits] = isAboveLeftAvailableCIP(cu, partIdxLT);
        numIntraNeighbor += (int)(bNeighborFlags[leftUnits]);
        numIntraNeighbor += isAboveAvailableCIP(cu, partIdxLT, partIdxRT, (bNeighborFlags + leftUnits + 1));
        numIntraNeighbor += isAboveRightAvailableCIP(cu, partIdxLT, partIdxRT, (bNeighborFlags + leftUnits + 1 + cuWidthInUnits));
        numIntraNeighbor += isLeftAvailableCIP(cu, partIdxLT, partIdxLB, (bNeighborFlags + leftUnits - 1));
        numIntraNeighbor += isBelowLeftAvailableCIP(cu, partIdxLT, partIdxLB, (bNeighborFlags + leftUnits   - 1 - cuHeightInUnits));
    }

    width = cuWidth2 + 1;
    height = cuHeight2 + 1;

    if (((width << 2) > strideOrig) || ((height << 2) > heightOrig))
    {
        return;
    }

    roiOrigin = cu->getPic()->getPicYuvRec()->getLumaAddr(cu->getAddr(), cu->getZorderIdxInCU() + zOrderIdxInPart);
    adiTemp   = adiBuf;

    fillReferenceSamples(roiOrigin, adiTemp, bNeighborFlags, numIntraNeighbor, unitWidth, unitHeight, iAboveUnits, leftUnits,
                         cuWidth, cuHeight, width, height, picStride);

    // generate filtered intra prediction samples
    // left and left above border + above and above right border + top left corner = length of 3. filter buffer
    int bufSize = cuHeight2 + cuWidth2 + 1;
    uint32_t wh = ADI_BUF_STRIDE * height;         // number of elements in one buffer

    pixel* filteredBuf1 = adiBuf + wh;         // 1. filter buffer
    pixel* filteredBuf2 = filteredBuf1 + wh; // 2. filter buffer
    pixel* filterBuf = filteredBuf2 + wh;    // buffer for 2. filtering (sequential)
    pixel* filterBufN = filterBuf + bufSize; // buffer for 1. filtering (sequential)

    int l = 0;
    // left border from bottom to top
    for (int i = 0; i < cuHeight2; i++)
    {
        filterBuf[l++] = adiTemp[ADI_BUF_STRIDE * (cuHeight2 - i)];
    }

    // top left corner
    filterBuf[l++] = adiTemp[0];

    // above border from left to right
    memcpy(&filterBuf[l], &adiTemp[1], cuWidth2 * sizeof(*filterBuf));

    if (cu->getSlice()->getSPS()->getUseStrongIntraSmoothing())
    {
        int blkSize = 32;
        int bottomLeft = filterBuf[0];
        int topLeft = filterBuf[cuHeight2];
        int topRight = filterBuf[bufSize - 1];
        int threshold = 1 << (X265_DEPTH - 5);
        bool bilinearLeft = abs(bottomLeft + topLeft - 2 * filterBuf[cuHeight]) < threshold;
        bool bilinearAbove  = abs(topLeft + topRight - 2 * filterBuf[cuHeight2 + cuHeight]) < threshold;

        if (cuWidth >= blkSize && (bilinearLeft && bilinearAbove))
        {
            int shift = g_convertToBit[cuWidth] + 3; // log2(uiCuHeight2)
            filterBufN[0] = filterBuf[0];
            filterBufN[cuHeight2] = filterBuf[cuHeight2];
            filterBufN[bufSize - 1] = filterBuf[bufSize - 1];
            //TODO: Performance Primitive???
            for (int i = 1; i < cuHeight2; i++)
            {
                filterBufN[i] = ((cuHeight2 - i) * bottomLeft + i * topLeft + cuHeight) >> shift;
            }

            for (int i = 1; i < cuWidth2; i++)
            {
                filterBufN[cuHeight2 + i] = ((cuWidth2 - i) * topLeft + i * topRight + cuWidth) >> shift;
            }
        }
        else
        {
            // 1. filtering with [1 2 1]
            filterBufN[0] = filterBuf[0];
            filterBufN[bufSize - 1] = filterBuf[bufSize - 1];
            for (int i = 1; i < bufSize - 1; i++)
            {
                filterBufN[i] = (filterBuf[i - 1] + 2 * filterBuf[i] + filterBuf[i + 1] + 2) >> 2;
            }
        }
    }
    else
    {
        // 1. filtering with [1 2 1]
        filterBufN[0] = filterBuf[0];
        filterBufN[bufSize - 1] = filterBuf[bufSize - 1];
        for (int i = 1; i < bufSize - 1; i++)
        {
            filterBufN[i] = (filterBuf[i - 1] + 2 * filterBuf[i] + filterBuf[i + 1] + 2) >> 2;
        }
    }

    // fill 1. filter buffer with filtered values
    l = 0;
    for (int i = 0; i < cuHeight2; i++)
    {
        filteredBuf1[ADI_BUF_STRIDE * (cuHeight2 - i)] = filterBufN[l++];
    }

    filteredBuf1[0] = filterBufN[l++];
    memcpy(&filteredBuf1[1], &filterBufN[l], cuWidth2 * sizeof(*filteredBuf1));
}

// Overloaded initialization of ADI buffers to support buffered references for xpredIntraAngBufRef
void TComPattern::initAdiPattern(TComDataCU* cu, uint32_t zOrderIdxInPart, uint32_t partDepth, pixel* adiBuf, int strideOrig, int heightOrig,
                                 pixel* refAbove, pixel* refLeft, pixel* refAboveFlt, pixel* refLeftFlt)
{
    initAdiPattern(cu, zOrderIdxInPart, partDepth, adiBuf, strideOrig, heightOrig);
    uint32_t cuWidth   = cu->getCUSize(0) >> partDepth;
    uint32_t cuHeight  = cu->getCUSize(0) >> partDepth;
    uint32_t cuWidth2  = cuWidth << 1;
    uint32_t cuHeight2 = cuHeight << 1;

    refAbove += cuWidth - 1;
    refAboveFlt += cuWidth - 1;
    refLeft += cuWidth - 1;
    refLeftFlt += cuWidth - 1;

    //  ADI_BUF_STRIDE * (2 * height + 1);
    memcpy(refAbove, adiBuf, (cuWidth2 + 1) * sizeof(pixel));
    memcpy(refAboveFlt, adiBuf + ADI_BUF_STRIDE * (2 * cuHeight + 1), (cuWidth2 + 1) * sizeof(pixel));

    for (int k = 0; k < cuHeight2 + 1; k++)
    {
        refLeft[k] = adiBuf[k * ADI_BUF_STRIDE];
        refLeftFlt[k] = (adiBuf + ADI_BUF_STRIDE * (cuHeight2 + 1))[k * ADI_BUF_STRIDE];   // Smoothened
    }
}

void TComPattern::initAdiPatternChroma(TComDataCU* cu, uint32_t zOrderIdxInPart, uint32_t partDepth, pixel* adiBuf, int strideOrig, int heightOrig, int chromaId)
{
    pixel*  roiOrigin;
    pixel*  adiTemp;
    uint32_t  cuWidth  = cu->getCUSize(0) >> (partDepth + cu->getHorzChromaShift());
    uint32_t  cuHeight = cu->getCUSize(0) >> (partDepth + cu->getVertChromaShift());

    cuHeight = (cuWidth != cuHeight) ? cuHeight >> 1 : cuHeight;

    uint32_t  width;
    uint32_t  height;
    int   picStride = cu->getPic()->getCStride();

    bool  bNeighborFlags[4 * MAX_NUM_SPU_W + 1];
    int   numIntraNeighbor = 0;

    uint32_t partIdxLT, partIdxRT, partIdxLB;

    cu->deriveLeftRightTopIdxAdi(partIdxLT, partIdxRT, zOrderIdxInPart, partDepth);

    int  partIdxStride   = cu->getPic()->getNumPartInCUSize();
    int  baseUnitSize    = g_maxCUSize >> g_maxCUDepth;
    int  unitWidth       = baseUnitSize  >> cu->getHorzChromaShift();
    int  unitHeight      = baseUnitSize  >> cu->getVertChromaShift();
    int  cuHeightInUnits = cuHeight / unitHeight;
    int  cuWidthInUnits  = cuWidth  / unitWidth;
    int  aboveUnits      = cuWidthInUnits << 1;
    int  leftUnits       = cuHeightInUnits << 1;
    partIdxLB            = g_rasterToZscan[g_zscanToRaster[partIdxLT] + ((cuHeightInUnits - 1) * partIdxStride)];

    if (!cu->getSlice()->getPPS()->getConstrainedIntraPred())
    {
        bNeighborFlags[leftUnits] = isAboveLeftAvailable(cu, partIdxLT);
        numIntraNeighbor += (int)(bNeighborFlags[leftUnits]);
        numIntraNeighbor += isAboveAvailable(cu, partIdxLT, partIdxRT, (bNeighborFlags + leftUnits + 1));
        numIntraNeighbor += isAboveRightAvailable(cu, partIdxLT, partIdxRT, (bNeighborFlags + leftUnits + 1 + cuWidthInUnits));
        numIntraNeighbor += isLeftAvailable(cu, partIdxLT, partIdxLB, (bNeighborFlags + leftUnits - 1));
        numIntraNeighbor += isBelowLeftAvailable(cu, partIdxLT, partIdxLB, (bNeighborFlags + leftUnits   - 1 - cuHeightInUnits));
    }
    else
    {
        bNeighborFlags[leftUnits] = isAboveLeftAvailableCIP(cu, partIdxLT);
        numIntraNeighbor += (int)(bNeighborFlags[leftUnits]);
        numIntraNeighbor += isAboveAvailableCIP(cu, partIdxLT, partIdxRT, (bNeighborFlags + leftUnits + 1));
        numIntraNeighbor += isAboveRightAvailableCIP(cu, partIdxLT, partIdxRT, (bNeighborFlags + leftUnits + 1 + cuWidthInUnits));
        numIntraNeighbor += isLeftAvailableCIP(cu, partIdxLT, partIdxLB, (bNeighborFlags + leftUnits - 1));
        numIntraNeighbor += isBelowLeftAvailableCIP(cu, partIdxLT, partIdxLB, (bNeighborFlags + leftUnits   - 1 - cuHeightInUnits));
    }

    width = cuWidth * 2 + 1;
    height = cuHeight * 2 + 1;

    if ((4 * width > strideOrig) || (4 * height > heightOrig))
    {
        return;
    }
    roiOrigin = (chromaId == 1) ? cu->getPic()->getPicYuvRec()->getCbAddr(cu->getAddr(), cu->getZorderIdxInCU() + zOrderIdxInPart) : cu->getPic()->getPicYuvRec()->getCrAddr(cu->getAddr(), cu->getZorderIdxInCU() + zOrderIdxInPart);
    adiTemp   = (chromaId == 1) ? adiBuf : (adiBuf + 2 * ADI_BUF_STRIDE * height);

    fillReferenceSamples(roiOrigin, adiTemp, bNeighborFlags, numIntraNeighbor, unitWidth, unitHeight, aboveUnits, leftUnits,
                         cuWidth, cuHeight, width, height, picStride);
}

void TComPattern::fillReferenceSamples(pixel* roiOrigin, pixel* adiTemp, bool* bNeighborFlags, int numIntraNeighbor, int unitWidth, int unitHeight, int aboveUnits, int leftUnits, uint32_t cuWidth, uint32_t cuHeight, uint32_t width, uint32_t height, int picStride)
{
    pixel* roiTemp;
    int  i, j;
    int  dcValue = 1 << (X265_DEPTH - 1);
    int  totalUnits = aboveUnits + leftUnits + 1;

    if (numIntraNeighbor == 0)
    {
        // Fill border with DC value
        for (i = 0; i < width; i++)
        {
            adiTemp[i] = dcValue;
        }

        for (i = 1; i < height; i++)
        {
            adiTemp[i * ADI_BUF_STRIDE] = dcValue;
        }
    }
    else if (numIntraNeighbor == totalUnits)
    {
        // Fill top-left border with rec. samples
        roiTemp = roiOrigin - picStride - 1;
        adiTemp[0] = roiTemp[0];

        // Fill left border with rec. samples
        // Fill below left border with rec. samples
        roiTemp = roiOrigin - 1;

        for (i = 0; i < 2 * cuHeight; i++)
        {
            adiTemp[(1 + i) * ADI_BUF_STRIDE] = roiTemp[0];
            roiTemp += picStride;
        }

        // Fill top border with rec. samples
        // Fill top right border with rec. samples
        roiTemp = roiOrigin - picStride;
        memcpy(&adiTemp[1], roiTemp, 2 * cuWidth * sizeof(*adiTemp));
    }
    else // reference samples are partially available
    {
        int  totalSamples = (leftUnits * unitHeight) + ((aboveUnits + 1) * unitWidth);
        pixel pAdiLine[5 * MAX_CU_SIZE];
        pixel *pAdiLineTemp;
        bool  *pNeighborFlags;
        int   next, curr;

        // Initialize
        for (i = 0; i < totalSamples; i++)
        {
            pAdiLine[i] = dcValue;
        }

        // Fill top-left sample
        roiTemp = roiOrigin - picStride - 1;
        pAdiLineTemp = pAdiLine + (leftUnits * unitHeight);
        pNeighborFlags = bNeighborFlags + leftUnits;
        if (*pNeighborFlags)
        {
            pixel topLeftVal = roiTemp[0];
            for (i = 0; i < unitWidth; i++)
            {
                pAdiLineTemp[i] = topLeftVal;
            }
        }

        // Fill left & below-left samples
        roiTemp += picStride;
        pAdiLineTemp--;
        pNeighborFlags--;
        for (j = 0; j < leftUnits; j++)
        {
            if (*pNeighborFlags)
            {
                for (i = 0; i < unitHeight; i++)
                {
                    pAdiLineTemp[-i] = roiTemp[i * picStride];
                }
            }
            roiTemp += unitHeight * picStride;
            pAdiLineTemp -= unitHeight;
            pNeighborFlags--;
        }

        // Fill above & above-right samples
        roiTemp = roiOrigin - picStride;
        pAdiLineTemp = pAdiLine + (leftUnits * unitHeight) + unitWidth;
        pNeighborFlags = bNeighborFlags + leftUnits + 1;
        for (j = 0; j < aboveUnits; j++)
        {
            if (*pNeighborFlags)
            {
                memcpy(pAdiLineTemp, roiTemp, unitWidth * sizeof(*adiTemp));
            }
            roiTemp += unitWidth;
            pAdiLineTemp += unitWidth;
            pNeighborFlags++;
        }

        // Pad reference samples when necessary
        curr = 0;
        next = 1;
        pAdiLineTemp = pAdiLine;
        int pAdiLineTopRowOffset = leftUnits * (unitHeight - unitWidth);
        if (!bNeighborFlags[0])
        {
            // very bottom unit of bottom-left; at least one unit will be valid.
            while (next < totalUnits && !bNeighborFlags[next])
            {
                next++;
            }

            pixel *pAdiLineNext = pAdiLine + ((next < leftUnits) ? (next * unitHeight) : (pAdiLineTopRowOffset + (next * unitWidth)));
            const pixel refSample = *pAdiLineNext;
            // Pad unavailable samples with new value
            int nextOrTop = std::min<int>(next, leftUnits);
            // fill left column
            while (curr < nextOrTop)
            {
                for (i = 0; i < unitHeight; i++)
                {
                    pAdiLineTemp[i] = refSample;
                }

                pAdiLineTemp += unitHeight;
                curr++;
            }

            // fill top row
            while (curr < next)
            {
                for (i = 0; i < unitWidth; i++)
                {
                    pAdiLineTemp[i] = refSample;
                }

                pAdiLineTemp += unitWidth;
                curr++;
            }
        }

        // pad all other reference samples.
        while (curr < totalUnits)
        {
            if (!bNeighborFlags[curr]) // samples not available
            {
                int numSamplesInCurrUnit = (curr >= leftUnits) ? unitWidth : unitHeight;
                const pixel refSample = *(pAdiLineTemp - 1);
                for (i = 0; i < numSamplesInCurrUnit; i++)
                {
                    pAdiLineTemp[i] = refSample;
                }

                pAdiLineTemp += numSamplesInCurrUnit;
                curr++;
            }
            else
            {
                pAdiLineTemp += (curr >= leftUnits) ? unitWidth : unitHeight;
                curr++;
            }
        }

        // Copy processed samples
        pAdiLineTemp = pAdiLine + height + unitWidth - 2;
        memcpy(adiTemp, pAdiLineTemp, width * sizeof(*adiTemp));

        pAdiLineTemp = pAdiLine + height - 1;
        for (i = 1; i < height; i++)
        {
            adiTemp[i * ADI_BUF_STRIDE] = pAdiLineTemp[-i];
        }
    }
}

bool TComPattern::isAboveLeftAvailable(TComDataCU* cu, uint32_t partIdxLT)
{
    uint32_t partAboveLeft;
    TComDataCU* pcCUAboveLeft = cu->getPUAboveLeft(partAboveLeft, partIdxLT);

    return pcCUAboveLeft ? true : false;
}

int TComPattern::isAboveAvailable(TComDataCU* cu, uint32_t partIdxLT, uint32_t partIdxRT, bool *bValidFlags)
{
    const uint32_t rasterPartBegin = g_zscanToRaster[partIdxLT];
    const uint32_t rasterPartEnd = g_zscanToRaster[partIdxRT] + 1;
    const uint32_t idxStep = 1;
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t rasterPart = rasterPartBegin; rasterPart < rasterPartEnd; rasterPart += idxStep)
    {
        uint32_t uiPartAbove;
        TComDataCU* pcCUAbove = cu->getPUAbove(uiPartAbove, g_rasterToZscan[rasterPart]);
        if (pcCUAbove)
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr++;
    }

    return numIntra;
}

int TComPattern::isLeftAvailable(TComDataCU* cu, uint32_t partIdxLT, uint32_t partIdxLB, bool *bValidFlags)
{
    const uint32_t rasterPartBegin = g_zscanToRaster[partIdxLT];
    const uint32_t rasterPartEnd = g_zscanToRaster[partIdxLB] + 1;
    const uint32_t idxStep = cu->getPic()->getNumPartInCUSize();
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t rasterPart = rasterPartBegin; rasterPart < rasterPartEnd; rasterPart += idxStep)
    {
        uint32_t partLeft;
        TComDataCU* pcCULeft = cu->getPULeft(partLeft, g_rasterToZscan[rasterPart]);
        if (pcCULeft)
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr--; // opposite direction
    }

    return numIntra;
}

int TComPattern::isAboveRightAvailable(TComDataCU* cu, uint32_t partIdxLT, uint32_t partIdxRT, bool *bValidFlags)
{
    const uint32_t numUnitsInPU = g_zscanToRaster[partIdxRT] - g_zscanToRaster[partIdxLT] + 1;
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t offset = 1; offset <= numUnitsInPU; offset++)
    {
        uint32_t uiPartAboveRight;
        TComDataCU* pcCUAboveRight = cu->getPUAboveRightAdi(uiPartAboveRight, partIdxRT, offset);
        if (pcCUAboveRight)
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr++;
    }

    return numIntra;
}

int TComPattern::isBelowLeftAvailable(TComDataCU* cu, uint32_t partIdxLT, uint32_t partIdxLB, bool *bValidFlags)
{
    const uint32_t numUnitsInPU = (g_zscanToRaster[partIdxLB] - g_zscanToRaster[partIdxLT]) / cu->getPic()->getNumPartInCUSize() + 1;
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t offset = 1; offset <= numUnitsInPU; offset++)
    {
        uint32_t uiPartBelowLeft;
        TComDataCU* pcCUBelowLeft = cu->getPUBelowLeftAdi(uiPartBelowLeft, partIdxLB, offset);
        if (pcCUBelowLeft)
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr--; // opposite direction
    }

    return numIntra;
}

bool TComPattern::isAboveLeftAvailableCIP(TComDataCU* cu, uint32_t partIdxLT)
{
    uint32_t partAboveLeft;
    TComDataCU* pcCUAboveLeft = cu->getPUAboveLeft(partAboveLeft, partIdxLT);

    return pcCUAboveLeft && pcCUAboveLeft->isIntra(partAboveLeft);
}

int TComPattern::isAboveAvailableCIP(TComDataCU* cu, uint32_t partIdxLT, uint32_t partIdxRT, bool *bValidFlags)
{
    const uint32_t rasterPartBegin = g_zscanToRaster[partIdxLT];
    const uint32_t rasterPartEnd = g_zscanToRaster[partIdxRT] + 1;
    const uint32_t idxStep = 1;
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t rasterPart = rasterPartBegin; rasterPart < rasterPartEnd; rasterPart += idxStep)
    {
        uint32_t uiPartAbove;
        TComDataCU* pcCUAbove = cu->getPUAbove(uiPartAbove, g_rasterToZscan[rasterPart]);
        if (pcCUAbove && pcCUAbove->isIntra(uiPartAbove))
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr++;
    }

    return numIntra;
}

int TComPattern::isLeftAvailableCIP(TComDataCU* cu, uint32_t partIdxLT, uint32_t partIdxLB, bool *bValidFlags)
{
    const uint32_t rasterPartBegin = g_zscanToRaster[partIdxLT];
    const uint32_t rasterPartEnd = g_zscanToRaster[partIdxLB] + 1;
    const uint32_t idxStep = cu->getPic()->getNumPartInCUSize();
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t rasterPart = rasterPartBegin; rasterPart < rasterPartEnd; rasterPart += idxStep)
    {
        uint32_t partLeft;
        TComDataCU* pcCULeft = cu->getPULeft(partLeft, g_rasterToZscan[rasterPart]);
        if (pcCULeft && pcCULeft->isIntra(partLeft))
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr--; // opposite direction
    }

    return numIntra;
}

int TComPattern::isAboveRightAvailableCIP(TComDataCU* cu, uint32_t partIdxLT, uint32_t partIdxRT, bool *bValidFlags)
{
    const uint32_t numUnitsInPU = g_zscanToRaster[partIdxRT] - g_zscanToRaster[partIdxLT] + 1;
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t offset = 1; offset <= numUnitsInPU; offset++)
    {
        uint32_t uiPartAboveRight;
        TComDataCU* pcCUAboveRight = cu->getPUAboveRightAdi(uiPartAboveRight, partIdxRT, offset);
        if (pcCUAboveRight && pcCUAboveRight->isIntra(uiPartAboveRight))
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr++;
    }

    return numIntra;
}

int TComPattern::isBelowLeftAvailableCIP(TComDataCU* cu, uint32_t partIdxLT, uint32_t partIdxLB, bool *bValidFlags)
{
    const uint32_t numUnitsInPU = (g_zscanToRaster[partIdxLB] - g_zscanToRaster[partIdxLT]) / cu->getPic()->getNumPartInCUSize() + 1;
    bool *validFlagPtr = bValidFlags;
    int numIntra = 0;

    for (uint32_t offset = 1; offset <= numUnitsInPU; offset++)
    {
        uint32_t uiPartBelowLeft;
        TComDataCU* pcCUBelowLeft = cu->getPUBelowLeftAdi(uiPartBelowLeft, partIdxLB, offset);
        if (pcCUBelowLeft && pcCUBelowLeft->isIntra(uiPartBelowLeft))
        {
            numIntra++;
            *validFlagPtr = true;
        }
        else
        {
            *validFlagPtr = false;
        }
        validFlagPtr--; // opposite direction
    }

    return numIntra;
}

//! \}
