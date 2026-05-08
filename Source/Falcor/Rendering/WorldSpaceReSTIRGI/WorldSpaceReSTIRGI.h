#pragma once

#include "Core/Macros.h"
#include "Core/Enum.h"
#include "Scene/Scene.h"
#include "Utils/Sampling/SampleGenerator.h"
#include "Utils/Algorithm/PrefixSum.h"
#include "Params.slang"

namespace Falcor
{
    class FALCOR_API WorldSpaceReSTIRGI
    {
    public:
        enum class TargetPdf
        {
            IncomingRadiance = 0,
            OutgoingRadiance = 1
        };

        FALCOR_ENUM_INFO(TargetPdf, {
            { TargetPdf::IncomingRadiance, "IncomingRadiance" },
            { TargetPdf::OutgoingRadiance, "OutgoingRadiance" },
        });

        struct Options
        {
            float normalThreshold = 0.9f;
            float depthThreshold = 0.1f;

            float roughnessThreshold = 0.2f;
            uint sceneGridDimension = 80u;
            TargetPdf resamplingTargetPdf = TargetPdf::IncomingRadiance;
        };

        WorldSpaceReSTIRGI(ref<Device> pDevice, ref<Scene> pScene, const Options& options, uint instanceID, uint numInstance);

        DefineList getDefines() const;

        bool renderUI(Gui::Widgets& widget);

        void BeginFrame(RenderContext* pRenderContext, uint2 frameDim);
        void UpdateReSTIRGI(RenderContext* pRenderContext, const ref<Buffer>& initialSample, const ref<Texture>& vNormW, const ref<Texture>& vDepth, const ref<Buffer>& reconnectionData, const ref<Texture>& vbuffer);
        void EndFrame(RenderContext* pRenderContext);

        void CopyRecompileState(const WorldSpaceReSTIRGI& other);

        ref<Buffer> mpFinalSample;
        GIParameter params;

    private:
        bool UpdateResources(uint2 frameDim);
        void UpdateProgram();
        void InitReservoirPass(RenderContext* pRenderContext, const ref<Buffer>& initialSample);
        void BuildHashGridPass(RenderContext* pRenderContext);
        void ResamplingPass(RenderContext* pRenderContext, const ref<Texture>& vDepth, const ref<Texture>& vNormW, const ref<Buffer>& reconnectionData, const ref<Texture>& vbuffer);
        void FinalShadingPass(RenderContext* pRenderContext);

        ref<Device> mpDevice;
        Options mOptions;
        ref<Scene> mpScene;

        ref<SampleGenerator> mpSampleGenerator;

        ref<ComputePass> mpInitReservoirPass;
        ref<ComputePass> mpGIResamplingPass;
        ref<ComputePass> mpFinalShadingPass;
        ref<ComputePass> mpBuildHashGridPass;

        ref<ComputePass> mpReflectTypes;

        ref<Buffer> mpInitialReservoir;
        ref<Buffer> mpReservoirs[2];

        ref<Buffer> mpAppendBuffer;

        ref<Buffer> mpCellStorage[2];
        ref<Buffer> mpIndexBuffer[2];
        ref<Buffer> mpCheckSumBuffer[2];
        ref<Buffer> mpCellCounter[2];

        std::unique_ptr<PrefixSum> mpPrefixSumPass;

        float3 mPreCameraPos;
        float4x4 mPreViewProj;

        bool mRecompile = true;
        bool mOptionChanged = false;

        uint giInstanceNum = 1u;
    };

    FALCOR_ENUM_REGISTER(WorldSpaceReSTIRGI::TargetPdf);
}
