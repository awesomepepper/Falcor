#include "Rendering/WorldSpaceReSTIRGI/WorldSpaceReSTIRGI.h"
#include "Core/Error.h"
#include "Scene/Camera/Camera.h"
#include "Utils/Math/FalcorMath.h"
#include "Utils/Timing/Profiler.h"
#include "Utils/UI/Gui.h"

namespace Falcor
{
    namespace
    {
        const std::string kReflectTypeFilePath = "Rendering/WorldSpaceReSTIRGI/ReflectTypes.cs.slang";
        const std::string kInitialReservoirFilePath = "Rendering/WorldSpaceReSTIRGI/InitialReservoirs.cs.slang";
        const std::string kBuildHashGridFilePath = "Rendering/WorldSpaceReSTIRGI/BuildHashGrid.cs.slang";
        const std::string kGIResamplingFilePath = "Rendering/WorldSpaceReSTIRGI/SpatiotemporalResampling.cs.slang";
        const std::string kFinalSampleFilePath = "Rendering/WorldSpaceReSTIRGI/FinalSample.cs.slang";

        const Gui::DropdownList kReSTIRGIModeList =
        {
            {(uint32_t)WorldSpaceReSTIRGI::TargetPdf::IncomingRadiance, "incoming radiance"},
            {(uint32_t)WorldSpaceReSTIRGI::TargetPdf::OutgoingRadiance, "outgoing radiance"},
        };
    }

    WorldSpaceReSTIRGI::WorldSpaceReSTIRGI(ref<Device> pDevice, ref<Scene> pScene, const Options& options, uint instanceID, uint numInstance)
        : mpDevice(pDevice), mOptions(options), mpScene(pScene)
    {
        mpSampleGenerator = SampleGenerator::create(mpDevice, SAMPLE_GENERATOR_TINY_UNIFORM);
        mpPrefixSumPass = std::make_unique<PrefixSum>(mpDevice);
        FALCOR_ASSERT(mpScene);

        auto defines = getDefines();

        mpReflectTypes = ComputePass::create(mpDevice, kReflectTypeFilePath, "main", defines);
        params.instanceID = instanceID;
        params.frameCount = 0u;
        giInstanceNum = numInstance;
    }

    DefineList WorldSpaceReSTIRGI::getDefines() const
    {
        DefineList defines;

        if (mpScene) defines.add(mpScene->getSceneDefines());
        if (mpSampleGenerator) defines.add(mpSampleGenerator->getDefines());

        defines.add("GI_ROUGHNESS_THRESHOLD", std::to_string(mOptions.roughnessThreshold));
        defines.add("GI_TARGET_PDF", std::to_string((int)mOptions.resamplingTargetPdf));

        return defines;
    }

    bool WorldSpaceReSTIRGI::renderUI(Gui::Widgets& widget)
    {
        bool staticDirty = false;
        bool runtimeDirty = false;

        if (auto group = widget.group("ReSTIRGI Options"))
        {
            runtimeDirty |= widget.var("Normal threshold", mOptions.normalThreshold, 0.f, 1.f);
            runtimeDirty |= widget.var("Depth threshold", mOptions.depthThreshold, 0.f, 1.f);
            runtimeDirty |= widget.var("Cells Dimension", mOptions.sceneGridDimension, 1u, 300u);

            staticDirty |= widget.var("Roughness threshold", mOptions.roughnessThreshold, 0.f, 1.2f);
            staticDirty |= widget.dropdown("Target pdf mode", kReSTIRGIModeList, reinterpret_cast<uint32_t&>(mOptions.resamplingTargetPdf));
        }

        if (staticDirty) mRecompile = true;
        bool dirty = staticDirty || runtimeDirty;
        if (dirty) mOptionChanged = true;

        return dirty;
    }

    void WorldSpaceReSTIRGI::BeginFrame(RenderContext* pRenderContext, uint2 frameDim)
    {
        bool resourcesChanged = UpdateResources(frameDim);
        params.frameDim = frameDim;
        params.fov = focalLengthToFovY(mpScene->getCamera()->getFocalLength(), Camera::kDefaultFrameHeight);
        params.sceneBBMin = mpScene->getSceneBounds().minPoint - float3(0.1f, 0.1f, 0.1f);

        float3 boundingSize = abs((mpScene->getSceneBounds().maxPoint - mpScene->getSceneBounds().minPoint) / static_cast<float>(mOptions.sceneGridDimension));

        params._pad = float4(0, 0, 0, 0);
        params.minCellSize = std::max(boundingSize.x, std::max(boundingSize.y, boundingSize.z));

        if (resourcesChanged || params.frameCount == 0)
        {
            for (uint32_t i = 0; i < 2; i++)
            {
                pRenderContext->clearUAV(mpCheckSumBuffer[i]->getUAV().get(), uint4(0));
                pRenderContext->clearUAV(mpCellCounter[i]->getUAV().get(), uint4(0));
                pRenderContext->clearUAV(mpIndexBuffer[i]->getUAV().get(), uint4(0));
                pRenderContext->clearUAV(mpCellStorage[i]->getUAV().get(), uint4(0));
                pRenderContext->clearUAV(mpReservoirs[i]->getUAV().get(), uint4(0));
            }
        }
        else
        {
            uint32_t currentBuffer = (params.frameCount + 1) % 2;
            pRenderContext->clearUAV(mpCheckSumBuffer[currentBuffer]->getUAV().get(), uint4(0));
            pRenderContext->clearUAV(mpCellCounter[currentBuffer]->getUAV().get(), uint4(0));
            pRenderContext->clearUAV(mpIndexBuffer[currentBuffer]->getUAV().get(), uint4(0));
        }
    }

    bool WorldSpaceReSTIRGI::UpdateResources(uint2 frameDim)
    {
        uint32_t elementCount = frameDim.x * frameDim.y;
        bool resourcesChanged = false;

        if (!mpInitialReservoir || mpInitialReservoir->getElementCount() != elementCount)
        {
            mpInitialReservoir = mpDevice->createStructuredBuffer(mpReflectTypes->getRootVar()["initialReservoirs"], elementCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr);
            resourcesChanged = true;
        }

        if (!mpFinalSample || mpFinalSample->getElementCount() != elementCount)
        {
            mpFinalSample = mpDevice->createStructuredBuffer(mpReflectTypes->getRootVar()["finalSample"], elementCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr);
            resourcesChanged = true;
        }

        if (!mpAppendBuffer || mpAppendBuffer->getElementCount() != elementCount)
        {
            mpAppendBuffer = mpDevice->createStructuredBuffer(mpReflectTypes->getRootVar()["appendBuffer"], elementCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr);
            resourcesChanged = true;
        }

        uint32_t reservoirCount = elementCount * 2;

        for (uint32_t i = 0; i < 2; i++)
        {
            if (!mpReservoirs[i] || mpReservoirs[i]->getElementCount() != reservoirCount)
            {
                mpReservoirs[i] = mpDevice->createStructuredBuffer(mpReflectTypes->getRootVar()["spatiotemporalReservoirs"], reservoirCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr);
                resourcesChanged = true;
            }
        }

        uint32_t hashBufferCount = 3200000 * sizeof(uint32_t);
        for (uint32_t i = 0; i < 2; i++)
        {
            if (!mpCellCounter[i])
            {
                mpCellCounter[i] = mpDevice->createBuffer(hashBufferCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal);
                mpCellCounter[i]->setName("cellCounter");
                resourcesChanged = true;
            }
            if (!mpIndexBuffer[i])
            {
                mpIndexBuffer[i] = mpDevice->createBuffer(hashBufferCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal);
                mpIndexBuffer[i]->setName("indexBuffer");
                resourcesChanged = true;
            }
            if (!mpCheckSumBuffer[i])
            {
                mpCheckSumBuffer[i] = mpDevice->createBuffer(hashBufferCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal);
                mpCheckSumBuffer[i]->setName("checkSumBuffer");
                resourcesChanged = true;
            }
            if (!mpCellStorage[i] || mpCellStorage[i]->getElementCount() != elementCount)
            {
                mpCellStorage[i] = mpDevice->createStructuredBuffer(mpReflectTypes->getRootVar()["cellStorage"], elementCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr);
                resourcesChanged = true;
            }
        }

        return resourcesChanged;
    }

    void WorldSpaceReSTIRGI::EndFrame(RenderContext* pRenderContext)
    {
        params.frameCount++;

        mPreCameraPos = mpScene->getCamera()->getPosition();
        mPreViewProj = mpScene->getCamera()->getViewProjMatrixNoJitter();
    }

    void WorldSpaceReSTIRGI::UpdateReSTIRGI(RenderContext* pRenderContext, const ref<Buffer>& initialSample, const ref<Texture>& vNormW, const ref<Texture>& vDepth, const ref<Buffer>& reconnectionData, const ref<Texture>& vbuffer)
    {
        UpdateProgram();
        InitReservoirPass(pRenderContext, initialSample);
        BuildHashGridPass(pRenderContext);
        ResamplingPass(pRenderContext, vDepth, vNormW, reconnectionData, vbuffer);
        FinalShadingPass(pRenderContext);
    }

    void WorldSpaceReSTIRGI::UpdateProgram()
    {
        if (!mRecompile) return;

        DefineList defines = getDefines();

        mpInitReservoirPass = ComputePass::create(mpDevice, kInitialReservoirFilePath, "main", defines);
        mpBuildHashGridPass = ComputePass::create(mpDevice, kBuildHashGridFilePath, "main", defines);
        mpGIResamplingPass = ComputePass::create(mpDevice, kGIResamplingFilePath, "main", defines);
        mpFinalShadingPass = ComputePass::create(mpDevice, kFinalSampleFilePath, "main", defines);

        mRecompile = false;
    }

    void WorldSpaceReSTIRGI::InitReservoirPass(RenderContext* pRenderContext, const ref<Buffer>& initialSample)
    {
        FALCOR_PROFILE(pRenderContext, "WorldSpaceReSTIR::InitReservoir");

        auto var = mpInitReservoirPass->getRootVar();
        var["sampleManager"]["initialSamples"] = initialSample;
        var["sampleManager"]["initialReservoirs"] = mpInitialReservoir;
        var["sampleManager"]["appendBuffer"] = mpAppendBuffer;
        var["sampleManager"]["checkSum"] = mpCheckSumBuffer[(params.frameCount + 1) % 2];
        var["sampleManager"]["cellCounters"] = mpCellCounter[(params.frameCount + 1) % 2];

        var["sampleManager"]["cameraPos"] = mpScene->getCamera()->getPosition();

        var["sampleManager"]["params"].setBlob(params);

        var["sampleManager"]["finalSample"] = mpFinalSample;

        mpInitReservoirPass->execute(pRenderContext, uint3(params.frameDim.x, params.frameDim.y, 1u));
    }

    void WorldSpaceReSTIRGI::BuildHashGridPass(RenderContext* pRenderContext)
    {
        FALCOR_PROFILE(pRenderContext, "WorldSpaceReSTIR::BuildHashGrid");

        pRenderContext->copyBufferRegion(mpIndexBuffer[(params.frameCount + 1) % 2].get(), 0, mpCellCounter[(params.frameCount + 1) % 2].get(), 0, mpCellCounter[(params.frameCount + 1) % 2]->getSize());
        mpPrefixSumPass->execute(
            pRenderContext,
            mpIndexBuffer[(params.frameCount + 1) % 2],
            static_cast<uint32_t>(mpIndexBuffer[(params.frameCount + 1) % 2]->getSize() / sizeof(uint32_t))
        );

        auto var = mpBuildHashGridPass->getRootVar();
        var["gridBuilder"]["indexBuffer"] = mpIndexBuffer[(params.frameCount + 1) % 2];
        var["gridBuilder"]["appendBuffer"] = mpAppendBuffer;
        var["gridBuilder"]["cellStorage"] = mpCellStorage[(params.frameCount + 1) % 2];

        var["gridBuilder"]["params"].setBlob(params);

        mpBuildHashGridPass->execute(pRenderContext, uint3(params.frameDim.x, params.frameDim.y, 1u));
    }

    void WorldSpaceReSTIRGI::ResamplingPass(RenderContext* pRenderContext, const ref<Texture>& vDepth, const ref<Texture>& vNormW, const ref<Buffer>& reconnectionData, const ref<Texture>& vbuffer)
    {
        FALCOR_PROFILE(pRenderContext, "WorldSpaceReSTIR::ReSampling");

        auto var = mpGIResamplingPass->getRootVar();

        mpScene->bindShaderData(var["gScene"]);

        var["resampleManager"]["depth"] = vDepth;
        var["resampleManager"]["norm"] = vNormW;
        var["resampleManager"]["reconnectionDataBuffer"] = reconnectionData;

        var["resampleManager"]["prevViewProj"] = mPreViewProj;
        var["resampleManager"]["cameraPrePos"] = mPreCameraPos;

        var["resampleManager"]["vbuffer"] = vbuffer;

        var["resampleManager"]["initialReservoirs"] = mpInitialReservoir;
        var["resampleManager"]["preReservoirs"] = mpReservoirs[(params.frameCount + 0) % 2];
        var["resampleManager"]["currentReservoirs"] = mpReservoirs[(params.frameCount + 1) % 2];

        var["resampleManager"]["cellStorage"] = mpCellStorage[(params.frameCount + 0) % 2];
        var["resampleManager"]["indexBuffer"] = mpIndexBuffer[(params.frameCount + 0) % 2];
        var["resampleManager"]["checkSum"] = mpCheckSumBuffer[(params.frameCount + 0) % 2];
        var["resampleManager"]["cellCounters"] = mpCellCounter[(params.frameCount + 0) % 2];

        var["resampleManager"]["numInstance"] = giInstanceNum;
        var["resampleManager"]["params"].setBlob(params);

        var["resampleManager"]["depthThreshold"] = mOptions.depthThreshold;
        var["resampleManager"]["normalThreshold"] = mOptions.normalThreshold;

        mpGIResamplingPass->execute(pRenderContext, uint3(params.frameDim.x, params.frameDim.y, 1u));

        mPreCameraPos = mpScene->getCamera()->getPosition();
        mPreViewProj = mpScene->getCamera()->getViewProjMatrixNoJitter();
    }

    void WorldSpaceReSTIRGI::FinalShadingPass(RenderContext* pRenderContext)
    {
        FALCOR_PROFILE(pRenderContext, "WorldSpaceReSTIR::FinalSample");
        auto var = mpFinalShadingPass->getRootVar();

        var["finalSampleGenerator"]["finalSample"] = mpFinalSample;
        var["finalSampleGenerator"]["currentReservoirs"] = mpReservoirs[(params.frameCount + 1) % 2];
        var["finalSampleGenerator"]["params"].setBlob(params);

        mpFinalShadingPass->execute(pRenderContext, uint3(params.frameDim.x, params.frameDim.y, 1u));
    }

    void WorldSpaceReSTIRGI::CopyRecompileState(const WorldSpaceReSTIRGI& other)
    {
        mRecompile = other.mRecompile;
    }
}
