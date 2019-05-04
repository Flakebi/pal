
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

#include "palPipelineAbiProcessor.h"
#include "palElfProcessor.h"
#include "palInlineFuncs.h"

// TODO Use pal vector
#include <vector>
#include <cstdio>

namespace Util
{

// =====================================================================================================================
struct SectionInfo
{
    uint32  id;
    gpusize offset;
};

// =====================================================================================================================
// Stores the mapping from ELF sections to GPU memory offsets.
class SectionMemoryMap
{
public:
    SectionMemoryMap() :
        m_alignment(0),
        m_size(0)
    {}
    virtual ~SectionMemoryMap() {}

    template <typename Allocator>
    void AddSection(const Util::Elf::Section<Allocator> *const section)
    {
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
    uint32 GetSectionIndex(size_t i) const { return m_sections[i].id; }
    uint64 GetAlignment() const { return m_alignment; }
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

    void DebugPrint() const
    {
        puts("---- Begin Section mapping");
        for (const auto& section : m_sections)
            printf("0x%0llx: %u\n", section.offset, section.id);
        puts("---- End Section mapping\n");
    }

private:
    uint64 m_alignment;
    gpusize m_size;
    std::vector<SectionInfo> m_sections;

    PAL_DISALLOW_COPY_AND_ASSIGN(SectionMemoryMap);
};

} // Pal
