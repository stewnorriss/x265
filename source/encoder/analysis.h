/*****************************************************************************
* Copyright (C) 2013 x265 project
*
* Authors: Deepthi Nandakumar <deepthi@multicorewareinc.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
*
* This program is also available under a commercial proprietary license.
* For more information, contact us at license @ x265.com.
*****************************************************************************/

#ifndef X265_ANALYSIS_H
#define X265_ANALYSIS_H

#define ANGULAR_MODE_ID 2
#define AMP_ID 3
#define INTER_MODES 4
#define INTRA_MODES 3

#include "common.h"
#include "predict.h"
#include "quant.h"
#include "yuv.h"
#include "shortyuv.h"
#include "threadpool.h"
#include "TLibCommon/TComDataCU.h"

#include "entropy.h"
#include "search.h"

namespace x265 {
// private namespace

struct StatisticLog
{
    uint64_t cntInter[4];
    uint64_t cntIntra[4];
    uint64_t cuInterDistribution[4][INTER_MODES];
    uint64_t cuIntraDistribution[4][INTRA_MODES];
    uint64_t cntIntraNxN;
    uint64_t cntSkipCu[4];
    uint64_t cntTotalCu[4];
    uint64_t totalCu;

    /* These states store the count of inter,intra and skip ctus within quad tree structure of each CU */
    uint32_t qTreeInterCnt[4];
    uint32_t qTreeIntraCnt[4];
    uint32_t qTreeSkipCnt[4];

    StatisticLog()
    {
        memset(this, 0, sizeof(StatisticLog));
    }
};

class Encoder;
class Entropy;
struct ThreadLocalData;

class Analysis : public JobProvider, public Search
{
public:

    enum {
        PRED_2Nx2N,
        PRED_NxN,
        PRED_Nx2N,
        PRED_2NxN,
        PRED_2NxnU,
        PRED_2NxnD,
        PRED_nLx2N,
        PRED_nRx2N,
        PRED_MERGE,
        PRED_SKIP,
        PRED_INTRA,     // 2Nx2N intra
        PRED_INTRA_NxN, // 4x4 PU blocks for 8x8 CU
        PRED_SPLIT,
        MAX_PRED_TYPES
    };

    struct ModeDepth
    {
        Mode           pred[MAX_PRED_TYPES];
        Mode*          bestMode;
        Yuv            origYuv;
        ShortYuv       tempResi;
        DataCUMemPool  cuMemPool;
        MVFieldMemPool mvFieldMemPool;
    };

    ModeDepth     m_modeDepth[NUM_CU_DEPTH];
    bool          m_bEncodeDQP;

    StatisticLog  m_sliceTypeLog[3];
    Frame*        m_frame;
    const Slice*  m_slice;

    Analysis();
    bool create(uint32_t totalDepth, uint32_t maxWidth, ThreadLocalData* tld);
    void destroy();
    void compressCTU(TComDataCU& ctu, const Entropy& initialContext);

protected:

    /* Job provider details */
    const CU*     m_curCUData;
    int           m_curDepth;
    ThreadLocalData* m_tld;
    bool          m_bJobsQueued;
    bool findJob(int threadId);

    /* mode analysis distribution */
    Entropy       m_intraContexts;
    int           m_totalNumJobs;
    volatile int  m_numAcquiredJobs;
    volatile int  m_numCompletedJobs;
    Event         m_modeCompletionEvent;
    void parallelAnalysisJob(int threadId, int jobId);

    /* motion estimation distribution */
    TComDataCU*   m_curMECu;
    int           m_curPart;
    MotionData    m_bestME[2];
    uint32_t      m_listSelBits[3];
    int           m_totalNumME;
    volatile int  m_numAcquiredME;
    volatile int  m_numCompletedME;
    Event         m_meCompletionEvent;
    Lock          m_outputLock;
    void parallelME(int threadId, int meId);

    /* Warning: The interface for these functions will undergo significant changes as a major refactor is under progress */
    void compressIntraCU(const TComDataCU& parentCTU, const CU& cuData, uint32_t partIndex);
    void compressSharedIntraCTU(const TComDataCU& parentCTU, const CU& cuData, uint32_t partIndex,
                                uint8_t* sharedDepth, char* sharedPartSizes, uint8_t* sharedModes, uint32_t &zOrder);
    void checkIntra(Mode& intraMode, const CU& cuData, PartSize partSize, uint8_t* sharedModes);

    void compressInterCU_rd0_4(const TComDataCU& parentCTU, const CU& cuData, uint32_t partIndex);
    void compressInterCU_rd5_6(const TComDataCU& parentCTU, const CU& cuData, uint32_t partIndex);
    void checkBestMode(Mode& mode, uint32_t depth);

    /* measure merge and skip */
    void checkMerge2Nx2N_rd0_4(const CU& cuData, uint32_t depth);
    void checkMerge2Nx2N_rd5_6(const CU& cuData, uint32_t depth, bool& earlySkip);

    /* measure inter options */
    void checkInter_rd0_4(Mode& interMode, const CU& cuData, PartSize partSize);
    void checkInter_rd5_6(Mode& interMode, const CU& cuData, PartSize partSize, bool bMergeOnly);
    void parallelInterSearch(Mode& interMode, const CU& cuData, bool bChroma);

    /* measure intra options */
    void checkIntra(const TComDataCU& parentCTU, const CU& cuData, PartSize partSize, uint8_t* sharedModes);
    void checkIntraInInter_rd0_4(Mode& intraMode, const CU& cuData);
    void checkIntraInInter_rd5_6(Mode& intraMode, const CU& cuData, PartSize partSize);
    void encodeIntraInInter(Mode& intraMode, const CU& cuData);

    void encodeResidue(const TComDataCU& parentCTU, const CU& cuData);
    void checkDQP(TComDataCU* cu, const CU& cuData);
    void deriveTestModeAMP(const TComDataCU& cu, bool &bHor, bool &bVer, bool &bMergeOnly);
    void fillOrigYUVBuffer(TComDataCU* cu, const Yuv* origYuv);
};

struct ThreadLocalData
{
    Analysis analysis;

    ~ThreadLocalData() { analysis.destroy(); }
};

}

#endif // ifndef X265_ANALYSIS_H
