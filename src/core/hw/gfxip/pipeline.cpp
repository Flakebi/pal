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

#include "core/device.h"
#include "core/g_palSettings.h"
#include "core/platform.h"
#include "core/hw/gfxip/gfxDevice.h"
#include "core/hw/gfxip/pipeline.h"
#include "palFile.h"
#include "palPipelineAbiProcessorImpl.h"
extern "C" {
    #include "llvmInstrProfiling.h"
}

#include "core/devDriverUtil.h"

using namespace Util;

// Don't use auto initialization of PGO code
int __llvm_profile_runtime = 0;

namespace Pal
{

// The generator describes the pipeline is generated by PAL or extern tool. Driver doesn't need to validate the
// buildId or settingsHash if it's generated by an extern tool.
enum class SerializedPipelineGenerator : uint32
{
    Pal         = 0, // The pipeline is generated by Pal driver.
    ExternTool  = 1, // The pipeline is generated by extern tool.
};

// Represents information for compatibility checks when loading a stored pipeline.  If a pipeline was stored by a
// different version of PAL than the version loading it, the load will fail.
struct SerializedPipelineHeader
{
    uint32        deviceId;       // As in DeviceProperties.
    BuildUniqueId buildId;        // 16-byte identifier for a particular PAL build (typically a time-stamp of the
                                  // compiled library that serialized the pipeline.
    MetroHash::Hash settingsHash; // Hash of the active PAL settings this pipeline was compiled with.

    // Serialize the base addresses of each VA range partition since some of those are baked into compiled shaders.
    gpusize vaRangeBaseAddr[static_cast<uint32>(VaRange::Count)];

    SerializedPipelineGenerator generator; // Indicates what generated this pipeline.
};

// Private structure used to store/load a data members of a pipeline object.
struct SerializedData
{
    size_t          totalGpuMemSize;
    PipelineInfo    info;
    ShaderMetadata  shaderMetadata;
};

// =====================================================================================================================
Pipeline::Pipeline(
    Device* pDevice,
    bool    isInternal)  // True if this is a PAL-owned pipeline (i.e., an RPM pipeline).
    :
    m_pDevice(pDevice),
    m_gpuMem(),
    m_gpuMemSize(0),
    m_dataLength(0),
    m_dataOffset(0),
    m_pPipelineBinary(nullptr),
    m_pipelineBinaryLen(0),
    m_apiHwMapping()
{
    m_flags.value      = 0;
    m_flags.isInternal = isInternal;

    m_apiHwMapping.u64All = 0;

    memset(&m_info, 0, sizeof(m_info));
    memset(&m_shaderMetaData, 0, sizeof(m_shaderMetaData));
    memset(&m_perfDataInfo, 0, sizeof(m_perfDataInfo));
}

// =====================================================================================================================
Pipeline::~Pipeline()
{
    if (m_gpuMem.IsBound())
    {
        if (!IsInternal())
        {
            printf("Destroying pipeline\n");
            PrintData();
        }
        m_pDevice->MemMgr()->FreeGpuMem(m_gpuMem.Memory(), m_gpuMem.Offset());
        m_gpuMem.Update(nullptr, 0);
    }

    PAL_SAFE_FREE(m_pPipelineBinary, m_pDevice->GetPlatform());
}

// =====================================================================================================================
static void PrintHex(const char* ptr, size_t len)
{
    for (size_t i = 0; i < len; i++)
        printf("0x%hhx, ", ptr[i]);
    puts("");
}

// =====================================================================================================================
void Pipeline::PrintData()
{
    if (m_dataLength > 0)
    {
        void *pMappedPtr;
        Result result = m_gpuMem.Map(&pMappedPtr);
        if (result == Result::Success)
        {
            pMappedPtr = VoidPtrInc(pMappedPtr, m_dataOffset);
            printf("Data: ");
            PrintHex(static_cast<char*>(pMappedPtr), m_dataLength);
            m_gpuMem.Unmap();
        }
        else
        {
            printf("Failed to map memory\n");
        }

        // Write profile data
        // TODO
        __llvm_profile_set_filename("/home/sebi/Downloads/test-%m.prof");
        int r = __llvm_profile_dump();
        if (r)
        {
            printf("Failed to dump profiling data (%d)\n", r);
        }
    }
}

// =====================================================================================================================
void Pipeline::PrintText(void* pMappedPtr, size_t offset, size_t length)
{
    pMappedPtr = VoidPtrInc(pMappedPtr, offset);
    unsigned int* ptr = static_cast<unsigned int*>(pMappedPtr);
    printf("Text: ");
    for (size_t i = 0; i < length / sizeof(*ptr); i++)
        printf("0x%0x, ", ptr[i]);
    puts("");
}

// =====================================================================================================================
// Destroys a pipeline object allocated via a subclass' CreateInternal()
void Pipeline::DestroyInternal()
{
    PAL_ASSERT(IsInternal());

    Platform*const pPlatform = m_pDevice->GetPlatform();
    Destroy();
    PAL_FREE(this, pPlatform);
}

// =====================================================================================================================
// Allocates GPU memory for this pipeline and uploads the code and data contain in the ELF binary to it.  Any ELF
// relocations are also applied to the memory during this operation.
Result Pipeline::PerformRelocationsAndUploadToGpuMemory(
    const AbiProcessor&       abiProcessor,
    const CodeObjectMetadata& metadata,
    PipelineUploader*         pUploader,
    bool                      preferNonLocalHeap)
{
    PAL_ASSERT(pUploader != nullptr);

    Util::PipelineSectionSegmentMapping mapping(0);
    Result result = pUploader->Begin(m_pDevice, abiProcessor, metadata, &m_perfDataInfo[0], preferNonLocalHeap, mapping);
    if (result == Result::Success)
    {
        m_gpuMemSize = pUploader->GpuMemSize();
        m_gpuMem.Update(pUploader->GpuMem(), pUploader->GpuMemOffset());
        m_dataOffset = pUploader->DataOffset();
        m_dataLength = pUploader->DataLength();

        // Perform relocations
        gpusize gpuVirtAddr = (pUploader->GpuMem()->Desc().gpuVirtAddr + pUploader->GpuMemOffset());
        result = abiProcessor.ApplyRelocations(
            pUploader->MappedAddr(),
            gpuVirtAddr,
            mapping
        );

        if (!IsInternal())
        {
            printf("GPU offset address: 0x%llx\n", gpuVirtAddr);
            mapping.DebugPrint();
            printf("Uploaded pipeline\n");
            void* pMappedPtr = VoidPtrInc(pUploader->MappedAddr(), pUploader->DataOffset());
            printf("Data: ");
            PrintHex(static_cast<char*>(pMappedPtr), pUploader->DataLength());
            PrintText(pUploader->MappedAddr(), pUploader->TextOffset(), pUploader->TextLength());
        }
    }

    return result;
}

// =====================================================================================================================
// Helper function for extracting the pipeline hash and per-shader hashes from pipeline metadata.
void Pipeline::ExtractPipelineInfo(
    const CodeObjectMetadata& metadata,
    ShaderType                firstShader,
    ShaderType                lastShader)
{
    m_info.internalPipelineHash =
        { metadata.pipeline.internalPipelineHash[0], metadata.pipeline.internalPipelineHash[1] };

#if PAL_CLIENT_INTERFACE_MAJOR_VERSION < 476
    // Default the PAL runtime hash to the unique portion of the internal pipeline hash. PAL pipelines that include
    // additional state should override this with a new hash composed of that state and this hash.
    m_info.palRuntimeHash = m_info.internalPipelineHash.unique;
#endif

    // We don't expect the pipeline ABI to report a hash of zero.
    PAL_ALERT((metadata.pipeline.internalPipelineHash[0] | metadata.pipeline.internalPipelineHash[1]) == 0);

#if PAL_CLIENT_INTERFACE_MAJOR_VERSION < 460
    m_info.compilerHash = m_info.internalPipelineHash.stable;
    m_info.pipelineHash = m_info.internalPipelineHash.unique;
#endif

    for (uint32 s = static_cast<uint32>(firstShader); s <= static_cast<uint32>(lastShader); ++s)
    {
        Abi::ApiShaderType shaderType = static_cast<Abi::ApiShaderType>(s);

        const auto& shaderMetadata = metadata.pipeline.shader[s];

        m_info.shader[s].hash = { shaderMetadata.apiShaderHash[0], shaderMetadata.apiShaderHash[1] };
        m_apiHwMapping.apiShaders[s] = static_cast<uint8>(shaderMetadata.hardwareMapping);
    }
}

// =====================================================================================================================
// Query this pipeline's Bound GPU Memory.
Result Pipeline::QueryAllocationInfo(
    size_t*                   pNumEntries,
    GpuMemSubAllocInfo* const pGpuMemList
    ) const
{
    Result result = Result::ErrorInvalidPointer;

    if (pNumEntries != nullptr)
    {
        (*pNumEntries) = 1;

        if (pGpuMemList != nullptr)
        {
            pGpuMemList[0].offset     = m_gpuMem.Offset();
            pGpuMemList[0].pGpuMemory = m_gpuMem.Memory();
            pGpuMemList[0].size       = m_gpuMemSize;
        }

        result = Result::Success;
    }

    return result;
}

// =====================================================================================================================
// Extracts the pipeline's code object ELF binary.
Result Pipeline::GetPipelineElf(
    uint32*    pSize,
    void*      pBuffer
    ) const
{
    Result result = Result::ErrorInvalidPointer;

    if (pSize != nullptr)
    {
        if ((m_pPipelineBinary != nullptr) && (m_pipelineBinaryLen != 0))
        {
            if (pBuffer == nullptr)
            {
                (*pSize) = static_cast<uint32>(m_pipelineBinaryLen);
                result = Result::Success;
            }
            else if ((*pSize) >= static_cast<uint32>(m_pipelineBinaryLen))
            {
                memcpy(pBuffer, m_pPipelineBinary, m_pipelineBinaryLen);
                result = Result::Success;
            }
            else
            {
                result = Result::ErrorInvalidMemorySize;
            }
        }
        else
        {
            result = Result::ErrorUnavailable;
        }
    }

    return result;
}

// =====================================================================================================================
// Extracts the binary shader instructions for a specific API shader stage.
Result Pipeline::GetShaderCode(
    ShaderType shaderType,
    size_t*    pSize,
    void*      pBuffer
    ) const
{
    Result result = Result::ErrorUnavailable;

    const ShaderStageInfo*const pInfo = GetShaderStageInfo(shaderType);
    if (pSize == nullptr)
    {
        result = Result::ErrorInvalidPointer;
    }
    else if (pInfo != nullptr)
    {
        PAL_ASSERT(pInfo->codeLength != 0); // How did we get here if there's no shader code?!

        if (pBuffer == nullptr)
        {
            (*pSize) = pInfo->codeLength;
            result   = Result::Success;
        }
        else if ((*pSize) >= pInfo->codeLength)
        {
            // To extract the shader code, we can re-parse the saved ELF binary and lookup the shader's program
            // instructions by examining the symbol table entry for that shader's entrypoint.
            AbiProcessor abiProcessor(m_pDevice->GetPlatform());
            result = abiProcessor.LoadFromBuffer(m_pPipelineBinary, m_pipelineBinaryLen);
            if (result == Result::Success)
            {
                const auto& symbol = abiProcessor.GetPipelineSymbolEntry(
                        Abi::GetSymbolForStage(Abi::PipelineSymbolType::ShaderMainEntry, pInfo->stageId));
                PAL_ASSERT(symbol.size == pInfo->codeLength);

                const void* pCodeSection   = nullptr;
                size_t      codeSectionLen = 0;
                abiProcessor.GetPipelineCode(&pCodeSection, &codeSectionLen);
                PAL_ASSERT((symbol.size + symbol.value) <= codeSectionLen);

                memcpy(pBuffer,
                       VoidPtrInc(pCodeSection, static_cast<size_t>(symbol.value)),
                       static_cast<size_t>(symbol.size));
            }
        }
        else
        {
            result = Result::ErrorInvalidMemorySize;
        }
    }

    return result;
}

// =====================================================================================================================
// Extracts the performance data from GPU memory and copies it to the specified buffer.
Result Pipeline::GetPerformanceData(
    Util::Abi::HardwareStage hardwareStage,
    size_t*                  pSize,
    void*                    pBuffer)
{
    Result       result       = Result::ErrorUnavailable;
    const uint32 index        = static_cast<uint32>(hardwareStage);
    const auto&  perfDataInfo = m_perfDataInfo[index];

    if (pSize == nullptr)
    {
        result = Result::ErrorInvalidPointer;
    }
    else if (perfDataInfo.sizeInBytes > 0)
    {
        if (pBuffer == nullptr)
        {
            (*pSize) = perfDataInfo.sizeInBytes;
            result   = Result::Success;
        }
        else if ((*pSize) >= perfDataInfo.sizeInBytes)
        {
            void* pData = nullptr;
            result = m_gpuMem.Map(&pData);

            if (result == Result::Success)
            {
                memcpy(pBuffer, VoidPtrInc(pData, perfDataInfo.cpuOffset), perfDataInfo.sizeInBytes);
                result = m_gpuMem.Unmap();
            }
        }
    }

    return result;
}

// =====================================================================================================================
// Helper method which extracts shader statistics from the pipeline ELF binary for a particular hardware stage.
Result Pipeline::GetShaderStatsForStage(
    const ShaderStageInfo& stageInfo,
    const ShaderStageInfo* pStageInfoCopy, // Optional: Non-null if we care about copy shader statistics.
    ShaderStats*           pStats
    ) const
{
    PAL_ASSERT(pStats != nullptr);
    memset(pStats, 0, sizeof(ShaderStats));

    // We can re-parse the saved pipeline ELF binary to extract shader statistics.
    AbiProcessor abiProcessor(m_pDevice->GetPlatform());
    Result result = abiProcessor.LoadFromBuffer(m_pPipelineBinary, m_pipelineBinaryLen);

    MsgPackReader      metadataReader;
    CodeObjectMetadata metadata;

    if (result == Result::Success)
    {
        result = abiProcessor.GetMetadata(&metadataReader, &metadata);
    }

    if (result == Result::Success)
    {
        const auto&  gpuInfo       = m_pDevice->ChipProperties();
        const auto&  stageMetadata = metadata.pipeline.hardwareStage[static_cast<uint32>(stageInfo.stageId)];

        pStats->common.numUsedSgprs = stageMetadata.sgprCount;
        pStats->common.numUsedVgprs = stageMetadata.vgprCount;

#if PAL_BUILD_GFX6
        if (gpuInfo.gfxLevel < GfxIpLevel::GfxIp9)
        {
            pStats->numAvailableSgprs = (stageMetadata.hasEntry.sgprLimit != 0) ? stageMetadata.sgprLimit
                                                                                : gpuInfo.gfx6.numShaderVisibleSgprs;
            pStats->numAvailableVgprs = (stageMetadata.hasEntry.vgprLimit != 0) ? stageMetadata.vgprLimit
                                                                                : gpuInfo.gfx6.numShaderVisibleVgprs;
        }
#endif

#if PAL_BUILD_GFX9
        if (gpuInfo.gfxLevel >= GfxIpLevel::GfxIp9)
        {
            pStats->numAvailableSgprs = (stageMetadata.hasEntry.sgprLimit != 0) ? stageMetadata.sgprLimit
                                                                                : gpuInfo.gfx9.numShaderVisibleSgprs;
            pStats->numAvailableVgprs = (stageMetadata.hasEntry.vgprLimit != 0) ? stageMetadata.vgprLimit
                                                                                : gpuInfo.gfx9.numShaderVisibleVgprs;
        }
#endif

        pStats->common.ldsUsageSizeInBytes    = stageMetadata.ldsSize;
        pStats->common.scratchMemUsageInBytes = stageMetadata.scratchMemorySize;

        pStats->isaSizeInBytes = stageInfo.disassemblyLength;

        if (pStageInfoCopy != nullptr)
        {
            const auto& copyStageMetadata =
                metadata.pipeline.hardwareStage[static_cast<uint32>(pStageInfoCopy->stageId)];

            pStats->flags.copyShaderPresent = 1;

            pStats->copyShader.numUsedSgprs = copyStageMetadata.sgprCount;
            pStats->copyShader.numUsedVgprs = copyStageMetadata.vgprCount;

            pStats->copyShader.ldsUsageSizeInBytes    = copyStageMetadata.ldsSize;
            pStats->copyShader.scratchMemUsageInBytes = copyStageMetadata.scratchMemorySize;
        }
    }

    return result;
}

// =====================================================================================================================
// Calculates the size, in bytes, of the performance data buffers needed total for the entire pipeline.
size_t Pipeline::PerformanceDataSize(
    const CodeObjectMetadata& metadata
    ) const
{
    size_t dataSize = 0;

    for (uint32 i = 0; i < static_cast<uint32>(Abi::HardwareStage::Count); i++)
    {
        dataSize += metadata.pipeline.hardwareStage[i].perfDataBufferSize;
    }

    return dataSize;
}

// =====================================================================================================================
void Pipeline::DumpPipelineElf(
    const AbiProcessor& abiProcessor,
    const char*         pPrefix,
    const char*         pName         // Optional: Non-null if we want to use a human-readable name for the filename.
    ) const
{
#if PAL_ENABLE_PRINTS_ASSERTS
    const PalSettings& settings = m_pDevice->Settings();
    uint64 hashToDump = settings.pipelineLogConfig.logPipelineHash;
    bool hashMatches = ((hashToDump == 0) || (m_info.internalPipelineHash.stable == hashToDump));

    const bool dumpInternal  = settings.pipelineLogConfig.logInternal;
    const bool dumpExternal  = settings.pipelineLogConfig.logExternal;
    const bool dumpPipeline  =
        (hashMatches && ((dumpExternal && !IsInternal()) || (dumpInternal && IsInternal())));

    if (dumpPipeline)
    {
        const char*const pLogDir = &settings.pipelineLogConfig.pipelineLogDirectory[0];

        char fileName[512] = { };
        if ((pName == nullptr) || (pName[0] == '\0'))
        {
            Snprintf(&fileName[0],
                     sizeof(fileName),
                     "%s/%s_0x%016llX.elf",
                     pLogDir,
                     pPrefix,
                     m_info.internalPipelineHash.stable);
        }
        else
        {
            Snprintf(&fileName[0], sizeof(fileName), "%s/%s_%s.elf", pLogDir, pPrefix, pName);
        }

        File file;
        file.Open(fileName, FileAccessWrite | FileAccessBinary);
        file.Write(m_pPipelineBinary, m_pipelineBinaryLen);
    }
#endif
}

// =====================================================================================================================
PipelineUploader::PipelineUploader(
    uint32 ctxRegisterCount,
    uint32 shRegisterCount)
    :
    m_pGpuMemory(nullptr),
    m_baseOffset(0),
    m_gpuMemSize(0),
    m_codeGpuVirtAddr(0),
    m_dataGpuVirtAddr(0),
    m_ctxRegGpuVirtAddr(0),
    m_shRegGpuVirtAddr(0),
    m_shRegisterCount(shRegisterCount),
    m_ctxRegisterCount(ctxRegisterCount),
    m_dataOffset(0),
    m_dataLength(0),
    m_textOffset(0),
    m_textLength(0),
    m_pMappedPtr(nullptr),
    m_pCtxRegWritePtr(nullptr),
    m_pShRegWritePtr(nullptr)
#if PAL_ENABLE_PRINTS_ASSERTS
    , m_pCtxRegWritePtrStart(nullptr),
    m_pShRegWritePtrStart(nullptr)
#endif
{
}

// =====================================================================================================================
PipelineUploader::~PipelineUploader()
{
    PAL_ASSERT(m_pMappedPtr == nullptr); // If this fires, the caller forgot to call End()!
}

// =====================================================================================================================
// Allocates GPU memory for the current pipeline.  Also, maps the memory for CPU access and uploads the pipeline code
// and data.  The GPU virtual addresses for the code, data, and register segments are also computed.  The caller is
// responsible for calling End() which unmaps the GPU memory.
Result PipelineUploader::Begin(
    Device*                        pDevice,
    const AbiProcessor&            abiProcessor,
    const CodeObjectMetadata&      metadata,
    PerfDataInfo*                  pPerfDataInfoList,
    bool                           preferNonLocalHeap,
    Util::PipelineSectionSegmentMapping& mapping)
{
    PAL_ASSERT(pPerfDataInfoList != nullptr);

    constexpr size_t GpuMemByteAlign = 256;

    GpuMemoryCreateInfo createInfo = { };
    createInfo.alignment = GpuMemByteAlign;
    createInfo.vaRange   = VaRange::DescriptorTable;

    if (preferNonLocalHeap)
    {
        createInfo.heaps[0]  = GpuHeapGartUswc;
        createInfo.heapCount = 1;
    }
    else
    {
        createInfo.heaps[0]  = GpuHeapLocal;
        createInfo.heaps[1]  = GpuHeapGartUswc;
        createInfo.heapCount = 2;
    }

    createInfo.priority  = GpuMemPriority::High;

    GpuMemoryInternalCreateInfo internalInfo = { };
    internalInfo.flags.alwaysResident = 1;

    const void* pDataBuffer   = nullptr;
    size_t      dataLength    = 0;
    gpusize     dataAlignment = 0;
    abiProcessor.GetData(&pDataBuffer, &dataLength, &dataAlignment);

    // For now, have one segment containing all sections
    auto elfProcessor = abiProcessor.GetElfProcessor();
    auto sections = elfProcessor->GetSections();
    for (uint32 i = 0; i < sections->NumSections(); i++)
    {
        auto section = sections->Get(i);
        uint32 flags = section->GetSectionHeader()->sh_flags;
        if ((flags & Elf::ShfWrite) || (flags & Elf::ShfExecInstr))
        mapping.AddSection(section);
    }
    createInfo.size = mapping.GetSize();

    const uint32 totalRegisters = (m_ctxRegisterCount + m_shRegisterCount);
    if (totalRegisters > 0)
    {
        constexpr uint32 RegisterEntryBytes = (sizeof(uint32) << 1);
        createInfo.size = (Pow2Align(createInfo.size, sizeof(uint32)) + (RegisterEntryBytes * totalRegisters));
    }

    // Compute the total size of all shader stages' performance data buffers.
    gpusize performanceDataOffset = createInfo.size;
    for (uint32 s = 0; s < static_cast<uint32>(Abi::HardwareStage::Count); ++s)
    {
        const uint32 performanceDataBytes = metadata.pipeline.hardwareStage[s].perfDataBufferSize;
        if (performanceDataBytes != 0)
        {
            pPerfDataInfoList[s].sizeInBytes = performanceDataBytes;
            pPerfDataInfoList[s].cpuOffset   = static_cast<size_t>(performanceDataOffset);

            createInfo.size       += performanceDataBytes;
            performanceDataOffset += performanceDataBytes;
        }
    } // for each hardware stage

    // The driver must make sure there is a distance of at least gpuInfo.shaderPrefetchBytes
    // that follows the end of the shader to avoid a page fault when the SQ tries to
    // prefetch past the end of a shader

    // shaderPrefetchBytes is set from "SQC_CONFIG.INST_PRF_COUNT" (gfx8-9)
    // defaulting to the hardware supported maximum if necessary

    size_t codeLength = abiProcessor.GetTextSection()->GetDataSize();
    const gpusize minSafeSize = Pow2Align(codeLength, ShaderICacheLineSize) +
                                pDevice->ChipProperties().gfxip.shaderPrefetchBytes;

    createInfo.size = Max(createInfo.size, minSafeSize);

    Result result = pDevice->MemMgr()->AllocateGpuMem(createInfo, internalInfo, false, &m_pGpuMemory, &m_baseOffset);
    if (result == Result::Success)
    {
        result = m_pGpuMemory->Map(&m_pMappedPtr);
        if (result == Result::Success)
        {
            gpusize offset;
            m_gpuMemSize = createInfo.size;
            m_pMappedPtr = VoidPtrInc(m_pMappedPtr, static_cast<size_t>(m_baseOffset));

            // Copy sections
            for (uint32 i = 0; i < mapping.GetNumSections(); i++)
            {
                uint32 sectionIndex = mapping.GetSectionIndex(i);
                auto section = sections->Get(sectionIndex);
                result = mapping.GetSectionOffset(sectionIndex, &offset);
                if (result != Result::Success)
                    return result;

                void* pMappedPtr = VoidPtrInc(m_pMappedPtr, static_cast<size_t>(offset));
                memcpy(pMappedPtr, section->GetData(), section->GetDataSize());
            }

            gpusize gpuVirtAddr = (m_pGpuMemory->Desc().gpuVirtAddr + m_baseOffset);
            void* pMappedPtr = m_pMappedPtr;

            // Find PGO performance counters
            uint32 perfSectionIndex = sections->GetSectionIndex("__llvm_prf_cnts");
            if (perfSectionIndex)
            {
                result = mapping.GetSectionOffset(perfSectionIndex, &offset);
                if (result != Result::Success)
                    return result;

                auto perfSection = sections->Get(perfSectionIndex);
                m_dataLength = perfSection->GetDataSize();
                m_dataOffset = offset;
            }

            // Find offset of .text section
            auto textSection = abiProcessor.GetTextSection();
            uint32 sectionIndex = textSection->GetIndex();
            result = mapping.GetSectionOffset(sectionIndex, &offset);
            if (result != Result::Success)
                return result;
            m_codeGpuVirtAddr     = gpuVirtAddr + offset;
            m_prefetchGpuVirtAddr = m_codeGpuVirtAddr;
            m_prefetchSize        = textSection->GetDataSize();
            m_textOffset          = offset;
            m_textLength          = m_prefetchSize;

            if (dataLength > 0)
            {
                // Find offset of .data section
                auto dataSection = abiProcessor.GetDataSection();
                sectionIndex = dataSection->GetIndex();
                result = mapping.GetSectionOffset(sectionIndex, &offset);
                if (result != Result::Success)
                    return result;
                m_dataGpuVirtAddr = gpuVirtAddr + offset;

                pMappedPtr  = VoidPtrInc(m_pMappedPtr, static_cast<size_t>(offset));

                // The for loop which follows is entirely non-standard behavior for an ELF loader, but is intended to
                // only be temporary code.
                for (uint32 s = 0; s < static_cast<uint32>(Abi::HardwareStage::Count); ++s)
                {
                    const Abi::PipelineSymbolType symbolType =
                        Abi::GetSymbolForStage(Abi::PipelineSymbolType::ShaderIntrlTblPtr,
                                               static_cast<Abi::HardwareStage>(s));

                    Abi::PipelineSymbolEntry symbol = { };
                    if (abiProcessor.HasPipelineSymbolEntry(symbolType, &symbol) &&
                        (symbol.sectionType == Abi::AbiSectionType::Data))
                    {
                        pDevice->GetGfxDevice()->PatchPipelineInternalSrdTable(
                            VoidPtrInc(pMappedPtr,  static_cast<size_t>(symbol.value)), // Dst
                            VoidPtrInc(pDataBuffer, static_cast<size_t>(symbol.value)), // Src
                            static_cast<size_t>(symbol.size),
                            m_dataGpuVirtAddr);
                    }
                } // for each hardware stage
                // End temporary code

                // gpuVirtAddr points to end of data
                // TODO What is with m_prefetchSize?
                //m_prefetchSize = gpuVirtAddr - m_prefetchGpuVirtAddr;
            } // if dataLength > 0

            pMappedPtr = VoidPtrInc(m_pMappedPtr, static_cast<size_t>(mapping.GetSize()));
            gpuVirtAddr += mapping.GetSize();
            if (totalRegisters > 0)
            {
                gpusize regGpuVirtAddr = Pow2Align(gpuVirtAddr, sizeof(uint32));
                uint32* pRegWritePtr   = static_cast<uint32*>(VoidPtrAlign(pMappedPtr, sizeof(uint32)));

                if (m_ctxRegisterCount > 0)
                {
                    m_ctxRegGpuVirtAddr = regGpuVirtAddr;
                    m_pCtxRegWritePtr   = pRegWritePtr;

                    regGpuVirtAddr += (m_ctxRegisterCount * (sizeof(uint32) * 2));
                    pRegWritePtr   += (m_ctxRegisterCount * 2);
                }

                if (m_shRegisterCount > 0)
                {
                    m_shRegGpuVirtAddr = regGpuVirtAddr;
                    m_pShRegWritePtr   = pRegWritePtr;
                }

#if PAL_ENABLE_PRINTS_ASSERTS
                m_pCtxRegWritePtrStart = m_pCtxRegWritePtr;
                m_pShRegWritePtrStart  = m_pShRegWritePtr;
#endif
            }

            // Initialize the performance data buffer for each shader stage and finalize its GPU virtual address.
            for (uint32 s = 0; s < static_cast<uint32>(Abi::HardwareStage::Count); ++s)
            {
                if (pPerfDataInfoList[s].sizeInBytes != 0)
                {
                    const size_t offset = pPerfDataInfoList[s].cpuOffset;
                    pPerfDataInfoList[s].gpuVirtAddr = LowPart(gpuVirtAddr + offset);
                    memset(VoidPtrInc(m_pMappedPtr, offset), 0, pPerfDataInfoList[s].sizeInBytes);
                }
            } // for each hardware stage

        } // if Map() succeeded
    } // if AllocateGpuMem() succeeded

    return result;
}

// =====================================================================================================================
// "Finishes" uploading a pipeline to GPU memory by unmapping the GPU allocation.
void PipelineUploader::End()
{
    if ((m_pGpuMemory != nullptr) && (m_pMappedPtr != nullptr))
    {
        // Sanity check to make sure we allocated the correct amount of memory for any loaded SH or context registers.
#if PAL_ENABLE_PRINTS_ASSERTS
        PAL_ASSERT(m_pCtxRegWritePtr == (m_pCtxRegWritePtrStart + (m_ctxRegisterCount * 2)));
        PAL_ASSERT(m_pShRegWritePtr  == (m_pShRegWritePtrStart  + (m_shRegisterCount  * 2)));

        m_pCtxRegWritePtrStart = nullptr;
        m_pShRegWritePtrStart  = nullptr;
#endif
        m_pCtxRegWritePtr = nullptr;
        m_pShRegWritePtr  = nullptr;
        m_pMappedPtr      = nullptr;

        m_pGpuMemory->Unmap();
    }
}

} // Pal
