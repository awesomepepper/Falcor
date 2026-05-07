/***************************************************************************
 # Copyright (c) 2015-24, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPass.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "Rendering/Lights/EnvMapSampler.h"
#include "Rendering/Lights/EmissiveLightSampler.h"
#include "Rendering/Lights/EmissiveUniformSampler.h"
#include "Rendering/Lights/EmissivePowerSampler.h"
#include "Rendering/Lights/LightBVHSampler.h"
#include "Rendering/Materials/TexLODTypes.slang"
#include "Params.slang"

using namespace Falcor;

/** Path tracer using ReSTIR PT with DXR 1.1 TraceRayInline.
    Based on "Generalized Resampled Importance Sampling" (GRIS) / ReSTIR PT.
*/
class ReSTIRPTPass : public RenderPass
{
public:
    FALCOR_PLUGIN_CLASS(ReSTIRPTPass, "ReSTIRPTPass", "ReSTIR-based path tracer using DXR 1.1 TraceRayInline.");

    static ref<ReSTIRPTPass> create(ref<Device> pDevice, const Properties& props) { return make_ref<ReSTIRPTPass>(pDevice, props); }

    ReSTIRPTPass(ref<Device> pDevice, const Properties& props);

    virtual Properties getProperties() const override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void compile(RenderContext* pContext, const CompileData& compileData) override;
    virtual void setScene(RenderContext* pRenderContext, const ref<Scene>& pScene) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;
    virtual bool onMouseEvent(const MouseEvent& mouseEvent) override { return false; }
    virtual bool onKeyEvent(const KeyboardEvent& keyEvent) override { return false; }

    static void registerBindings(pybind11::module& m);

    /** Static configuration. Changing any of these options require shader recompilation. */
    struct StaticParams
    {
        uint32_t    samplesPerPixel = 1;
        uint32_t    candidateSamples = 8;
        uint32_t    maxSurfaceBounces = 9;
        uint32_t    maxDiffuseBounces = -1;
        uint32_t    maxSpecularBounces = -1;
        uint32_t    maxTransmissionBounces = -1;
        uint32_t    sampleGenerator = SAMPLE_GENERATOR_TINY_UNIFORM;
        bool        adjustShadingNormals = false;
        bool        useBSDFSampling = true;
        bool        useNEE = true;
        bool        useMIS = true;
        bool        useRussianRoulette = false;
        bool        useAlphaTest = true;
        uint32_t    maxNestedMaterials = 2;
        bool        useLightsInDielectricVolumes = false;
        bool        limitTransmission = false;
        uint32_t    maxTransmissionReflectionDepth = 0;
        uint32_t    maxTransmissionRefractionDepth = 0;
        bool        disableCaustics = false;
        bool        disableDirectIllumination = true;
        TexLODMode  primaryLodMode = TexLODMode::Mip0;
        uint32_t    colorFormat = 1;
        uint32_t    misHeuristic = 0;
        float       misPowerExponent = 2.f;
        uint32_t    emissiveSampler = 2;

        bool        useDeterministicBSDF = true;
        uint32_t    spatialMisKind = 2;
        uint32_t    temporalMisKind = 1;
        uint32_t    shiftStrategy = 2;
        bool        temporalUpdateForDynamicScene = false;
        uint32_t    pathSamplingMode = 0;
        bool            separatePathBSDF = true;
        bool            rcDataOfflineMode = false;
        bool            useNRDDemodulation = true;

        DefineList getDefines() const;
    };

private:
    void Init();
    void parseProperties(const Properties& props);
    void validateOptions();
    void updatePrograms();
    void prepareResources(RenderContext* pRenderContext, const RenderData& renderData);
    void setNRDData(const ShaderVar& var, const RenderData& renderData) const;
    void preparePathTracer(const RenderData& renderData);
    void resetLighting();
    void prepareMaterials(RenderContext* pRenderContext);
    bool prepareLighting(RenderContext* pRenderContext);
    void setShaderData(const ShaderVar& var, const RenderData& renderData, bool isPathTracer, bool isPathGenerator) const;
    bool renderRenderingUI(Gui::Widgets& widget);
    bool renderDebugUI(Gui::Widgets& widget);
    bool beginFrame(RenderContext* pRenderContext, const RenderData& renderData);
    void endFrame(RenderContext* pRenderContext, const RenderData& renderData);
    void generatePaths(RenderContext* pRenderContext, const RenderData& renderData, int sampleId = 0);
    void tracePass(RenderContext* pRenderContext, const RenderData& renderData, const ref<ComputePass>& pass, const std::string& passName, int sampleID);
    void PathReusePass(RenderContext* pRenderContext, uint32_t restir_i, const RenderData& renderData, bool isTemporalReuse = false, int spatialRoundId = 0, bool isLastRound = false);
    void PathRetracePass(RenderContext* pRenderContext, uint32_t restir_i, const RenderData& renderData, bool temporalReuse = false, int spatialRoundId = 0);
    ref<Texture> createNeighborOffsetTexture(uint32_t sampleCount);

    // Configuration
    RestirPathTracerParams          mParams;
    StaticParams                    mStaticParams;
    LightBVHSampler::Options        mLightBVHOptions;

    // Internal state
    ref<Scene>                      mpScene;
    ref<SampleGenerator>            mpSampleGenerator;
    std::unique_ptr<EnvMapSampler>      mpEnvMapSampler;
    std::unique_ptr<EmissiveLightSampler> mpEmissiveSampler;

    bool                            mRecompile = false;
    bool                            mVarsChanged = true;
    bool                            mOptionsChanged = false;
    bool                            mGBufferAdjustShadingNormals = false;
    bool                            mOutputTime = false;
    bool                            mOutputNRDData = false;

    bool                            mEnableTemporalReuse = true;
    bool                            mEnableSpatialReuse = true;
    uint32_t                        mSpatialReusePattern = 0;  // SpatialReusePattern::Default
    uint32_t                        mPathReusePattern = 2;     // PathReusePattern::NRooksShift
    uint32_t                        mSmallWindowRestirWindowRadius = 2;
    int                             mSpatialNeighborCount = 3;
    float                           mSpatialReuseRadius = 20.f;
    int                             mNumSpatialRounds = 1;

    bool                            mEnableTemporalReprojection = true;
    bool                            mFeatureBasedRejection = true;
    bool                            mUseMaxHistory = true;
    int                             mReservoirFrameCount = 0;
    bool                            mUseDirectLighting = false;
    int                             mTemporalHistoryLength = 20;
    bool                            mNoResamplingForTemporalReuse = false;
    int                             mSeedOffset = 0;

    bool                            mResetRenderPassFlags = false;

    ref<ComputePass>                mpSpatialReusePass;
    ref<ComputePass>                mpTemporalReusePass;
    ref<ComputePass>                mpComputePathReuseMISWeightsPass;
    ref<ComputePass>                mpSpatialPathRetracePass;
    ref<ComputePass>                mpTemporalPathRetracePass;
    ref<ComputePass>                mpGeneratePaths;
    ref<ComputePass>                mpTracePass;
    ref<ComputePass>                mpReflectTypes;

    // Data
    ref<Buffer>                     mpCounters;
    ref<Buffer>                     mpCountersReadback;
    ref<Buffer>                     mpOutputReservoirs;
    std::vector<ref<Buffer>>        mpTemporalReservoirs;
    ref<Buffer>                     mReconnectionDataBuffer;
    ref<Buffer>                     mPathReuseMISWeightBuffer;
    ref<Texture>                    mpTemporalVBuffer;
    ref<Texture>                    mpNeighborOffsets;
    ref<Buffer>                     mNRooksPatternBuffer;
};
