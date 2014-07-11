/*****************************************************************************
 * Copyright (C) 2013 x265 project
 *
 * Authors: Chung Shin Yee <shinyee@multicorewareinc.com>
 *          Min Chen <chenm003@163.com>
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

#ifndef X265_FRAMEFILTER_H
#define X265_FRAMEFILTER_H

#include "common.h"
#include "frame.h"
#include "TLibCommon/TComLoopFilter.h"
#include "TLibEncoder/TEncSampleAdaptiveOffset.h"

namespace x265 {
// private x265 namespace

class Encoder;

// Manages the processing of a single frame loopfilter
class FrameFilter
{
public:

    FrameFilter();

    virtual ~FrameFilter() {}

    void init(Encoder *top, FrameEncoder *frame, int numRows, SBac* row0Coder);

    void destroy();

    void start(Frame *pic);

    void processRow(int row, ThreadLocalData& tld);
    void processRowPost(int row);
    void processSao(int row);

protected:

    x265_param*                 m_param;
    Frame*                      m_pic;
    FrameEncoder*               m_frame;
    int                         m_hChromaShift;
    int                         m_vChromaShift;
    int                         m_pad[2];

public:

    TComLoopFilter              m_loopFilter;
    TEncSampleAdaptiveOffset    m_sao;
    int                         m_numRows;
    int                         m_saoRowDelay;

    // SAO
    SBac                        m_sbacCoder;
    SBac*                       m_sbacCoderRow0;  // to mimic HM behavior
    
    /* Temp storage for ssim computation that doesn't need repeated malloc */
    void*                       m_ssimBuf;
};
}

#endif // ifndef X265_FRAMEFILTER_H
