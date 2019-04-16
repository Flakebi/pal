
/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2014-2019 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 **********************************************************************************************************************/

#pragma once

// TODO Needed headers
#include "core/device.h"
#include "core/gpuMemory.h"
#include "palElfPackager.h"
#include "palLib.h"
#include "palMetroHash.h"
#include "palSparseVectorImpl.h"
#include "palPipeline.h"
#include "palPipelineAbiProcessor.h"

#include "palElfProcessor.h"
#include "palInlineFuncs.h"

#include <vector>

namespace Pal
{

// =====================================================================================================================
struct SectionInfo
{
    uint32  id;
    gpusize offset;
};

// =====================================================================================================================
// Stores the mapping from ELF sections to ELF segments.
// This has the function of an ELF segment. As shaders are not linked, there
// are no standard ELF segments and this class is used to group sections with
// the same flags.
class PipelineSectionSegmentMapping
{
public:
    PipelineSectionSegmentMapping(uint64 flags) :
        m_flags(flags),
        m_alignment(0),
        m_size(0)
    {}
    virtual ~PipelineSectionSegmentMapping() {}

    template <typename Allocator>
    void AddSection(const Util::Elf::Section<Allocator> *const section)
    {
        //PAL_ASSERT(section->GetSetcionHeader().sh_flags == m_flags
        //    && "All sections in a segment must have the same flags");

        SectionInfo info;
        info.id = section->GetIndex();
        uint64 alignment = section->GetSectionHeader()->sh_addralign;
        info.offset = Util::Pow2Align(m_size, alignment);
        m_size = info.offset + section->GetDataSize();

        if (alignment > m_alignment)
            m_alignment = alignment;

        m_sections.push_back(info);
    }

    uint32 GetNumSections() const { return m_sections.size(); }
    uint64 GetAlignment() const { return m_alignment; }
    gpusize GetFlags() const { return m_flags; }
    gpusize GetSize() const { return m_size; }
    Result GetSectionOffset(uint32 sectionIndex, gpusize *offset) const
    {
        for (const auto& section : m_sections)
        {
            if (section.id == sectionIndex)
            {
                *offset = section.offset;
                return Result::Success;
            }
        }
        return Result::ErrorUnavailable;
    }

private:
    uint64 m_flags;
    uint64 m_alignment;
    gpusize m_size;
    std::vector<SectionInfo> m_sections;

    PAL_DISALLOW_DEFAULT_CTOR(PipelineSectionSegmentMapping);
    PAL_DISALLOW_COPY_AND_ASSIGN(PipelineSectionSegmentMapping);
};

// =====================================================================================================================
// Stores the mapping from ELF segments to GPU memory.
class PipelineGpuMapping
{
public:
    PipelineGpuMapping() {}
    virtual ~PipelineGpuMapping() {}

    template <typename Allocator>
    void AddSection(const Util::Elf::Section<Allocator> *const section)
    {
        uint64 flags = section->GetSectionHeader().sh_flags;
        // Check if we have a segment with these flags
        for (auto& segment : m_segments)
        {
            if (segment.GetFlags() == flags)
            {
                segment.AddSection(section);
                return;
            }
        }

        // Create new segment
        PipelineSectionSegmentMapping m(flags);
        m.AddSection(section);
        m_segments.push_back(std::move(m));
    }

    size_t GetSegmentCount() const { return m_segments.size(); }
    PipelineSectionSegmentMapping& GetSegment(size_t i) { return m_segments[i]; }
    Result GetSectionPosition(uint32 sectionIndex, uint64 sectionFlags, size_t *segmentIndex, gpusize *offset) const
    {
        for (size_t i = 0; i < m_segments.size(); i++)
        {
            if (m_segments[i].GetFlags() == sectionFlags)
            {
                *segmentIndex = i;
                return m_segments[i].GetSectionOffset(sectionIndex, offset);
            }
        }
        return Result::ErrorUnavailable;
    }

private:
    std::vector<PipelineSectionSegmentMapping> m_segments;

    PAL_DISALLOW_COPY_AND_ASSIGN(PipelineGpuMapping);
};

} // Pal
