/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
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
#include "WorldSpaceReSTIRGIPass.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "RenderGraph/RenderPassStandardFlags.h"

using namespace Falcor;

namespace
{
    const char kDesc[] = "World-space ReSTIR-based global illumination pass.";

    const std::string kTracePassFilePath = "RenderPasses/WorldSpaceReSTIRGIPass/TracePass.rt.slang";
    const std::string kFinalShadingFilePath = "RenderPasses/WorldSpaceReSTIRGIPass/FinalShading.cs.slang";
    const std::string kReflectTypeFilePath = "RenderPasses/WorldSpaceReSTIRGIPass/ReflectTypes.cs.slang";

    const std::string kInputVBuffer = "vbuffer";
    const std::string kInputDepthBuffer = "vDepth";
    const std::string kInputNormBuffer = "vNormW";

    const std::string kOutputColor = "outputColor";

    const ChannelList kInputChannels = {
        // clang-format off
        { kInputVBuffer,        "gVBuffer",     "Visibility buffer in packed format" },
        { kInputDepthBuffer,    "gDepth",       "Depth buffer" },
        { kInputNormBuffer,     "gNorm",        "World-space normal buffer" },
        // clang-format on
    };

    const ChannelList kOutputChannels = {
        // clang-format off
        { kOutputColor,         "gOutputColor", "Output color", false, ResourceFormat::RGBA32Float },
        // clang-format on
    };

    const uint32_t kMaxPayloadSizeBytes = 256u;
}

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, WorldSpaceReSTIRGIPass>();
}

WorldSpaceReSTIRGIPass::WorldSpaceReSTIRGIPass(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    mpDevice = pDevice;
    mOptions = WorldSpaceReSTIRGI::Options();
    parseProperties(props);
}

void WorldSpaceReSTIRGIPass::parseProperties(const Properties& props)
{
    for (const auto& [key, value] : props)
    {
        // Parse options from properties if needed
    }
}

Properties WorldSpaceReSTIRGIPass::getProperties() const
{
    Properties props;
    return props;
}

RenderPassReflection WorldSpaceReSTIRGIPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    addRenderPassInputs(reflector, kInputChannels);
    addRenderPassOutputs(reflector, kOutputChannels);
    return reflector;
}

void WorldSpaceReSTIRGIPass::compile(RenderContext* pRenderContext, const CompileData& compileData)
{
    params.frameDim = compileData.defaultTexDims;
}

void WorldSpaceReSTIRGIPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    auto& dict = renderData.getDictionary();

    const auto& pOutputColor = renderData.getTexture(kOutputColor);
    FALCOR_ASSERT(pOutputColor);

    if (!mpScene)
    {
        clearRenderPassChannels(pRenderContext, kOutputChannels, renderData);
        return;
    }

    if (mOptionChanged)
    {
        auto flags = dict.getValue(kRenderPassRefreshFlags, RenderPassRefreshFlags::None);
        dict[Falcor::kRenderPassRefreshFlags] = flags | Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
        mOptionChanged = false;
    }

    params.frameDim = uint2(pOutputColor->getWidth(), pOutputColor->getHeight());

    for (uint32_t i = 0; i < reSTIRInstances.size(); i++)
    {
        params.currentGIInstance = i;
        UpdateResource();
        UpdateProgram();
        reSTIRInstances[i]->BeginFrame(pRenderContext, params.frameDim);
        PrepareGIData(pRenderContext, renderData);
        reSTIRInstances[i]->UpdateReSTIRGI(pRenderContext, mpInitialSample,
            renderData.getTexture(kInputNormBuffer),
            renderData.getTexture(kInputDepthBuffer),
            mpReconnectionData,
            renderData.getTexture(kInputVBuffer));
        FinalShading(pRenderContext, renderData, i);
        reSTIRInstances[i]->EndFrame(pRenderContext);
    }

    params.frameCount++;
}

void WorldSpaceReSTIRGIPass::renderUI(Gui::Widgets& widget)
{
    bool staticDirty = false;
    bool runtimeDirty = false;

    if (widget.group("path tracing options"))
    {
        staticDirty |= widget.checkbox("useReSTIRDI", mPtOptions.usedReSTIRDI);
        staticDirty |= widget.checkbox("useNEE", mPtOptions.usedNEE);
        if (mPtOptions.usedNEE)
            staticDirty |= widget.checkbox("useMIS", mPtOptions.usedMIS);
        staticDirty |= widget.var("gibounce", mPtOptions.maxBounces, 1u, 10u);
    }

    staticDirty |= widget.var("giInstance", numReSTIRInstances, 1u, 6u);

    if (!reSTIRInstances.empty() && reSTIRInstances[0])
    {
        mOptionChanged |= reSTIRInstances[0]->renderUI(widget);
        staticDirty |= mOptionChanged;
        for (size_t i = 1; i < reSTIRInstances.size(); i++)
        {
            reSTIRInstances[i]->CopyRecompileState(*reSTIRInstances[0]);
        }
    }

    runtimeDirty |= widget.var("pad", pad, 0u, 2u);

    if (staticDirty) mRecompile = true;
    bool dirty = staticDirty || runtimeDirty;
    if (dirty) mOptionChanged = true;
}

void WorldSpaceReSTIRGIPass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    params.frameCount = 0u;

    mPathTracingPass.mpProgram = nullptr;
    mPathTracingPass.mpBindTable = nullptr;
    mPathTracingPass.mpVars = nullptr;

    mpSampleGenerator = SampleGenerator::create(mpDevice, SAMPLE_GENERATOR_TINY_UNIFORM);

    if (mpScene->getRenderSettings().useEmissiveLights)
    {
        mpScene->getLightCollection(pRenderContext);
    }

    if (mpScene->useEnvLight())
    {
        mpEnvMapSampler = std::make_unique<EnvMapSampler>(mpDevice, mpScene->getEnvMap());
    }

    if (mpScene->useEmissiveLights())
    {
        const auto& pLights = mpScene->getLightCollection(pRenderContext);
        mpEmissiveSampler = std::make_unique<EmissiveUniformSampler>(pRenderContext, pLights);
    }

    ProgramDesc desc;
    desc.addShaderLibrary(kTracePassFilePath);
    desc.setMaxAttributeSize(mpScene->getRaytracingMaxAttributeSize());
    desc.setMaxPayloadSize(kMaxPayloadSizeBytes);
    desc.setMaxTraceRecursionDepth(1);

    mPathTracingPass.mpBindTable = RtBindingTable::create(1, 1, mpScene->getGeometryCount());
    mPathTracingPass.mpBindTable->setRayGen(desc.addRayGen("RayGen"));
    mPathTracingPass.mpBindTable->setMiss(0, desc.addMiss("ScatterMiss"));

    if (mpScene->hasGeometryType(Scene::GeometryType::TriangleMesh))
    {
        mPathTracingPass.mpBindTable->setHitGroup(0, mpScene->getGeometryIDs(Scene::GeometryType::TriangleMesh), desc.addHitGroup("ScatterTriangleClosestHit", "ScatterTriangleAnyHit"));
    }

    if (mpScene->hasGeometryType(Scene::GeometryType::DisplacedTriangleMesh))
    {
        mPathTracingPass.mpBindTable->setHitGroup(0, mpScene->getGeometryIDs(Scene::GeometryType::DisplacedTriangleMesh), desc.addHitGroup("ScatterDisplacedTriangleMeshClosestHit", "", "DisplacedTriangleMeshIntersection"));
    }

    auto defines = GetDefines();
    mPathTracingPass.mpProgram = Program::create(mpDevice, desc, defines);
    mPathTracingPass.mpVars = RtProgramVars::create(mpDevice, mPathTracingPass.mpProgram, mPathTracingPass.mpBindTable);

    mpReflectTypePass = ComputePass::create(mpDevice, kReflectTypeFilePath, "main", defines);
    mpFinalShadingPass = ComputePass::create(mpDevice, kFinalShadingFilePath, "main", defines);

    {
        reSTIRInstances.clear();
        reSTIRInstances.reserve(numReSTIRInstances);
        params.numGIInstance = numReSTIRInstances;
        for (uint32_t i = 0; i < numReSTIRInstances; i++)
        {
            reSTIRInstances.push_back(std::make_unique<WorldSpaceReSTIRGI>(mpDevice, mpScene, mOptions, i, numReSTIRInstances));
        }
    }
}

void WorldSpaceReSTIRGIPass::UpdateProgram()
{
    if (!mRecompile) return;

    auto defines = GetDefines();

    ProgramDesc desc = mPathTracingPass.mpProgram->getDesc();
    mPathTracingPass.mpProgram = Program::create(mpDevice, desc, defines);
    mPathTracingPass.mpVars = RtProgramVars::create(mpDevice, mPathTracingPass.mpProgram, mPathTracingPass.mpBindTable);

    mRecompile = false;

    if (reSTIRInstances.size() != numReSTIRInstances)
    {
        reSTIRInstances.clear();
        reSTIRInstances.reserve(numReSTIRInstances);
        params.numGIInstance = numReSTIRInstances;
        for (uint32_t i = 0; i < numReSTIRInstances; i++)
        {
            reSTIRInstances.push_back(std::make_unique<WorldSpaceReSTIRGI>(mpDevice, mpScene, mOptions, i, numReSTIRInstances));
        }
    }
}

void WorldSpaceReSTIRGIPass::UpdateResource()
{
    uint32_t elementCount = params.frameDim.x * params.frameDim.y;
    if (!mpInitialSample || mpInitialSample->getElementCount() != elementCount)
    {
        mpInitialSample = mpDevice->createStructuredBuffer(mpReflectTypePass->getRootVar()["initialSamples"], elementCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr);
    }
    if (!mpReconnectionData || mpReconnectionData->getElementCount() != elementCount)
    {
        mpReconnectionData = mpDevice->createStructuredBuffer(mpReflectTypePass->getRootVar()["reconnectionDataBuffer"], elementCount, ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr);
    }
}

DefineList WorldSpaceReSTIRGIPass::GetDefines()
{
    DefineList defines;

    if (mpSampleGenerator) defines.add(mpSampleGenerator->getDefines());
    if (mpEmissiveSampler) defines.add(mpEmissiveSampler->getDefines());

    if (mpScene)
    {
        defines.add(mpScene->getSceneDefines());
        defines.add("USE_EMISSIVE_LIGHTS", mpScene->useEmissiveLights() ? "1" : "0");
        defines.add("USE_ANALYTIC_LIGHTS", mpScene->useAnalyticLights() ? "1" : "0");
        defines.add("USE_ENV_LIGHT", mpScene->useEnvLight() ? "1" : "0");
    }

    defines.add("USE_RESTIRDI", mPtOptions.usedReSTIRDI ? "1" : "0");
    defines.add("USE_NEE", mPtOptions.usedNEE ? "1" : "0");
    defines.add("USE_MIS", mPtOptions.usedMIS ? "1" : "0");
    defines.add("MAX_GI_BOUNCE", std::to_string(mPtOptions.maxBounces));
    defines.add("GI_ROUGHNESS_THRESHOLD", std::to_string(mOptions.roughnessThreshold));

    return defines;
}

void WorldSpaceReSTIRGIPass::PrepareGIData(RenderContext* pRenderContext, const RenderData& renderData)
{
    auto vars = mPathTracingPass.mpVars->getRootVar();

    vars["sampleInitializer"]["vbuffer"] = renderData.getTexture(kInputVBuffer);
    vars["sampleInitializer"]["initialSamples"] = mpInitialSample;
    vars["sampleInitializer"]["outputColor"] = renderData.getTexture(kOutputColor);
    vars["sampleInitializer"]["reconnectionDataBuffer"] = mpReconnectionData;
    vars["sampleInitializer"]["roughnessThreshold"] = mOptions.roughnessThreshold;

    vars["pathtracer"]["params"].setBlob(params);
    mpScene->bindShaderDataForRaytracing(pRenderContext, vars["gScene"]);

    if (mpEnvMapSampler) mpEnvMapSampler->bindShaderData(vars["pathtracer"]["envMapSampler"]);
    if (mpEmissiveSampler) mpEmissiveSampler->bindShaderData(vars["pathtracer"]["emissiveSampler"]);

    mpScene->raytrace(pRenderContext, mPathTracingPass.mpProgram.get(), mPathTracingPass.mpVars, uint3(params.frameDim.x, params.frameDim.y, 1u));
}

void WorldSpaceReSTIRGIPass::FinalShading(RenderContext* pRenderContext, const RenderData& renderData, uint currentInstance)
{
    auto vars = mpFinalShadingPass->getRootVar();

    vars["finalShading"]["vbuffer"] = renderData.getTexture(kInputVBuffer);
    vars["finalShading"]["reconnectionDataBuffer"] = mpReconnectionData;
    vars["finalShading"]["outputColor"] = renderData.getTexture(kOutputColor);

    vars["finalShading"]["params"].setBlob(params);
    vars["finalShading"]["finalSample"] = reSTIRInstances[currentInstance]->mpFinalSample;

    mpScene->bindShaderData(vars["gScene"]);

    mpFinalShadingPass->execute(pRenderContext, uint3(params.frameDim.x, params.frameDim.y, 1u));
}
