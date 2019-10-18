/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2015-2019 Advanced Micro Devices, Inc. All Rights Reserved.
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
#include "core/hw/gfxip/gfx9/gfx9Device.h"
#include "core/hw/gfxip/gfx9/gfx9GraphicsPipeline.h"
#include "core/hw/gfxip/gfx9/gfx9PipelineChunkVsPs.h"
#include "palPipelineAbiProcessorImpl.h"

using namespace Util;

namespace Pal
{
namespace Gfx9
{

// Stream-out vertex stride register addresses.
constexpr uint16 VgtStrmoutVtxStrideAddr[] =
    { mmVGT_STRMOUT_VTX_STRIDE_0, mmVGT_STRMOUT_VTX_STRIDE_1, mmVGT_STRMOUT_VTX_STRIDE_2, mmVGT_STRMOUT_VTX_STRIDE_3, };

// Base count of PS SH registers which are loaded using LOAD_SH_REG_INDEX when binding to a command buffer.
static constexpr uint32 BaseLoadedShRegCountPs =
    1 + // mmSPI_SHADER_PGM_LO_PS
    1 + // mmSPI_SHADER_PGM_HI_PS
    1 + // mmSPI_SHADER_PGM_RSRC1_PS
    1 + // mmSPI_SHADER_PGM_RSRC2_PS
    0 + // SPI_SHADER_PGM_CHKSUM_PS is not included because it is not present on all HW
    1;  // mmSPI_SHADER_USER_DATA_PS_0 + ConstBufTblStartReg

// Base count of VS SH registers which are loaded using LOAD_SH_REG_INDEX when binding to a command buffer.
static constexpr uint32 BaseLoadedShRegCountVs =
    1 + // mmSPI_SHADER_PGM_LO_VS
    1 + // mmSPI_SHADER_PGM_HI_VS
    1 + // mmSPI_SHADER_PGM_RSRC1_VS
    1 + // mmSPI_SHADER_PGM_RSRC2_VS
    0 + // SPI_SHADER_PGM_CHKSUM_VS is not included because it is not present on all HW
    0 + // mmSPI_SHADER_REQ_CTRL_PS is gfx10 only
    0 + // mmSPI_SHADER_REQ_CTRL_VS is gfx10 only
    1;  // mmSPI_SHADER_USER_DATA_VS_0 + ConstBufTblStartReg

// Base count of Context registers which are loaded using LOAD_CNTX_REG_INDEX when binding to a command buffer.
static constexpr uint32 BaseLoadedCntxRegCount =
    1 + // mmSPI_SHADER_Z_FORMAT
    1 + // mmSPI_SHADER_COL_FORMAT
    1 + // mmSPI_BARYC_CNTL
    1 + // mmSPI_PS_INPUT_ENA
    1 + // mmSPI_PS_INPUT_ADDR
    1 + // mmDB_SHADER_CONTROL
    1 + // mmPA_SC_SHADER_CONTROL
    1 + // mmPA_SC_BINNER_CNTL_1
    1 + // mmSPI_SHADER_POS_FORMAT
    1 + // mmPA_CL_VS_OUT_CNTL
    1 + // mmVGT_PRIMITIVEID_EN
    0 + // mmSPI_PS_INPUT_CNTL_0...31 are not included because the number of interpolants depends on the pipeline
    1 + // mmVGT_STRMOUT_CONFIG
    0 + // mmSPI_SHADER_USER_ACCUM_PS/VS0...3 are not included because it is not present on all HW
    1;  // mmVGT_STRMOUT_BUFFER_CONFIG

// Base count of Context registers which are loaded using LOAD_CNTX_REG_INDEX when binding to a command buffer when
// stream-out is enabled for this pipeline.
static constexpr uint32 BaseLoadedCntxRegCountStreamOut =
    4;  // mmVGT_STRMOUT_VTX_STRIDE_[0...3]

// =====================================================================================================================
PipelineChunkVsPs::PipelineChunkVsPs(
    const Device&       device,
    const PerfDataInfo* pVsPerfDataInfo,
    const PerfDataInfo* pPsPerfDataInfo)
    :
    m_device(device),
    m_pVsPerfDataInfo(pVsPerfDataInfo),
    m_pPsPerfDataInfo(pPsPerfDataInfo)
{
    memset(&m_commands, 0, sizeof(m_commands));
    memset(&m_stageInfoVs, 0, sizeof(m_stageInfoVs));
    memset(&m_stageInfoPs, 0, sizeof(m_stageInfoPs));

    m_stageInfoVs.stageId = Abi::HardwareStage::Vs;
    m_stageInfoPs.stageId = Abi::HardwareStage::Ps;
}

// =====================================================================================================================
// Early initialization for this pipeline chunk.  Responsible for determining the number of SH and context registers to
// be loaded using LOAD_CNTX_REG_INDEX and LOAD_SH_REG_INDEX.
void PipelineChunkVsPs::EarlyInit(
    const RegisterVector&     registers,
    GraphicsPipelineLoadInfo* pInfo)
{
    PAL_ASSERT(pInfo != nullptr);

    const Gfx9PalSettings&   settings  = m_device.Settings();
    const GpuChipProperties& chipProps = m_device.Parent()->ChipProperties();

    // Determine if stream-out is enabled for this pipeline.
    registers.HasEntry(mmVGT_STRMOUT_CONFIG, &m_commands.streamOut.vgtStrmoutConfig.u32All);

    // Determine the number of PS interpolators and save them for LateInit to consume.
    pInfo->interpolatorCount = 0;
    for (uint32 i = 0; i < MaxPsInputSemantics; ++i)
    {
        const uint16 offset = static_cast<uint16>(mmSPI_PS_INPUT_CNTL_0 + i);
        if (registers.HasEntry(offset, &m_commands.context.spiPsInputCntl[i].u32All) == false)
        {
            break;
        }

        ++(pInfo->interpolatorCount);
    }

    if (settings.enableLoadIndexForObjectBinds != false)
    {
        pInfo->loadedCtxRegCount += (BaseLoadedCntxRegCount + pInfo->interpolatorCount);
        pInfo->loadedShRegCount  += (BaseLoadedShRegCountPs + ((chipProps.gfx9.supportSpp == 1) ? 1 : 0));

        if (pInfo->enableNgg == false)
        {
            pInfo->loadedShRegCount += (BaseLoadedShRegCountVs + ((chipProps.gfx9.supportSpp == 1) ? 1 : 0));
        }

        if (VgtStrmoutConfig().u32All != 0)
        {
            pInfo->loadedCtxRegCount += BaseLoadedCntxRegCountStreamOut;
        }

        if (IsGfx10(chipProps.gfxLevel))
        {
            // mmSPI_SHADER_REQ_CTRL_PS & mmSPI_SHADE_REQ_CTRL_VS
            pInfo->loadedShRegCount += (pInfo->enableNgg ? 1 : 2);
        }

        if (chipProps.gfx9.supportSpiPrefPriority)
        {
            if (pInfo->enableNgg == false)
            {
                // mmSPI_SHADER_USER_ACCUM_VS_0...3
                pInfo->loadedShRegCount += 4;
            }
            // mmSPI_SHADER_USER_ACCUM_PS_0...3
            pInfo->loadedShRegCount += 4;
        }
    }
}

// =====================================================================================================================
// Late initialization for this pipeline chunk.  Responsible for fetching register values from the pipeline binary and
// determining the values of other registers.  Also uploads register state into GPU memory.
void PipelineChunkVsPs::LateInit(
    const AbiProcessor&                 abiProcessor,
    const CodeObjectMetadata&           metadata,
    const RegisterVector&               registers,
    const GraphicsPipelineLoadInfo&     loadInfo,
    const GraphicsPipelineCreateInfo&   createInfo,
    GraphicsPipelineUploader*           pUploader,
    MetroHash64*                        pHasher)
{
    const bool useLoadIndexPath = pUploader->EnableLoadIndexPath();

    const Gfx9PalSettings&   settings  = m_device.Settings();
    const GpuChipProperties& chipProps = m_device.Parent()->ChipProperties();

    BuildPm4Headers(useLoadIndexPath, loadInfo);

    Abi::PipelineSymbolEntry symbol = { };
    if (abiProcessor.HasPipelineSymbolEntry(Abi::PipelineSymbolType::PsMainEntry, &symbol))
    {
        m_stageInfoPs.codeLength   = static_cast<size_t>(symbol.size);
        const gpusize programGpuVa = (pUploader->CodeGpuVirtAddr() + symbol.value);
        PAL_ASSERT(programGpuVa == Pow2Align(programGpuVa, 256));

        m_commands.sh.ps.spiShaderPgmLoPs.bits.MEM_BASE = Get256BAddrLo(programGpuVa);
        m_commands.sh.ps.spiShaderPgmHiPs.bits.MEM_BASE = Get256BAddrHi(programGpuVa);
    }

    if (abiProcessor.HasPipelineSymbolEntry(Abi::PipelineSymbolType::PsShdrIntrlTblPtr, &symbol))
    {
        const gpusize srdTableGpuVa = (pUploader->DataGpuVirtAddr() + symbol.value);
        m_commands.sh.ps.spiShaderUserDataLoPs.bits.DATA = LowPart(srdTableGpuVa);
    }

    if (abiProcessor.HasPipelineSymbolEntry(Abi::PipelineSymbolType::PsDisassembly, &symbol))
    {
        m_stageInfoPs.disassemblyLength = static_cast<size_t>(symbol.size);
    }

    m_commands.sh.ps.spiShaderPgmRsrc1Ps.u32All = registers.At(mmSPI_SHADER_PGM_RSRC1_PS);
    m_commands.sh.ps.spiShaderPgmRsrc2Ps.u32All = registers.At(mmSPI_SHADER_PGM_RSRC2_PS);
    registers.HasEntry(mmSPI_SHADER_PGM_RSRC3_PS, &m_commands.dynamic.ps.spiShaderPgmRsrc3Ps.u32All);

    // NOTE: The Pipeline ABI doesn't specify CU_GROUP_DISABLE for various shader stages, so it should be safe to
    // always use the setting PAL prefers.
    m_commands.sh.ps.spiShaderPgmRsrc1Ps.bits.CU_GROUP_DISABLE = (settings.numPsWavesSoftGroupedPerCu > 0 ? 0 : 1);

    if (chipProps.gfx9.supportSpp != 0)
    {
        registers.HasEntry(Apu09_1xPlus::mmSPI_SHADER_PGM_CHKSUM_PS, &m_commands.sh.ps.spiShaderPgmChksumPs.u32All);
    }

    m_commands.dynamic.ps.spiShaderPgmRsrc3Ps.bits.CU_EN = m_device.GetCuEnableMask(0, settings.psCuEnLimitMask);

    if (IsGfx10(chipProps.gfxLevel))
    {
        m_commands.dynamic.ps.spiShaderPgmRsrc4Ps.bits.CU_EN = m_device.GetCuEnableMaskHi(0, settings.psCuEnLimitMask);

        if (chipProps.gfx9.supportSpiPrefPriority)
        {
            registers.HasEntry(Gfx10::mmSPI_SHADER_USER_ACCUM_PS_0, &m_commands.sh.ps.shaderUserAccumPs0.u32All);
            registers.HasEntry(Gfx10::mmSPI_SHADER_USER_ACCUM_PS_1, &m_commands.sh.ps.shaderUserAccumPs1.u32All);
            registers.HasEntry(Gfx10::mmSPI_SHADER_USER_ACCUM_PS_2, &m_commands.sh.ps.shaderUserAccumPs2.u32All);
            registers.HasEntry(Gfx10::mmSPI_SHADER_USER_ACCUM_PS_3, &m_commands.sh.ps.shaderUserAccumPs3.u32All);
            if (loadInfo.enableNgg == false)
            {
                registers.HasEntry(Gfx10::mmSPI_SHADER_USER_ACCUM_VS_0, &m_commands.sh.vs.shaderUserAccumVs0.u32All);
                registers.HasEntry(Gfx10::mmSPI_SHADER_USER_ACCUM_VS_1, &m_commands.sh.vs.shaderUserAccumVs1.u32All);
                registers.HasEntry(Gfx10::mmSPI_SHADER_USER_ACCUM_VS_2, &m_commands.sh.vs.shaderUserAccumVs2.u32All);
                registers.HasEntry(Gfx10::mmSPI_SHADER_USER_ACCUM_VS_3, &m_commands.sh.vs.shaderUserAccumVs3.u32All);
            }
        }

        if (settings.numPsWavesSoftGroupedPerCu > 0)
        {
            m_commands.sh.ps.shaderReqCtrlPs.bits.SOFT_GROUPING_EN = 1;
            m_commands.sh.ps.shaderReqCtrlPs.bits.NUMBER_OF_REQUESTS_PER_CU = settings.numPsWavesSoftGroupedPerCu - 1;
        }

        if (settings.numVsWavesSoftGroupedPerCu > 0)
        {
            m_commands.sh.vs.shaderReqCtrlVs.bits.SOFT_GROUPING_EN = 1;
            m_commands.sh.vs.shaderReqCtrlVs.bits.NUMBER_OF_REQUESTS_PER_CU = settings.numVsWavesSoftGroupedPerCu - 1;
        }
    }

    if (loadInfo.enableNgg == false)
    {
        if (abiProcessor.HasPipelineSymbolEntry(Abi::PipelineSymbolType::VsMainEntry, &symbol))
        {
            m_stageInfoVs.codeLength   = static_cast<size_t>(symbol.size);
            const gpusize programGpuVa = (pUploader->CodeGpuVirtAddr() + symbol.value);
            PAL_ASSERT(programGpuVa == Pow2Align(programGpuVa, 256));

            m_commands.sh.vs.spiShaderPgmLoVs.bits.MEM_BASE = Get256BAddrLo(programGpuVa);
            m_commands.sh.vs.spiShaderPgmHiVs.bits.MEM_BASE = Get256BAddrHi(programGpuVa);
        }

        if (abiProcessor.HasPipelineSymbolEntry(Abi::PipelineSymbolType::VsShdrIntrlTblPtr, &symbol))
        {
            const gpusize srdTableGpuVa = (pUploader->DataGpuVirtAddr() + symbol.value);
            m_commands.sh.vs.spiShaderUserDataLoVs.bits.DATA = LowPart(srdTableGpuVa);
        }

        if (abiProcessor.HasPipelineSymbolEntry(Abi::PipelineSymbolType::VsDisassembly, &symbol))
        {
            m_stageInfoVs.disassemblyLength = static_cast<size_t>(symbol.size);
        }

        m_commands.sh.vs.spiShaderPgmRsrc1Vs.u32All = registers.At(mmSPI_SHADER_PGM_RSRC1_VS);
        m_commands.sh.vs.spiShaderPgmRsrc2Vs.u32All = registers.At(mmSPI_SHADER_PGM_RSRC2_VS);
        registers.HasEntry(mmSPI_SHADER_PGM_RSRC3_VS, &m_commands.dynamic.vs.spiShaderPgmRsrc3Vs.u32All);

        // NOTE: The Pipeline ABI doesn't specify CU_GROUP_ENABLE for various shader stages, so it should be safe to
        // always use the setting PAL prefers.
        m_commands.sh.vs.spiShaderPgmRsrc1Vs.bits.CU_GROUP_ENABLE = (settings.numVsWavesSoftGroupedPerCu > 0 ? 1 : 0);

        if (chipProps.gfx9.supportSpp != 0)
        {
            registers.HasEntry(Apu09_1xPlus::mmSPI_SHADER_PGM_CHKSUM_VS, &m_commands.sh.vs.spiShaderPgmChksumVs.u32All);
        }

        uint16 vsCuDisableMask = 0;
        if (IsGfx10(chipProps.gfxLevel))
        {
            // Both CU's of a WGP need to be disabled for better performance.
            vsCuDisableMask = 0xC;
        }
        else
        {
            // Disable virtualized CU #1 instead of #0 because thread traces use CU #0 by default.
            vsCuDisableMask = 0x2;
        }

        // NOTE: The Pipeline ABI doesn't specify CU enable masks for each shader stage, so it should be safe to
        // always use the ones PAL prefers.
        m_commands.dynamic.vs.spiShaderPgmRsrc3Vs.bits.CU_EN =
                    m_device.GetCuEnableMask(vsCuDisableMask, settings.vsCuEnLimitMask);
        if (IsGfx10(chipProps.gfxLevel))
        {
            const uint16 vsCuDisableMaskHi = 0;
            m_commands.dynamic.vs.spiShaderPgmRsrc4Vs.bits.CU_EN =
                    m_device.GetCuEnableMaskHi(vsCuDisableMaskHi, settings.vsCuEnLimitMask);
        }
    } // if enableNgg == false

    if (VgtStrmoutConfig().u32All != 0)
    {
        m_commands.streamOut.vgtStrmoutBufferConfig.u32All = registers.At(mmVGT_STRMOUT_BUFFER_CONFIG);

        for (uint32 i = 0; i < MaxStreamOutTargets; ++i)
        {
            m_commands.streamOut.stride[i].vgtStrmoutVtxStride.u32All = registers.At(VgtStrmoutVtxStrideAddr[i]);
        }
    }

    m_commands.context.dbShaderControl.u32All    = registers.At(mmDB_SHADER_CONTROL);
    m_commands.context.spiBarycCntl.u32All       = registers.At(mmSPI_BARYC_CNTL);
    m_commands.context.spiPsInputAddr.u32All     = registers.At(mmSPI_PS_INPUT_ADDR);
    m_commands.context.spiPsInputEna.u32All      = registers.At(mmSPI_PS_INPUT_ENA);
    m_commands.context.spiShaderColFormat.u32All = registers.At(mmSPI_SHADER_COL_FORMAT);
    m_commands.context.spiShaderZFormat.u32All   = registers.At(mmSPI_SHADER_Z_FORMAT);
    m_commands.context.paClVsOutCntl.u32All      = registers.At(mmPA_CL_VS_OUT_CNTL);

#if PAL_CLIENT_INTERFACE_MAJOR_VERSION >= 524
    if (createInfo.rsState.clipDistMask != 0)
    {
        m_commands.context.paClVsOutCntl.bitfields.CLIP_DIST_ENA_0 &= (createInfo.rsState.clipDistMask & 0x1) != 0;
        m_commands.context.paClVsOutCntl.bitfields.CLIP_DIST_ENA_1 &= (createInfo.rsState.clipDistMask & 0x2) != 0;
        m_commands.context.paClVsOutCntl.bitfields.CLIP_DIST_ENA_2 &= (createInfo.rsState.clipDistMask & 0x4) != 0;
        m_commands.context.paClVsOutCntl.bitfields.CLIP_DIST_ENA_3 &= (createInfo.rsState.clipDistMask & 0x8) != 0;
        m_commands.context.paClVsOutCntl.bitfields.CLIP_DIST_ENA_4 &= (createInfo.rsState.clipDistMask & 0x10) != 0;
        m_commands.context.paClVsOutCntl.bitfields.CLIP_DIST_ENA_5 &= (createInfo.rsState.clipDistMask & 0x20) != 0;
        m_commands.context.paClVsOutCntl.bitfields.CLIP_DIST_ENA_6 &= (createInfo.rsState.clipDistMask & 0x40) != 0;
        m_commands.context.paClVsOutCntl.bitfields.CLIP_DIST_ENA_7 &= (createInfo.rsState.clipDistMask & 0x80) != 0;
    }
#endif

    m_commands.context.spiShaderPosFormat.u32All = registers.At(mmSPI_SHADER_POS_FORMAT);
    m_commands.context.vgtPrimitiveIdEn.u32All   = registers.At(mmVGT_PRIMITIVEID_EN);
    m_commands.context.paScShaderControl.u32All  = registers.At(mmPA_SC_SHADER_CONTROL);

    m_commands.common.paScAaConfig.reg_data      = registers.At(mmPA_SC_AA_CONFIG);

    if (chipProps.gfx9.supportCustomWaveBreakSize && (settings.forceWaveBreakSize != Gfx10ForceWaveBreakSizeClient))
    {
        // Override whatever wave-break size was specified by the pipeline binary if the panel is forcing a
        // value for the preferred wave-break size.
        m_commands.context.paScShaderControl.gfx10.WAVE_BREAK_REGION_SIZE =
            static_cast<uint32>(settings.forceWaveBreakSize);
    }

    // Binner_cntl1:
    // 16 bits: Maximum amount of parameter storage allowed per batch.
    // - Legacy: param cache lines/2 (groups of 16 vert-attributes) (0 means 1 encoding)
    // - NGG: number of vert-attributes (0 means 1 encoding)
    // - NGG + PC: param cache lines/2 (groups of 16 vert-attributes) (0 means 1 encoding)
    // 16 bits: Max number of primitives in batch
    m_commands.context.paScBinnerCntl1.u32All = 0;
    m_commands.context.paScBinnerCntl1.bits.MAX_PRIM_PER_BATCH = settings.binningMaxPrimPerBatch - 1;

    if (loadInfo.enableNgg)
    {
        m_commands.context.paScBinnerCntl1.bits.MAX_ALLOC_COUNT = settings.binningMaxAllocCountNggOnChip - 1;
    }
    else
    {
        m_commands.context.paScBinnerCntl1.bits.MAX_ALLOC_COUNT = settings.binningMaxAllocCountLegacy - 1;
    }

    pHasher->Update(m_commands.context);
    pHasher->Update(m_commands.common);
    pHasher->Update(m_commands.streamOut);

    if (useLoadIndexPath)
    {
        pUploader->AddShReg(mmSPI_SHADER_PGM_LO_PS,    m_commands.sh.ps.spiShaderPgmLoPs);
        pUploader->AddShReg(mmSPI_SHADER_PGM_HI_PS,    m_commands.sh.ps.spiShaderPgmHiPs);
        pUploader->AddShReg(mmSPI_SHADER_PGM_RSRC1_PS, m_commands.sh.ps.spiShaderPgmRsrc1Ps);
        pUploader->AddShReg(mmSPI_SHADER_PGM_RSRC2_PS, m_commands.sh.ps.spiShaderPgmRsrc2Ps);

        pUploader->AddShReg(mmSPI_SHADER_USER_DATA_PS_0 + ConstBufTblStartReg,
                            m_commands.sh.ps.spiShaderUserDataLoPs);

        if (chipProps.gfx9.supportSpp != 0)
        {
            pUploader->AddShReg(Apu09_1xPlus::mmSPI_SHADER_PGM_CHKSUM_PS, m_commands.sh.ps.spiShaderPgmChksumPs);
        }

        if (IsGfx10(chipProps.gfxLevel))
        {
            pUploader->AddShReg(Gfx10::mmSPI_SHADER_REQ_CTRL_PS, m_commands.sh.ps.shaderReqCtrlPs);
        }

        if (chipProps.gfx9.supportSpiPrefPriority)
        {
            pUploader->AddShReg(Gfx10::mmSPI_SHADER_USER_ACCUM_PS_0, m_commands.sh.ps.shaderUserAccumPs0);
            pUploader->AddShReg(Gfx10::mmSPI_SHADER_USER_ACCUM_PS_1, m_commands.sh.ps.shaderUserAccumPs1);
            pUploader->AddShReg(Gfx10::mmSPI_SHADER_USER_ACCUM_PS_2, m_commands.sh.ps.shaderUserAccumPs2);
            pUploader->AddShReg(Gfx10::mmSPI_SHADER_USER_ACCUM_PS_3, m_commands.sh.ps.shaderUserAccumPs3);
            if (loadInfo.enableNgg == false)
            {
                pUploader->AddShReg(Gfx10::mmSPI_SHADER_USER_ACCUM_VS_0, m_commands.sh.vs.shaderUserAccumVs0);
                pUploader->AddShReg(Gfx10::mmSPI_SHADER_USER_ACCUM_VS_1, m_commands.sh.vs.shaderUserAccumVs1);
                pUploader->AddShReg(Gfx10::mmSPI_SHADER_USER_ACCUM_VS_2, m_commands.sh.vs.shaderUserAccumVs2);
                pUploader->AddShReg(Gfx10::mmSPI_SHADER_USER_ACCUM_VS_3, m_commands.sh.vs.shaderUserAccumVs3);
            }
        }
        if (loadInfo.enableNgg == false)
        {
            pUploader->AddShReg(mmSPI_SHADER_PGM_LO_VS,    m_commands.sh.vs.spiShaderPgmLoVs);
            pUploader->AddShReg(mmSPI_SHADER_PGM_HI_VS,    m_commands.sh.vs.spiShaderPgmHiVs);
            pUploader->AddShReg(mmSPI_SHADER_PGM_RSRC1_VS, m_commands.sh.vs.spiShaderPgmRsrc1Vs);
            pUploader->AddShReg(mmSPI_SHADER_PGM_RSRC2_VS, m_commands.sh.vs.spiShaderPgmRsrc2Vs);

            pUploader->AddShReg(mmSPI_SHADER_USER_DATA_VS_0 + ConstBufTblStartReg,
                                m_commands.sh.vs.spiShaderUserDataLoVs);

            if (chipProps.gfx9.supportSpp != 0)
            {
                pUploader->AddShReg(Apu09_1xPlus::mmSPI_SHADER_PGM_CHKSUM_VS, m_commands.sh.vs.spiShaderPgmChksumVs);
            }

            if (IsGfx10(chipProps.gfxLevel))
            {
                pUploader->AddShReg(Gfx10::mmSPI_SHADER_REQ_CTRL_VS, m_commands.sh.vs.shaderReqCtrlVs);
            }
        } // if enableNgg == false

        pUploader->AddCtxReg(mmDB_SHADER_CONTROL,         m_commands.context.dbShaderControl);
        pUploader->AddCtxReg(mmSPI_BARYC_CNTL,            m_commands.context.spiBarycCntl);
        pUploader->AddCtxReg(mmSPI_PS_INPUT_ADDR,         m_commands.context.spiPsInputAddr);
        pUploader->AddCtxReg(mmSPI_PS_INPUT_ENA,          m_commands.context.spiPsInputEna);
        pUploader->AddCtxReg(mmSPI_SHADER_COL_FORMAT,     m_commands.context.spiShaderColFormat);
        pUploader->AddCtxReg(mmSPI_SHADER_Z_FORMAT,       m_commands.context.spiShaderZFormat);;
        pUploader->AddCtxReg(mmSPI_SHADER_POS_FORMAT,     m_commands.context.spiShaderPosFormat);
        pUploader->AddCtxReg(mmPA_CL_VS_OUT_CNTL,         m_commands.context.paClVsOutCntl);
        pUploader->AddCtxReg(mmVGT_PRIMITIVEID_EN,        m_commands.context.vgtPrimitiveIdEn);
        pUploader->AddCtxReg(mmPA_SC_SHADER_CONTROL,      m_commands.context.paScShaderControl);
        pUploader->AddCtxReg(mmPA_SC_BINNER_CNTL_1,       m_commands.context.paScBinnerCntl1);
        pUploader->AddCtxReg(mmVGT_STRMOUT_CONFIG,        m_commands.streamOut.vgtStrmoutConfig);
        pUploader->AddCtxReg(mmVGT_STRMOUT_BUFFER_CONFIG, m_commands.streamOut.vgtStrmoutBufferConfig);

        for (uint16 i = 0; i < loadInfo.interpolatorCount; ++i)
        {
            pUploader->AddCtxReg(mmSPI_PS_INPUT_CNTL_0 + i, m_commands.context.spiPsInputCntl[i]);
        }

        if (VgtStrmoutConfig().u32All != 0)
        {
            for (uint32 i = 0; i < MaxStreamOutTargets; ++i)
            {
                pUploader->AddCtxReg(VgtStrmoutVtxStrideAddr[i], m_commands.streamOut.stride[i].vgtStrmoutVtxStride);
            }
        }
    }
}

// =====================================================================================================================
// Copies this pipeline chunk's sh commands into the specified command space. Returns the next unused DWORD in
// pCmdSpace.
template <bool UseLoadIndexPath>
uint32* PipelineChunkVsPs::WriteShCommands(
    CmdStream*              pCmdStream,
    uint32*                 pCmdSpace,
    bool                    isNgg,
    const DynamicStageInfo& vsStageInfo,
    const DynamicStageInfo& psStageInfo
    ) const
{
    auto dynamicCmdsPs = m_commands.dynamic.ps;

    if (psStageInfo.wavesPerSh > 0)
    {
        dynamicCmdsPs.spiShaderPgmRsrc3Ps.bits.WAVE_LIMIT = psStageInfo.wavesPerSh;
    }

    if (psStageInfo.cuEnableMask != 0)
    {
        dynamicCmdsPs.spiShaderPgmRsrc3Ps.bits.CU_EN &= psStageInfo.cuEnableMask;
        if (dynamicCmdsPs.hdrPgmRsrc4Ps.header.u32All != 0)
        {
            dynamicCmdsPs.spiShaderPgmRsrc4Ps.bits.CU_EN =
                Device::AdjustCuEnHi(dynamicCmdsPs.spiShaderPgmRsrc4Ps.bits.CU_EN, psStageInfo.cuEnableMask);
        }
    }

    if (isNgg == false)
    {
        auto dynamicCmdsVs = m_commands.dynamic.vs;

        if (vsStageInfo.wavesPerSh != 0)
        {
            dynamicCmdsVs.spiShaderPgmRsrc3Vs.bits.WAVE_LIMIT = vsStageInfo.wavesPerSh;
        }

        if (vsStageInfo.cuEnableMask != 0)
        {
            dynamicCmdsVs.spiShaderPgmRsrc3Vs.bits.CU_EN &= vsStageInfo.cuEnableMask;
            if (dynamicCmdsVs.hdrPgmRsrc4Vs.header.u32All != 0)
            {
                dynamicCmdsVs.spiShaderPgmRsrc4Vs.bits.CU_EN =
                    Device::AdjustCuEnHi(dynamicCmdsVs.spiShaderPgmRsrc4Vs.bits.CU_EN, vsStageInfo.cuEnableMask);
            }
        }

        if (UseLoadIndexPath == false)
        {
            pCmdSpace = pCmdStream->WritePm4Image(m_commands.sh.vs.spaceNeeded, &m_commands.sh.vs, pCmdSpace);
        }

        PAL_ASSERT(dynamicCmdsVs.spaceNeeded != 0);
        pCmdSpace = pCmdStream->WritePm4Image(dynamicCmdsVs.spaceNeeded, &dynamicCmdsVs, pCmdSpace);

        if (m_pVsPerfDataInfo->regOffset != UserDataNotMapped)
        {
            pCmdSpace = pCmdStream->WriteSetOneShReg<ShaderGraphics>(m_pVsPerfDataInfo->regOffset,
                                                                     m_pVsPerfDataInfo->gpuVirtAddr,
                                                                     pCmdSpace);
        }
    } // if isNgg == false

    if (UseLoadIndexPath == false)
    {
        pCmdSpace = pCmdStream->WritePm4Image(m_commands.sh.ps.spaceNeeded, &m_commands.sh.ps, pCmdSpace);
    }

    PAL_ASSERT(dynamicCmdsPs.spaceNeeded != 0);
    pCmdSpace = pCmdStream->WritePm4Image(dynamicCmdsPs.spaceNeeded, &dynamicCmdsPs, pCmdSpace);

    if (m_pPsPerfDataInfo->regOffset != UserDataNotMapped)
    {
        pCmdSpace = pCmdStream->WriteSetOneShReg<ShaderGraphics>(m_pPsPerfDataInfo->regOffset,
                                                                 m_pPsPerfDataInfo->gpuVirtAddr,
                                                                 pCmdSpace);
    }

    return pCmdSpace;
}

// Instantiate template versions for the linker.
template
uint32* PipelineChunkVsPs::WriteShCommands<false>(
    CmdStream*              pCmdStream,
    uint32*                 pCmdSpace,
    bool                    isNgg,
    const DynamicStageInfo& vsStageInfo,
    const DynamicStageInfo& psStageInfo
    ) const;
template
uint32* PipelineChunkVsPs::WriteShCommands<true>(
    CmdStream*              pCmdStream,
    uint32*                 pCmdSpace,
    bool                    isNgg,
    const DynamicStageInfo& vsStageInfo,
    const DynamicStageInfo& psStageInfo
    ) const;

// =====================================================================================================================
// Copies this pipeline chunk's context commands into the specified command space. Returns the next unused DWORD in
// pCmdSpace.
template <bool UseLoadIndexPath>
uint32* PipelineChunkVsPs::WriteContextCommands(
    CmdStream* pCmdStream,
    uint32*    pCmdSpace
    ) const
{
    if (UseLoadIndexPath == false)
    {
        PAL_ASSERT(m_commands.streamOut.spaceNeeded != 0);
        pCmdSpace = pCmdStream->WritePm4Image(m_commands.streamOut.spaceNeeded, &m_commands.streamOut, pCmdSpace);
        pCmdSpace = pCmdStream->WritePm4Image(m_commands.context.spaceNeeded, &m_commands.context, pCmdSpace);
    }

    constexpr uint32 SpaceNeededCommon = sizeof(m_commands.common) / sizeof(uint32);
    return pCmdStream->WritePm4Image(SpaceNeededCommon, &m_commands.common, pCmdSpace);
}

// Instantiate template versions for the linker.
template
uint32* PipelineChunkVsPs::WriteContextCommands<false>(
    CmdStream* pCmdStream,
    uint32*    pCmdSpace
    ) const;
template
uint32* PipelineChunkVsPs::WriteContextCommands<true>(
    CmdStream* pCmdStream,
    uint32*    pCmdSpace
    ) const;

// =====================================================================================================================
// Assembles the PM4 headers for the commands in this pipeline chunk.
void PipelineChunkVsPs::BuildPm4Headers(
    bool                            enableLoadIndexPath,
    const GraphicsPipelineLoadInfo& loadInfo)
{
    const GpuChipProperties& chipProps = m_device.Parent()->ChipProperties();
    const CmdUtil&           cmdUtil   = m_device.CmdUtil();

    m_commands.sh.ps.spaceNeeded = cmdUtil.BuildSetSeqShRegs(mmSPI_SHADER_PGM_LO_PS,
                                                             mmSPI_SHADER_PGM_RSRC2_PS,
                                                             ShaderGraphics,
                                                             &m_commands.sh.ps.hdrSpiShaderPgm);

    m_commands.sh.ps.spaceNeeded += cmdUtil.BuildSetOneShReg(mmSPI_SHADER_USER_DATA_PS_0 + ConstBufTblStartReg,
                                                             ShaderGraphics,
                                                             &m_commands.sh.ps.hdrSpiShaderUserData);

    if (chipProps.gfx9.supportSpp != 0)
    {
        m_commands.sh.ps.spaceNeeded += cmdUtil.BuildSetOneShReg(Apu09_1xPlus::mmSPI_SHADER_PGM_CHKSUM_PS,
                                                                 ShaderGraphics,
                                                                 &m_commands.sh.ps.hdrSpiShaderPgmChksum);
    }
    else
    {
        m_commands.sh.ps.spaceNeeded += cmdUtil.BuildNop(CmdUtil::ShRegSizeDwords + 1,
                                                         &m_commands.sh.ps.hdrSpiShaderPgmChksum);
    }

    if (loadInfo.enableNgg == false)
    {
        m_commands.sh.vs.spaceNeeded += cmdUtil.BuildSetSeqShRegs(mmSPI_SHADER_PGM_LO_VS,
                                                                  mmSPI_SHADER_PGM_RSRC2_VS,
                                                                  ShaderGraphics,
                                                                  &m_commands.sh.vs.hdrSpiShaderPgm);

        m_commands.sh.vs.spaceNeeded += cmdUtil.BuildSetOneShReg(mmSPI_SHADER_USER_DATA_VS_0 + ConstBufTblStartReg,
                                                                 ShaderGraphics,
                                                                 &m_commands.sh.vs.hdrSpiShaderUserData);
    }
    else
    {
        const uint32 shaderPgmCnt = mmSPI_SHADER_PGM_RSRC2_VS - mmSPI_SHADER_PGM_LO_VS + 1;
        m_commands.sh.vs.spaceNeeded += cmdUtil.BuildNop(CmdUtil::ShRegSizeDwords + shaderPgmCnt,
                                                         &m_commands.sh.vs.hdrSpiShaderPgm);

        m_commands.sh.vs.spaceNeeded += cmdUtil.BuildNop(CmdUtil::ShRegSizeDwords + 1,
                                                         &m_commands.sh.vs.hdrSpiShaderUserData);
    }

    if ((loadInfo.enableNgg == false) && (chipProps.gfx9.supportSpp != 0))
    {
        m_commands.sh.vs.spaceNeeded += cmdUtil.BuildSetOneShReg(Apu09_1xPlus::mmSPI_SHADER_PGM_CHKSUM_VS,
                                                                 ShaderGraphics,
                                                                 &m_commands.sh.vs.hdrSpiShaderPgmChksum);
    }
    else
    {
        m_commands.sh.vs.spaceNeeded += cmdUtil.BuildNop(CmdUtil::ShRegSizeDwords + 1,
                                                         &m_commands.sh.vs.hdrSpiShaderPgmChksum);
    }
    if ((loadInfo.enableNgg == false) && IsGfx10(chipProps.gfxLevel))
    {
        m_commands.sh.vs.spaceNeeded += cmdUtil.BuildSetOneShReg(Gfx10::mmSPI_SHADER_REQ_CTRL_VS,
                                                                 ShaderGraphics,
                                                                 &m_commands.sh.vs.hdrShaderReqCtrlVs);
    }
    else
    {
        m_commands.sh.vs.spaceNeeded += cmdUtil.BuildNop(CmdUtil::ShRegSizeDwords + 1,
                                                         &m_commands.sh.vs.hdrShaderReqCtrlVs);
    }

    m_commands.context.spaceNeeded = cmdUtil.BuildSetSeqContextRegs(mmSPI_SHADER_POS_FORMAT,
                                                                    mmSPI_SHADER_COL_FORMAT,
                                                                    &m_commands.context.hdrSpiShaderFormat);

    m_commands.context.spaceNeeded += cmdUtil.BuildSetOneContextReg(mmSPI_BARYC_CNTL,
                                                                    &m_commands.context.hdrSpiBarycCntl);

    m_commands.context.spaceNeeded += cmdUtil.BuildSetSeqContextRegs(mmSPI_PS_INPUT_ENA,
                                                                     mmSPI_PS_INPUT_ADDR,
                                                                     &m_commands.context.hdrSpiPsInput);

    m_commands.context.spaceNeeded += cmdUtil.BuildSetOneContextReg(mmDB_SHADER_CONTROL,
                                                                    &m_commands.context.hdrDbShaderControl);

    m_commands.context.spaceNeeded += cmdUtil.BuildSetOneContextReg(mmPA_SC_SHADER_CONTROL,
                                                                    &m_commands.context.hdrPaScShaderControl);

    m_commands.context.spaceNeeded += cmdUtil.BuildSetOneContextReg(mmPA_SC_BINNER_CNTL_1,
                                                                    &m_commands.context.hdrPaScBinnerCntl1);

    m_commands.context.spaceNeeded += cmdUtil.BuildSetOneContextReg(mmPA_CL_VS_OUT_CNTL,
                                                                    &m_commands.context.hdrPaClVsOutCntl);

    m_commands.context.spaceNeeded += cmdUtil.BuildSetOneContextReg(mmVGT_PRIMITIVEID_EN,
                                                                    &m_commands.context.hdrVgtPrimitiveIdEn);

    if (loadInfo.interpolatorCount > 0)
    {
        PAL_ASSERT(loadInfo.interpolatorCount <= MaxPsInputSemantics);
        m_commands.context.spaceNeeded +=
            cmdUtil.BuildSetSeqContextRegs(mmSPI_PS_INPUT_CNTL_0,
                                           (mmSPI_PS_INPUT_CNTL_0 + loadInfo.interpolatorCount - 1),
                                           &m_commands.context.hdrSpiPsInputCntl);
    }

    m_commands.streamOut.spaceNeeded = cmdUtil.BuildSetSeqContextRegs(mmVGT_STRMOUT_CONFIG,
                                                                      mmVGT_STRMOUT_BUFFER_CONFIG,
                                                                      &m_commands.streamOut.headerStrmoutCfg);

    if (VgtStrmoutConfig().u32All != 0)
    {
        for (uint32 i = 0; i < MaxStreamOutTargets; ++i)
        {
            m_commands.streamOut.spaceNeeded +=
                cmdUtil.BuildSetOneContextReg(VgtStrmoutVtxStrideAddr[i], &m_commands.streamOut.stride[i].header);
        }
    }

    // NOTE: Supporting real-time compute requires use of SET_SH_REG_INDEX for this register.
    m_commands.dynamic.ps.spaceNeeded = cmdUtil.BuildSetOneShRegIndex(
                                                        mmSPI_SHADER_PGM_RSRC3_PS,
                                                        ShaderGraphics,
                                                        index__pfp_set_sh_reg_index__apply_kmd_cu_and_mask,
                                                        &m_commands.dynamic.ps.hdrPgmRsrc3Ps);

    if (IsGfx10(chipProps.gfxLevel))
    {
        m_commands.dynamic.ps.spaceNeeded += cmdUtil.BuildSetOneShRegIndex(
                                                    Gfx10::mmSPI_SHADER_PGM_RSRC4_PS,
                                                    ShaderGraphics,
                                                    index__pfp_set_sh_reg_index__apply_kmd_cu_and_mask,
                                                    &m_commands.dynamic.ps.hdrPgmRsrc4Ps);
    }

    if (loadInfo.enableNgg == false)
    {
        // NOTE: Supporting real-time compute requires use of SET_SH_REG_INDEX for this register.
        m_commands.dynamic.vs.spaceNeeded = cmdUtil.BuildSetOneShRegIndex(
                                                        mmSPI_SHADER_PGM_RSRC3_VS,
                                                        ShaderGraphics,
                                                        index__pfp_set_sh_reg_index__apply_kmd_cu_and_mask,
                                                        &m_commands.dynamic.vs.hdrPgmRsrc3Vs);

        if (IsGfx10(chipProps.gfxLevel))
        {
            m_commands.dynamic.vs.spaceNeeded += cmdUtil.BuildSetOneShRegIndex(
                                                            Gfx10::mmSPI_SHADER_PGM_RSRC4_VS,
                                                            ShaderGraphics,
                                                            index__pfp_set_sh_reg_index__apply_kmd_cu_and_mask,
                                                            &m_commands.dynamic.vs.hdrPgmRsrc4Vs);
        }
    } // if enableNgg == false

    if (chipProps.gfx9.supportSpiPrefPriority)
    {
        m_commands.sh.ps.spaceNeeded += cmdUtil.BuildSetSeqShRegs(Gfx10::mmSPI_SHADER_USER_ACCUM_PS_0,
                                                                  Gfx10::mmSPI_SHADER_USER_ACCUM_PS_3,
                                                                  ShaderGraphics,
                                                                  &m_commands.sh.ps.hdrSpishaderUserAccumPs);
    }
    else
    {
        m_commands.sh.ps.spaceNeeded += cmdUtil.BuildNop(CmdUtil::ShRegSizeDwords + 4,
                                                         &m_commands.sh.ps.hdrSpishaderUserAccumPs);
    }

    if (chipProps.gfx9.supportSpiPrefPriority && (loadInfo.enableNgg == false))
    {
        m_commands.sh.vs.spaceNeeded += cmdUtil.BuildSetSeqShRegs(Gfx10::mmSPI_SHADER_USER_ACCUM_VS_0,
                                                                  Gfx10::mmSPI_SHADER_USER_ACCUM_VS_3,
                                                                  ShaderGraphics,
                                                                  &m_commands.sh.vs.hdrSpishaderUserAccumVs);
    }
    else
    {
        m_commands.sh.vs.spaceNeeded += cmdUtil.BuildNop(CmdUtil::ShRegSizeDwords + 4,
                                                         &m_commands.sh.vs.hdrSpishaderUserAccumVs);
    }

    if (IsGfx10(chipProps.gfxLevel))
    {
        m_commands.sh.ps.spaceNeeded += cmdUtil.BuildSetOneShReg(Gfx10::mmSPI_SHADER_REQ_CTRL_PS,
                                                                 ShaderGraphics,
                                                                 &m_commands.sh.ps.hdrShaderReqCtrlPs);
    }
    else
    {
        m_commands.sh.ps.spaceNeeded += cmdUtil.BuildNop(CmdUtil::ShRegSizeDwords + 1,
                                                         &m_commands.sh.ps.hdrShaderReqCtrlPs);
    }

    cmdUtil.BuildContextRegRmw(mmPA_SC_AA_CONFIG,
                               PA_SC_AA_CONFIG__COVERAGE_TO_SHADER_SELECT_MASK,
                               0,
                               &m_commands.common.paScAaConfig);
}

} // Gfx9
} // Pal
