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

#include "core/platform.h"
#include "core/hw/gfxip/gfx9/gfx9CmdStream.h"
#include "core/hw/gfxip/gfx9/gfx9CmdUtil.h"
#include "core/hw/gfxip/gfx9/gfx9ComputePipeline.h"
#include "core/hw/gfxip/gfx9/gfx9Device.h"
#include "palPipelineAbiProcessorImpl.h"
#include "palFile.h"

using namespace Util;

namespace Pal
{
namespace Gfx9
{

// User-data signature for an unbound compute pipeline.
const ComputePipelineSignature NullCsSignature =
{
    { 0, },                     // User-data mapping for each shader stage
    UserDataNotMapped,          // Register address for numWorkGroups
    NoUserDataSpilling,         // Spill threshold
    0,                          // User-data entry limit
    UserDataNotMapped,          // Register address for performance data buffer
};
static_assert(UserDataNotMapped == 0, "Unexpected value for indicating unmapped user-data entries!");

// Base count of SH registers which are loaded using LOAD_SH_REG_INDEX when binding to a universal command buffer.
constexpr uint32 BaseLoadedShRegCount =
    1 + // mmCOMPUTE_PGM_LO
    1 + // mmCOMPUTE_PGM_HI
    1 + // mmCOMPUTE_PGM_RSRC1
    0 + // mmCOMPUTE_PGM_RSRC2 is not included because it partially depends on bind-time state
    0 + // mmCOMPUTE_RESOURCE_LIMITS is not included because it partially depends on bind-time state
    1 + // mmCOMPUTE_NUM_THREAD_X
    1 + // mmCOMPUTE_NUM_THREAD_Y
    1 + // mmCOMPUTE_NUM_THREAD_Z
    1 + // mmCOMPUTE_USER_DATA_0 + ConstBufTblStartReg
    0;  // mmCOMPUTE_SHADER_CHKSUM is not included because it is not present on all HW

// =====================================================================================================================
ComputePipeline::ComputePipeline(
    Device* pDevice,
    bool    isInternal)  // True if this is a PAL-owned pipeline (i.e., an RPM pipeline).
    :
    Pal::ComputePipeline(pDevice->Parent(), isInternal),
    m_pDevice(pDevice)
{
    memset(&m_commands, 0, sizeof(m_commands));
    memcpy(&m_signature, &NullCsSignature, sizeof(m_signature));
}

// =====================================================================================================================
// Initializes the signature of a compute pipeline using a pipeline ELF.
void ComputePipeline::SetupSignatureFromElf(
    const CodeObjectMetadata& metadata,
    const RegisterVector&     registers)
{
    uint16  entryToRegAddr[MaxUserDataEntries] = { };

    m_signature.stage.firstUserSgprRegAddr = (mmCOMPUTE_USER_DATA_0 + FastUserDataStartReg);
    for (uint16 offset = mmCOMPUTE_USER_DATA_0; offset <= mmCOMPUTE_USER_DATA_15; ++offset)
    {
        uint32 value = 0;
        if (registers.HasEntry(offset, &value))
        {
            if (value < MaxUserDataEntries)
            {
                PAL_ASSERT(offset >= m_signature.stage.firstUserSgprRegAddr);
                const uint8 userSgprId = static_cast<uint8>(offset - m_signature.stage.firstUserSgprRegAddr);
                entryToRegAddr[value]  = offset;

                m_signature.stage.mappedEntry[userSgprId] = static_cast<uint8>(value);
                m_signature.stage.userSgprCount = Max<uint8>(userSgprId + 1, m_signature.stage.userSgprCount);
            }
            else if (value == static_cast<uint32>(Abi::UserDataMapping::GlobalTable))
            {
                PAL_ASSERT(offset == (mmCOMPUTE_USER_DATA_0 + InternalTblStartReg));
            }
            else if (value == static_cast<uint32>(Abi::UserDataMapping::PerShaderTable))
            {
                PAL_ASSERT(offset == (mmCOMPUTE_USER_DATA_0 + ConstBufTblStartReg));
            }
            else if (value == static_cast<uint32>(Abi::UserDataMapping::SpillTable))
            {
                m_signature.stage.spillTableRegAddr = static_cast<uint16>(offset);
            }
            else if (value == static_cast<uint32>(Abi::UserDataMapping::Workgroup))
            {
                m_signature.numWorkGroupsRegAddr = static_cast<uint16>(offset);
            }
            else if (value == static_cast<uint32>(Abi::UserDataMapping::GdsRange))
            {
                PAL_ASSERT(offset == (mmCOMPUTE_USER_DATA_0 + GdsRangeRegCompute));
            }
            else if (value == static_cast<uint32>(Abi::UserDataMapping::PerShaderPerfData))
            {
                m_signature.perfDataAddr = offset;
                m_perfDataInfo[static_cast<uint32>(Abi::HardwareStage::Cs)].regOffset = offset;
            }
            else if ((value == static_cast<uint32>(Abi::UserDataMapping::VertexBufferTable)) ||
                     (value == static_cast<uint32>(Abi::UserDataMapping::StreamOutTable))    ||
                     (value == static_cast<uint32>(Abi::UserDataMapping::BaseVertex))        ||
                     (value == static_cast<uint32>(Abi::UserDataMapping::BaseInstance))      ||
                     (value == static_cast<uint32>(Abi::UserDataMapping::DrawIndex))         ||
                     (value == static_cast<uint32>(Abi::UserDataMapping::BaseIndex))         ||
                     (value == static_cast<uint32>(Abi::UserDataMapping::Log2IndexSize))     ||
                     (value == static_cast<uint32>(Abi::UserDataMapping::EsGsLdsSize)))
            {
                PAL_ALERT_ALWAYS(); // These are for graphics pipelines only!
            }
            else
            {
                // This appears to be an illegally-specified user-data register!
                PAL_NEVER_CALLED();
            }
        } // If HasEntry()
    } // For each user-SGPR

#if PAL_ENABLE_PRINTS_ASSERTS
    // Indirect user-data table(s) are not supported on compute pipelines, so just assert that the table addresses
    // are unmapped.
    if (metadata.pipeline.hasEntry.indirectUserDataTableAddresses != 0)
    {
        constexpr uint32 MetadataIndirectTableAddressCount =
            (sizeof(metadata.pipeline.indirectUserDataTableAddresses) /
             sizeof(metadata.pipeline.indirectUserDataTableAddresses[0]));
        constexpr uint32 DummyAddresses[MetadataIndirectTableAddressCount] = { 0 };

        PAL_ASSERT_MSG(0 == memcmp(&metadata.pipeline.indirectUserDataTableAddresses[0],
                                   &DummyAddresses[0], sizeof(DummyAddresses)),
                       "Indirect user-data tables are not supported for Compute Pipelines!");
    }
#endif

    // NOTE: We skip the stream-out table address here because it is not used by compute pipelines.

    if (metadata.pipeline.hasEntry.spillThreshold != 0)
    {
        m_signature.spillThreshold = static_cast<uint16>(metadata.pipeline.spillThreshold);
    }

    if (metadata.pipeline.hasEntry.userDataLimit != 0)
    {
        m_signature.userDataLimit = static_cast<uint16>(metadata.pipeline.userDataLimit);
    }

}

// =====================================================================================================================
// Helper function for computing the number of SH registers to load using a LOAD_SH_REG_INDEX packet for pipeline binds.
static PAL_INLINE uint32 LoadedShRegCount(
    const GpuChipProperties& chipProps)
{
    // Add one register if the GPU supports SPP.
    uint32 count = (BaseLoadedShRegCount + chipProps.gfx9.supportSpp);

    return count;
}

// =====================================================================================================================
// Initializes HW-specific state related to this compute pipeline (register values, user-data mapping, etc.) using the
// specified Pipeline ABI processor.
Result ComputePipeline::HwlInit(
    const ComputePipelineCreateInfo& createInfo,
    const AbiProcessor&              abiProcessor,
    const CodeObjectMetadata&        metadata,
    MsgPackReader*                   pMetadataReader)
{
    const Gfx9PalSettings&   settings  = m_pDevice->Settings();
    const CmdUtil&           cmdUtil   = m_pDevice->CmdUtil();
    const auto&              regInfo   = cmdUtil.GetRegInfo();
    const GpuChipProperties& chipProps = m_pDevice->Parent()->ChipProperties();

    RegisterVector registers(m_pDevice->GetPlatform());
    Result result = pMetadataReader->Unpack(&registers);

    ComputePipelineUploader uploader(settings.enableLoadIndexForObjectBinds ? LoadedShRegCount(chipProps) : 0);
    if (result == Result::Success)
    {
        // Next, handle relocations and upload the pipeline code & data to GPU memory.
        result = PerformRelocationsAndUploadToGpuMemory(
            abiProcessor,
            metadata,
            &uploader,
#if PAL_CLIENT_INTERFACE_MAJOR_VERSION >= 488
            createInfo.flags.preferNonLocalHeap
#else
            false
#endif
        );
    }
    if (result ==  Result::Success)
    {
        BuildPm4Headers(uploader);
        UpdateRingSizes(metadata);

        // Update the pipeline signature with user-mapping data contained in the ELF:
        SetupSignatureFromElf(metadata, registers);

        Abi::PipelineSymbolEntry csProgram  = { };
        Result result = uploader.GetPipelineSymbolGpuVirtAddr(
            abiProcessor,
            Abi::PipelineSymbolType::CsMainEntry,
            &csProgram);
        if (result == Result::Success)
        {
            m_stageInfo.codeLength    = static_cast<size_t>(csProgram.size);
            const gpusize csProgramVa = csProgram.value;
            PAL_ASSERT(IsPow2Aligned(csProgramVa, 256u));

            m_commands.set.computePgmLo.bits.DATA = Get256BAddrLo(csProgramVa);
            m_commands.set.computePgmHi.bits.DATA = Get256BAddrHi(csProgramVa);
        }

        Abi::PipelineSymbolEntry csSrdTable = { };
        result = uploader.GetPipelineSymbolGpuVirtAddr(
            abiProcessor,
            Abi::PipelineSymbolType::CsShdrIntrlTblPtr,
            &csSrdTable);
        if (result == Result::Success)
        {
            const gpusize csSrdTableVa = csSrdTable.value;
            m_commands.set.computeUserDataLo.bits.DATA = LowPart(csSrdTableVa);
        }

        m_commands.set.computePgmRsrc1.u32All     = registers.At(mmCOMPUTE_PGM_RSRC1);
        m_commands.dynamic.computePgmRsrc2.u32All = registers.At(mmCOMPUTE_PGM_RSRC2);
        m_commands.set.computeNumThreadX.u32All   = registers.At(mmCOMPUTE_NUM_THREAD_X);
        m_commands.set.computeNumThreadY.u32All   = registers.At(mmCOMPUTE_NUM_THREAD_Y);
        m_commands.set.computeNumThreadZ.u32All   = registers.At(mmCOMPUTE_NUM_THREAD_Z);

        if (chipProps.gfx9.supportSpp == 1)
        {
            PAL_ASSERT(regInfo.mmComputeShaderChksum != 0);
            registers.HasEntry(regInfo.mmComputeShaderChksum, &m_commands.set.computeShaderChksum.u32All);
        }

        m_threadsPerTgX = m_commands.set.computeNumThreadX.bits.NUM_THREAD_FULL;
        m_threadsPerTgY = m_commands.set.computeNumThreadY.bits.NUM_THREAD_FULL;
        m_threadsPerTgZ = m_commands.set.computeNumThreadZ.bits.NUM_THREAD_FULL;

        if (uploader.EnableLoadIndexPath())
        {
            uploader.AddShReg(mmCOMPUTE_PGM_LO, m_commands.set.computePgmLo);
            uploader.AddShReg(mmCOMPUTE_PGM_HI, m_commands.set.computePgmHi);

            uploader.AddShReg((mmCOMPUTE_USER_DATA_0 + ConstBufTblStartReg), m_commands.set.computeUserDataLo);

            uploader.AddShReg(mmCOMPUTE_PGM_RSRC1,    m_commands.set.computePgmRsrc1);
            uploader.AddShReg(mmCOMPUTE_NUM_THREAD_X, m_commands.set.computeNumThreadX);
            uploader.AddShReg(mmCOMPUTE_NUM_THREAD_Y, m_commands.set.computeNumThreadY);
            uploader.AddShReg(mmCOMPUTE_NUM_THREAD_Z, m_commands.set.computeNumThreadZ);

            if (chipProps.gfx9.supportSpp == 1)
            {
                uploader.AddShReg(regInfo.mmComputeShaderChksum, m_commands.set.computeShaderChksum);
            }
        }
        uploader.End();

        registers.HasEntry(mmCOMPUTE_RESOURCE_LIMITS, &m_commands.dynamic.computeResourceLimits.u32All);

        const uint32 wavefrontSize   = 64;
        const uint32 threadsPerGroup = (m_threadsPerTgX * m_threadsPerTgY * m_threadsPerTgZ);
        const uint32 wavesPerGroup   = RoundUpQuotient(threadsPerGroup, wavefrontSize);

        // SIMD_DEST_CNTL: Controls which SIMDs thread groups get scheduled on.  If the number of
        // waves-per-TG is a multiple of 4, this should be 1, otherwise 0.
        m_commands.dynamic.computeResourceLimits.bits.SIMD_DEST_CNTL = ((wavesPerGroup % 4) == 0) ? 1 : 0;

        // Force even distribution on all SIMDs in CU for workgroup size is 64
        // This has shown some good improvements if #CU per SE not a multiple of 4
        if (((chipProps.gfx9.numShaderArrays * chipProps.gfx9.numCuPerSh) & 0x3) && (wavesPerGroup == 1))
        {
            m_commands.dynamic.computeResourceLimits.bits.FORCE_SIMD_DIST = 1;
        }

        if (m_pDevice->Parent()->LegacyHwsTrapHandlerPresent() && (chipProps.gfxLevel == GfxIpLevel::GfxIp9))
        {

            // If the legacy HWS's trap handler is present, compute shaders must always set the TRAP_PRESENT
            // flag.

            // TODO: Handle the case where the client enabled a trap handler and the hardware scheduler's trap handler
            // is already active!
            PAL_ASSERT(m_commands.dynamic.computePgmRsrc2.bits.TRAP_PRESENT == 0);
            m_commands.dynamic.computePgmRsrc2.bits.TRAP_PRESENT = 1;
        }

        // LOCK_THRESHOLD: Sets per-SH low threshold for locking.  Set in units of 4, 0 disables locking.
        // LOCK_THRESHOLD's maximum value: (6 bits), in units of 4, so it is max of 252.
        constexpr uint32 Gfx9MaxLockThreshold = 252;
        PAL_ASSERT(settings.csLockThreshold <= Gfx9MaxLockThreshold);
        m_commands.dynamic.computeResourceLimits.bits.LOCK_THRESHOLD = Min((settings.csLockThreshold >> 2),
                                                                           (Gfx9MaxLockThreshold >> 2));

        // SIMD_DEST_CNTL: Controls whichs SIMDs thread groups get scheduled on.  If no override is set, just keep
        // the existing value in COMPUTE_RESOURCE_LIMITS.
        switch (settings.csSimdDestCntl)
        {
        case CsSimdDestCntlForce1:
            m_commands.dynamic.computeResourceLimits.bits.SIMD_DEST_CNTL = 1;
            break;
        case CsSimdDestCntlForce0:
            m_commands.dynamic.computeResourceLimits.bits.SIMD_DEST_CNTL = 0;
            break;
        default:
            PAL_ASSERT(settings.csSimdDestCntl == CsSimdDestCntlDefault);
            break;
        }

        m_pDevice->CmdUtil().BuildPipelinePrefetchPm4(uploader, &m_commands.prefetch);

        GetFunctionGpuVirtAddrs(abiProcessor, uploader, createInfo.pIndirectFuncList, createInfo.indirectFuncCount);
    }

    return result;
}

// =====================================================================================================================
// Helper function to compute the WAVES_PER_SH field of the COMPUTE_RESOURCE_LIMITS register.
uint32 ComputePipeline::CalcMaxWavesPerSh(
    uint32 maxWavesPerCu
    ) const
{
    // The maximum number of waves per SH in "register units".
    // By default set the WAVE_LIMIT field to be unlimited.
    // Limits given by the ELF will only apply if the caller doesn't set their own limit.
    uint32 wavesPerSh = 0;

    if (maxWavesPerCu > 0)
    {
        const auto&  gfx9ChipProps        = m_pDevice->Parent()->ChipProperties().gfx9;
        const uint32 numWavefrontsPerCu   = (gfx9ChipProps.numSimdPerCu * gfx9ChipProps.numWavesPerSimd);
        const uint32 maxWavesPerShCompute = numWavefrontsPerCu * gfx9ChipProps.numCuPerSh;

        // We assume no one is trying to use more than 100% of all waves.
        PAL_ASSERT(maxWavesPerCu <= numWavefrontsPerCu);

        const uint32 maxWavesPerSh = (maxWavesPerCu * gfx9ChipProps.numCuPerSh);

        // For compute shaders, it is in units of 1 wave and must not exceed the max.
        wavesPerSh = Min(maxWavesPerShCompute, maxWavesPerSh);
    }

    return wavesPerSh;
}

// =====================================================================================================================
// Writes the PM4 commands required to bind this pipeline. Returns a pointer to the next unused DWORD in pCmdSpace.
uint32* ComputePipeline::WriteCommands(
    Pal::CmdStream*                 pCmdStream,
    uint32*                         pCmdSpace,
    const DynamicComputeShaderInfo& csInfo,
    bool                            prefetch
    ) const
{
    auto*const pGfx9CmdStream = static_cast<CmdStream*>(pCmdStream);

    // Disable the LOAD_INDEX path if the PM4 optimizer is enabled or for compute command buffers.  The optimizer cannot
    // optimize these load packets because the register values are in GPU memory.  Additionally, any client requesting
    // PM4 optimization is trading CPU cycles for GPU performance, so the savings of using LOAD_INDEX is not important.
    // This gets disabled for compute command buffers because the MEC does not support any LOAD packets.
    const bool useSetPath =
        ((m_commands.loadIndex.loadShRegIndex.header.u32All == 0) ||
         pGfx9CmdStream->Pm4OptimizerEnabled()                    ||
         (pGfx9CmdStream->GetEngineType() == EngineType::EngineTypeCompute));

    if (useSetPath)
    {
        pCmdSpace = pGfx9CmdStream->WritePm4Image(m_commands.set.spaceNeeded, &m_commands.set, pCmdSpace);
    }
    else
    {
        constexpr uint32 SpaceNeeded = sizeof(m_commands.loadIndex) / sizeof(uint32);
        pCmdSpace = pGfx9CmdStream->WritePm4Image(SpaceNeeded, &m_commands.loadIndex, pCmdSpace);
    }

    auto dynamicCmds = m_commands.dynamic;

    // TG_PER_CU: Sets the CS threadgroup limit per CU. Range is 1 to 15, 0 disables the limit.
    constexpr uint32 Gfx9MaxTgPerCu = 15;
    dynamicCmds.computeResourceLimits.bits.TG_PER_CU = Min(csInfo.maxThreadGroupsPerCu, Gfx9MaxTgPerCu);
    if (csInfo.maxWavesPerCu > 0)
    {
        dynamicCmds.computeResourceLimits.bits.WAVES_PER_SH = CalcMaxWavesPerSh(csInfo.maxWavesPerCu);
    }

    if (csInfo.ldsBytesPerTg > 0)
    {
        // Round to nearest multiple of the LDS granularity, then convert to the register value.
        // NOTE: Granularity for the LDS_SIZE field is 128, range is 0->128 which allocates 0 to 16K DWORDs.
        dynamicCmds.computePgmRsrc2.bits.LDS_SIZE =
            Pow2Align((csInfo.ldsBytesPerTg / sizeof(uint32)), Gfx9LdsDwGranularity) >> Gfx9LdsDwGranularityShift;
    }

    constexpr uint32 SpaceNeededDynamic = sizeof(dynamicCmds) / sizeof(uint32);
    pCmdSpace = pGfx9CmdStream->WritePm4Image(SpaceNeededDynamic, &dynamicCmds, pCmdSpace);

    const auto& perfData = m_perfDataInfo[static_cast<uint32>(Abi::HardwareStage::Cs)];
    if (perfData.regOffset != UserDataNotMapped)
    {
        pCmdSpace = pGfx9CmdStream->WriteSetOneShReg<ShaderCompute>(perfData.regOffset,
                                                                    perfData.gpuVirtAddr,
                                                                    pCmdSpace);
    }

    if (prefetch)
    {
        memcpy(pCmdSpace, &m_commands.prefetch, m_commands.prefetch.spaceNeeded * sizeof(uint32));
        pCmdSpace += m_commands.prefetch.spaceNeeded;
    }

    return pCmdSpace;
}

// =====================================================================================================================
// Obtains shader compilation stats.
Result ComputePipeline::GetShaderStats(
    ShaderType   shaderType,
    ShaderStats* pShaderStats,
    bool         getDisassemblySize
    ) const
{
    const GpuChipProperties& chipProps = m_pDevice->Parent()->ChipProperties();

    PAL_ASSERT(pShaderStats != nullptr);
    Result result = Result::ErrorUnavailable;

    if (shaderType == ShaderType::Compute)
    {
        result = GetShaderStatsForStage(m_stageInfo, nullptr, pShaderStats);
        if (result == Result::Success)
        {
            pShaderStats->shaderStageMask        = ApiShaderStageCompute;
            pShaderStats->palShaderHash          = m_info.shader[static_cast<uint32>(shaderType)].hash;
            pShaderStats->cs.numThreadsPerGroupX = m_threadsPerTgX;
            pShaderStats->cs.numThreadsPerGroupY = m_threadsPerTgY;
            pShaderStats->cs.numThreadsPerGroupZ = m_threadsPerTgZ;
            pShaderStats->common.gpuVirtAddress  = GetOriginalAddress(m_commands.set.computePgmLo.bits.DATA,
                                                                      m_commands.set.computePgmHi.bits.DATA);

            pShaderStats->common.ldsSizePerThreadGroup = chipProps.gfxip.ldsSizePerThreadGroup;
        }
    }

    return result;
}

// =====================================================================================================================
// Builds the packet headers for the various PM4 images associated with this pipeline.  Register values and packet
// payloads are computed elsewhere.
void ComputePipeline::BuildPm4Headers(
    const ComputePipelineUploader& uploader)
{
    const auto&    chipProps = m_pDevice->Parent()->ChipProperties();
    const CmdUtil& cmdUtil   = m_pDevice->CmdUtil();
    const auto&    regInfo   = cmdUtil.GetRegInfo();

    // PM4 image for compute command buffers:

    m_commands.set.spaceNeeded = cmdUtil.BuildSetSeqShRegs(mmCOMPUTE_NUM_THREAD_X,
                                                           mmCOMPUTE_NUM_THREAD_Z,
                                                           ShaderCompute,
                                                           &m_commands.set.hdrComputeNumThread);

    m_commands.set.spaceNeeded += cmdUtil.BuildSetSeqShRegs(mmCOMPUTE_PGM_LO,
                                                            mmCOMPUTE_PGM_HI,
                                                            ShaderCompute,
                                                            &m_commands.set.hdrComputePgm);

    m_commands.set.spaceNeeded += cmdUtil.BuildSetOneShReg(mmCOMPUTE_PGM_RSRC1,
                                                           ShaderCompute,
                                                           &m_commands.set.hdrComputePgmRsrc1);

    m_commands.set.spaceNeeded += cmdUtil.BuildSetOneShReg(mmCOMPUTE_USER_DATA_0 + ConstBufTblStartReg,
                                                           ShaderCompute,
                                                           &m_commands.set.hdrComputeUserData);
    if (chipProps.gfx9.supportSpp == 1)
    {
        m_commands.set.spaceNeeded += cmdUtil.BuildSetOneShReg(regInfo.mmComputeShaderChksum,
                                                               ShaderCompute,
                                                               &m_commands.set.hdrComputeShaderChksum);
    }
    else
    {
        m_commands.set.spaceNeeded += cmdUtil.BuildNop(CmdUtil::ShRegSizeDwords + 1,
                                                       &m_commands.set.hdrComputeShaderChksum);
    }

    // PM4 image for universal command buffers:

    if (uploader.EnableLoadIndexPath())
    {
        cmdUtil.BuildLoadShRegsIndex(uploader.ShRegGpuVirtAddr(),
                                     uploader.ShRegisterCount(),
                                     ShaderCompute,
                                     &m_commands.loadIndex.loadShRegIndex);
    }

    // PM4 image for dynamic (bind-time) state:

    cmdUtil.BuildSetOneShReg(mmCOMPUTE_PGM_RSRC2,       ShaderCompute, &m_commands.dynamic.hdrComputePgmRsrc2);
    cmdUtil.BuildSetOneShReg(mmCOMPUTE_RESOURCE_LIMITS, ShaderCompute, &m_commands.dynamic.hdrComputeResourceLimits);
}

// =====================================================================================================================
// Update the device that this compute pipeline has some new ring-size requirements.
void ComputePipeline::UpdateRingSizes(
    const CodeObjectMetadata& metadata)
{
    ShaderRingItemSizes ringSizes = { };

    const auto& csStageMetadata = metadata.pipeline.hardwareStage[static_cast<uint32>(Abi::HardwareStage::Cs)];
    if (csStageMetadata.hasEntry.scratchMemorySize != 0)
    {
        ringSizes.itemSize[static_cast<size_t>(ShaderRingType::ComputeScratch)] =
            (csStageMetadata.scratchMemorySize / sizeof(uint32));
    }

    // Inform the device that this pipeline has some new ring-size requirements.
    m_pDevice->UpdateLargestRingSizes(&ringSizes);
}

} // Gfx9
} // Pal
