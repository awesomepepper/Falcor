/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
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
#include "RTPM.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "RenderGraph/RenderPassStandardFlags.h"

#include <random>
#include <ctime>
#include <limits>
#include <fstream>

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, RTPM>();
}

namespace
{
const char kShaderGeneratePhoton[] = "RenderPasses/RTPM/PMGenerate.rt.slang";
const char kShaderCollectPhoton[] = "RenderPasses/RTPM/PMCollect.cs.slang";
const char kShaderVisualPhoton[] = "RenderPasses/RTPM/PMVisual.cs.slang";
const char kUsePhotonVisualization[] = "usePhotonVisualization";

// Ray tracing settings that affect the traversal stack size.
const uint32_t kMaxPayloadSizeBytes = 64u;
const uint32_t kMaxAttributeSizeBytes = 8u;
const uint32_t kMaxRecursionDepth = 2u;

const ChannelList kInputChannels = {
    {"vbuffer", "gVBuffer", "V Buffer to get the intersected triangle", false},
    {"viewW", "gViewWorld", "World View Direction", false},
    {"thp", "gThp", "Throughput", true /* optional */},
    {"emissive", "gEmissive", "Emissive", true /* optional */},
    {"normale", "gNormale", "The Geometry normal of the intersected triangle", true /* optional */}};

const ChannelList kOutputChannels = {
    {"PhotonImage", "gPhotonImage", "An image that shows indirect light from photons", false, ResourceFormat::RGBA32Float}};

const Gui::DropdownList kInfoTexDropdownList{
    {(uint)RTPM::TextureFormat::_16Bit, "16Bits"},
    {(uint)RTPM::TextureFormat::_32Bit, "32Bits"}};

const Gui::DropdownList kLightTexModeList{{RTPM::LightTexMode::power, "Power"}, {RTPM::LightTexMode::area, "Area"}};
} // namespace

RTPM::RTPM(ref<Device> pDevice, const Properties& props) : RenderPass(pDevice)
{
    parseProperties(props);
    mpSampleGenerator = SampleGenerator::create(pDevice, SAMPLE_GENERATOR_UNIFORM);
    FALCOR_ASSERT(mpSampleGenerator);
}

void RTPM::parseProperties(const Properties& props)
{
    for (const auto& [key, value] : props)
    {
        if (key == kUsePhotonVisualization)
            mUsePhotonVisualization = value;
    }
}

Properties RTPM::getProperties() const
{
    Properties props;
    props[kUsePhotonVisualization] = mUsePhotonVisualization;
    return props;
}

RenderPassReflection RTPM::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    addRenderPassInputs(reflector, kInputChannels);
    addRenderPassOutputs(reflector, kOutputChannels);
    return reflector;
}

void RTPM::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    auto& dict = renderData.getDictionary();
    if (mOptionsChanged)
    {
        auto flags = dict.getValue(kRenderPassRefreshFlags, RenderPassRefreshFlags::None);
        dict[Falcor::kRenderPassRefreshFlags] = flags | Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
        mResetIterations = true;
        mSetConstantBuffers = true;
        mOptionsChanged = false;
        mResetTimer = true;
    }

    if (!mpScene)
        return;

    if (mResetIterations || mAlwaysResetIterations || is_set(mpScene->getUpdates(), Scene::UpdateFlags::CameraMoved))
    {
        mFrameCount = 0;
        mResetIterations = false;
        mResetTimer = true;
    }

    checkTimer();
    if (mUseTimer && mTimerStopRenderer)
        return;

    copyPhotonCounter(pRenderContext);

    if (mNumPhotonsChanged)
    {
        changeNumPhotons();
        mNumPhotonsChanged = false;
    }

    if (mFrameCount == 0)
    {
        mPhotonRadius = mPhotonRadiusStart;
    }

    if (is_set(mpScene->getUpdates(), Scene::UpdateFlags::GeometryChanged))
    {
        throw std::runtime_error("This render pass does not support scene geometry changes. Aborting.");
    }

    if (mpScene->getRenderSettings().useEmissiveLights)
    {
        mpScene->getLightCollection(pRenderContext);
    }

    if (mResizePhotonBuffers)
    {
        if (mFitBuffersToPhotonShot)
        {
            if (mPhotonCount > 0)
            {
                mPhotonBufferSizeUI = static_cast<uint>(mPhotonCount * 1.1);
            }
            mFitBuffersToPhotonShot = false;
        }
        uint photonWidth = static_cast<uint>(std::ceil(mPhotonBufferSizeUI / static_cast<float>(kInfoTexHeight)));
        mPhotonBuffers.maxSize = photonWidth * kInfoTexHeight;

        mPhotonBufferSizeUI = mPhotonBuffers.maxSize;
        mResizePhotonBuffers = false;
        mPhotonBuffersReady = false;
        mRebuildAS = true;
    }

    if (mPhotonBuffersReady && mPhotonInfoFormatChanged)
    {
        preparePhotonInfoTexture();
        mPhotonInfoFormatChanged = false;
    }

    if (!mPhotonBuffersReady)
    {
        mPhotonBuffersReady = preparePhotonBuffers();
    }

    if (!mRandNumSeedBuffer)
    {
        prepareRandomSeedBuffer(renderData.getDefaultTextureDims());
    }

    if (mRebuildLightTex)
    {
        mLightSampleTex.reset();
        mRebuildLightTex = false;
    }

    if (!mLightSampleTex)
    {
        createLightSampleTexture(pRenderContext);
    }

    if (mResetCS)
    {
        mpCSCollect.reset();
        prepareHashBuffer();
        mResetCS = false;
    }

    generatePhotons(pRenderContext, renderData);
    mUsePhotonVisualization ? visualizePhotons(pRenderContext, renderData) : collectPhotons(pRenderContext, renderData);
    mFrameCount++;

    if (mUseStatisticProgressivePM)
    {
        float itF = static_cast<float>(mFrameCount);
        mPhotonRadius *= sqrt((itF + mSPPMAlpha) / (itF + 1.0f));
        mPhotonRadius = std::max(mPhotonRadius, kMinPhotonRadius);
    }

    if (mSetConstantBuffers)
        mSetConstantBuffers = false;
}

void RTPM::generatePhotons(RenderContext* pRenderContext, const RenderData& renderData)
{
    pRenderContext->copyBufferRegion(mPhotonCounterBuffer.counter.get(), 0, mPhotonCounterBuffer.reset.get(), 0, sizeof(uint32_t));
    pRenderContext->resourceBarrier(mPhotonCounterBuffer.counter.get(), Resource::State::ShaderResource);

    pRenderContext->clearTexture(mPhotonBuffers.position.get(), float4(0, 0, 0, 0));
    pRenderContext->clearTexture(mPhotonBuffers.infoFlux.get(), float4(0, 0, 0, 0));
    pRenderContext->clearTexture(mPhotonBuffers.infoDir.get(), float4(0, 0, 0, 0));
    pRenderContext->clearUAV(mpPhotonBuckets->getUAV().get(), uint4(0, 0, 0, 0));

    auto lightCollection = mpScene->getLightCollection(pRenderContext);

    DefineList generateDefines;
    generateDefines.add("USE_ANALYTIC_LIGHTS", mpScene->useAnalyticLights() ? "1" : "0");
    generateDefines.add("USE_EMISSIVE_LIGHTS", mpScene->useEmissiveLights() ? "1" : "0");
    generateDefines.add("USE_ENV_LIGHT", mpScene->useEnvLight() ? "1" : "0");
    generateDefines.add("USE_ENV_BACKGROUND", mpScene->useEnvBackground() ? "1" : "0");
    generateDefines.add("MAX_PHOTON_INDEX", std::to_string(mPhotonBuffers.maxSize));
    generateDefines.add("ANALYTIC_INV_PDF", std::to_string(mAnalyticInvPdf));
    generateDefines.add("INFO_TEXTURE_HEIGHT", std::to_string(kInfoTexHeight));
    generateDefines.add("NUM_PHOTONS_PER_BUCKET", std::to_string(mNumPhotonsPerBucket));
    generateDefines.add("NUM_BUCKETS", std::to_string(mNumBuckets));
    generateDefines.add("PHOTON_FACE_NORMAL", mEnableFaceNormalRejection ? "1" : "0");
    mTracerGenerate.pProgram->addDefines(generateDefines);

    if (!mTracerGenerate.pVars)
        prepareVars();
    FALCOR_ASSERT(mTracerGenerate.pVars);

    auto& dict = renderData.getDictionary();

    auto var = mTracerGenerate.pVars->getRootVar();

    std::string nameBuf = "PerFrame";
    var[nameBuf]["gFrameCount"] = mFrameCount;
    var[nameBuf]["gPhotonRadius"] = mPhotonRadius;
    var[nameBuf]["gPhotonHashScaleFactor"] = 1.f / mPhotonRadius;

    if (mSetConstantBuffers)
    {
        nameBuf = "CB";
        var[nameBuf]["gPRNGDimension"] = dict.keyExists(kRenderPassPRNGDimension) ? dict[kRenderPassPRNGDimension] : 0u;
        var[nameBuf]["gPhotonStoreProbability"] = mPhotonStoreProbability;
        var[nameBuf]["gEmissiveScale"] = mIntensityScalar;
        var[nameBuf]["gSpecRoughCutoff"] = mSpecRoughCutoff;

        var[nameBuf]["gMaxRecursion"] = mMaxBounces;
        var[nameBuf]["gUseAlphaTest"] = mUseAlphaTest;
        var[nameBuf]["gAdjustShadingNormals"] = mAdjustShadingNormals;
        var[nameBuf]["gQuadProbeIt"] = mQuadraticProbeIterations;
    }

    var["gPhotonPos"] = mPhotonBuffers.position;
    var["gPhotonFlux"] = mPhotonBuffers.infoFlux;
    var["gPhotonDir"] = mPhotonBuffers.infoDir;
    var["gRndSeedBuffer"] = mRandNumSeedBuffer;
    var["gPhotonHashBucket"] = mpPhotonBuckets;
    var["gPhotonCounter"] = mPhotonCounterBuffer.counter;
    var["gLightSample"] = mLightSampleTex;
    var["gNumPhotonsPerEmissive"] = mPhotonsPerTriangle;

    const uint2 targetDim = uint2(mPGDispatchX, mMaxDispatchY);
    FALCOR_ASSERT(targetDim.x > 0 && targetDim.y > 0);

    mpScene->raytrace(pRenderContext, mTracerGenerate.pProgram.get(), mTracerGenerate.pVars, uint3(targetDim, 1));
}

void RTPM::collectPhotons(RenderContext* pRenderContext, const RenderData& renderData)
{
    FALCOR_PROFILE(pRenderContext, "collect photons");

    if (!mpCSCollect)
    {
        ProgramDesc desc;
        desc.addShaderModules(mpScene->getShaderModules());
        desc.addShaderLibrary(kShaderCollectPhoton).csEntry("main");
        desc.addTypeConformances(mpScene->getTypeConformances());

        DefineList defines;
        defines.add(mpScene->getSceneDefines());
        defines.add(mpSampleGenerator->getDefines());
        defines.add("INFO_TEXTURE_HEIGHT", std::to_string(kInfoTexHeight));
        defines.add("NUM_PHOTONS_PER_BUCKET", std::to_string(mNumPhotonsPerBucket));
        defines.add("NUM_BUCKETS", std::to_string(mNumBuckets));
        defines.add("PHOTON_FACE_NORMAL", mEnableFaceNormalRejection ? "1" : "0");

        mpCSCollect = ComputePass::create(mpDevice, desc, defines);
    }

    auto var = mpCSCollect->getRootVar();
    mpScene->bindShaderData(var["gScene"]);
    mpSampleGenerator->bindShaderData(var);

    std::string nameBuf = "PerFrame";
    var[nameBuf]["gFrameCount"] = mFrameCount;
    var[nameBuf]["gPhotonRadius"] = mPhotonRadius;
    var[nameBuf]["gPhotonHashScaleFactor"] = 1.f / mPhotonRadius;

    if (mSetConstantBuffers)
    {
        nameBuf = "CB";
        var[nameBuf]["gEmissiveScale"] = mIntensityScalar;
        var[nameBuf]["gQuadProbeIt"] = mQuadraticProbeIterations;
        var[nameBuf]["gEnableStochasicGathering"] = mEnableStochasticCollection;
        var[nameBuf]["gCollectProbability"] = mStochasticCollectProbability;
    }

    var["gPhotonHashBucket"] = mpPhotonBuckets;
    var["gPhotonPos"] = mPhotonBuffers.position;
    var["gPhotonFlux"] = mPhotonBuffers.infoFlux;
    var["gPhotonDir"] = mPhotonBuffers.infoDir;

    prepareFallbackTextures();
    auto bindAsTex = [&](const ChannelDesc& desc)
    {
        if (!desc.texname.empty())
        {
            ref<Texture> pTex = renderData.getTexture(desc.name);
            if (!pTex)
            {
                if (std::string(desc.name) == "thp")
                    pTex = mpDefaultThp;
                else if (std::string(desc.name) == "emissive")
                    pTex = mpDefaultEmissive;
                else if (std::string(desc.name) == "normale")
                    pTex = mpDefaultNormale;
            }
            if (pTex)
                var[desc.texname] = pTex;
        }
    };
    for (auto& channel : kInputChannels)
        bindAsTex(channel);
    bindAsTex(kOutputChannels[0]);

    const uint2 targetDim = renderData.getDefaultTextureDims();
    FALCOR_ASSERT(targetDim.x > 0 && targetDim.y > 0);
    FALCOR_ASSERT(pRenderContext && mpCSCollect);

    mpCSCollect->execute(pRenderContext, uint3(targetDim, 1));
}

void RTPM::visualizePhotons(RenderContext* pRenderContext, const RenderData& renderData)
{
    FALCOR_PROFILE(pRenderContext, "visualize photons");

    if (!mpCSCollect)
    {
        ProgramDesc desc;
        desc.addShaderLibrary(kShaderVisualPhoton).csEntry("main");
        desc.addTypeConformances(mpScene->getTypeConformances());

        DefineList defines;
        defines.add(mpScene->getSceneDefines());
        defines.add(mpSampleGenerator->getDefines());
        defines.add("INFO_TEXTURE_HEIGHT", std::to_string(kInfoTexHeight));
        defines.add("NUM_PHOTONS_PER_BUCKET", std::to_string(mNumPhotonsPerBucket));
        defines.add("NUM_BUCKETS", std::to_string(mNumBuckets));
        defines.add("PHOTON_FACE_NORMAL", mEnableFaceNormalRejection ? "1" : "0");

        mpCSCollect = ComputePass::create(mpDevice, desc, defines);
    }

    auto var = mpCSCollect->getRootVar();
    mpScene->bindShaderData(var["gScene"]);
    mpSampleGenerator->bindShaderData(var);

    std::string nameBuf = "PerFrame";
    var[nameBuf]["gFrameCount"] = mFrameCount;
    var[nameBuf]["gPhotonRadius"] = mPhotonRadius;
    var[nameBuf]["gPhotonHashScaleFactor"] = 1.f / mPhotonRadius;

    if (mSetConstantBuffers)
    {
        nameBuf = "CB";
        var[nameBuf]["gEmissiveScale"] = mIntensityScalar;
        var[nameBuf]["gQuadProbeIt"] = mQuadraticProbeIterations;
        var[nameBuf]["gEnableStochasicGathering"] = mEnableStochasticCollection;
        var[nameBuf]["gCollectProbability"] = mStochasticCollectProbability;
    }

    var["gPhotonHashBucket"] = mpPhotonBuckets;
    var["gPhotonPos"] = mPhotonBuffers.position;
    var["gPhotonFlux"] = mPhotonBuffers.infoFlux;
    var["gPhotonDir"] = mPhotonBuffers.infoDir;

    auto bindAsTex = [&](const ChannelDesc& desc)
    {
        if (!desc.texname.empty())
        {
            var[desc.texname] = renderData.getTexture(desc.name);
        }
    };
    for (auto& channel : kInputChannels)
        bindAsTex(channel);
    bindAsTex(kOutputChannels[0]);

    const uint2 targetDim = renderData.getDefaultTextureDims();
    FALCOR_ASSERT(targetDim.x > 0 && targetDim.y > 0);
    FALCOR_ASSERT(pRenderContext && mpCSCollect);

    mpCSCollect->execute(pRenderContext, uint3(targetDim, 1));
}

void RTPM::copyPhotonCounter(RenderContext* pRenderContext)
{
    pRenderContext->copyBufferRegion(mPhotonCounterBuffer.cpuCopy.get(), 0, mPhotonCounterBuffer.counter.get(), 0, sizeof(uint32_t));

    void* data = mPhotonCounterBuffer.cpuCopy->map();
    std::memcpy(&mPhotonCount, data, sizeof(uint));
    mPhotonCounterBuffer.cpuCopy->unmap();
}

void RTPM::changeNumPhotons()
{
    if (mNumPhotonsUI != mNumPhotons)
    {
        mNumPhotons = mNumPhotonsUI;
        mLightSampleTex = nullptr;
        mFrameCount = 0;
    }

    if (mPhotonBuffers.maxSize != mPhotonBufferSizeUI || mFitBuffersToPhotonShot)
    {
        mResizePhotonBuffers = true;
        mPhotonBuffersReady = false;
        mPhotonBuffers.maxSize = 0;
    }
}

ResourceFormat inline getFormatRGBA(uint format, bool flux = true)
{
    switch (format)
    {
    case static_cast<uint>(RTPM::TextureFormat::_8Bit):
        if (flux)
            return ResourceFormat::RGBA8Unorm;
        else
            return ResourceFormat::RGBA8Snorm;
    case static_cast<uint>(RTPM::TextureFormat::_16Bit):
        return ResourceFormat::RGBA16Float;
    case static_cast<uint>(RTPM::TextureFormat::_32Bit):
        return ResourceFormat::RGBA32Float;
    }

    return ResourceFormat::RGBA32Float;
}

void RTPM::preparePhotonInfoTexture()
{
    FALCOR_ASSERT(mPhotonBuffers.maxSize > 0);

    mPhotonBuffers.infoFlux.reset();
    mPhotonBuffers.infoDir.reset();
    mPhotonBuffers.position.reset();

    mPhotonBuffers.infoFlux = mpDevice->createTexture2D(
        mPhotonBuffers.maxSize / kInfoTexHeight,
        kInfoTexHeight,
        getFormatRGBA(mInfoTexFormat, true),
        1,
        1,
        nullptr,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );
    mPhotonBuffers.infoFlux->setName("PhotonMapperHash::mPhotonBuffers.fluxInfo");

    mPhotonBuffers.infoDir = mpDevice->createTexture2D(
        mPhotonBuffers.maxSize / kInfoTexHeight,
        kInfoTexHeight,
        getFormatRGBA(mInfoTexFormat, false),
        1,
        1,
        nullptr,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );
    mPhotonBuffers.infoDir->setName("PhotonMapperHash::mPhotonBuffers.dirInfo");

    mPhotonBuffers.position = mpDevice->createTexture2D(
        mPhotonBuffers.maxSize / kInfoTexHeight,
        kInfoTexHeight,
        ResourceFormat::RGBA32Float,
        1,
        1,
        nullptr,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess
    );
    mPhotonBuffers.position->setName("PhotonMapperHash::mPhotonBuffers.position");

    FALCOR_ASSERT(mPhotonBuffers.infoFlux);
    FALCOR_ASSERT(mPhotonBuffers.infoDir);
    FALCOR_ASSERT(mPhotonBuffers.position);
}

bool RTPM::preparePhotonBuffers()
{
    FALCOR_ASSERT(mPhotonBuffers.maxSize > 0);

    prepareHashBuffer();
    preparePhotonInfoTexture();

    return true;
}

void RTPM::prepareHashBuffer()
{
    if (mpPhotonBuckets)
    {
        mpPhotonBuckets.reset();
    }

    mNumBuckets = 1 << mNumBucketBits;
    mpPhotonBuckets = mpDevice->createStructuredBuffer(
        sizeof(uint32_t) * (mNumPhotonsPerBucket + 5),
        mNumBuckets,
        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess,
        MemoryType::DeviceLocal,
        nullptr,
        false
    );
    mpPhotonBuckets->setName("PhotonMapperHash::Bucket");
}

void RTPM::prepareRandomSeedBuffer(const uint2 screenDimensions)
{
    FALCOR_ASSERT(screenDimensions.x > 0 && screenDimensions.y > 0);

    std::seed_seq seq{time(0)};
    std::vector<uint32_t> cpuSeeds(screenDimensions.x * screenDimensions.y);
    seq.generate(cpuSeeds.begin(), cpuSeeds.end());

    mRandNumSeedBuffer = mpDevice->createTexture2D(
        screenDimensions.x,
        screenDimensions.y,
        ResourceFormat::R32Uint,
        1,
        1,
        cpuSeeds.data()
    );
    mRandNumSeedBuffer->setName("PhotonMapperHash::RandomSeedBuffer");

    FALCOR_ASSERT(mRandNumSeedBuffer);
}

void RTPM::createLightSampleTexture(RenderContext* pRenderContext)
{
    if (mPhotonsPerTriangle)
        mPhotonsPerTriangle.reset();
    if (mLightSampleTex)
        mLightSampleTex.reset();

    FALCOR_ASSERT(mpScene);

    auto lightCollection = mpScene->getLightCollection(pRenderContext);

    // Get active analytic lights
    std::vector<ref<Light>> analyticLights;
    for (const auto& light : mpScene->getLights())
    {
        if (light->isActive())
        {
            analyticLights.push_back(light);
        }
    }

    uint analyticPhotons = 0;
    uint numEmissivePhotons = 0;

    if (analyticLights.size() != 0)
    {
        uint lightsTotal = static_cast<uint>(analyticLights.size() + lightCollection->getMeshLights().size());
        float percentAnalytic = static_cast<float>(analyticLights.size()) / static_cast<float>(lightsTotal);
        analyticPhotons = static_cast<uint>(mNumPhotons * percentAnalytic);
        analyticPhotons += (uint)analyticLights.size() - (analyticPhotons % (uint)analyticLights.size());
        numEmissivePhotons = mNumPhotons - analyticPhotons;
    }
    else
        numEmissivePhotons = mNumPhotons;

    std::vector<uint> numPhotonsPerTriangle;

    if (numEmissivePhotons > 0)
    {
        getActiveEmissiveTriangles(pRenderContext);
        auto meshLightTriangles = lightCollection->getMeshLightTriangles(pRenderContext);

        float totalMode = 0;
        for (uint i = 0; i < (uint)mActiveEmissiveTriangles.size(); i++)
        {
            uint triIdx = mActiveEmissiveTriangles[i];

            switch (mLightTexMode)
            {
            case LightTexMode::power:
                totalMode += meshLightTriangles[triIdx].flux;
                break;
            case LightTexMode::area:
                totalMode += meshLightTriangles[triIdx].area;
                break;
            default:
                totalMode += meshLightTriangles[triIdx].flux;
            }
        }
        float photonsPerMode = numEmissivePhotons / totalMode;

        uint tmpNumEmissivePhotons = 0;
        numPhotonsPerTriangle.reserve(mActiveEmissiveTriangles.size());
        for (uint i = 0; i < (uint)mActiveEmissiveTriangles.size(); i++)
        {
            uint triIdx = mActiveEmissiveTriangles[i];
            uint photons = 0;

            switch (mLightTexMode)
            {
            case LightTexMode::power:
                photons = static_cast<uint>(std::ceil(meshLightTriangles[triIdx].flux * photonsPerMode));
                break;
            case LightTexMode::area:
                photons = static_cast<uint>(std::ceil(meshLightTriangles[triIdx].area * photonsPerMode));
                break;
            default:
                photons = static_cast<uint>(std::ceil(meshLightTriangles[triIdx].flux * photonsPerMode));
            }

            if (photons == 0)
                photons = 1;
            tmpNumEmissivePhotons += photons;
            numPhotonsPerTriangle.push_back(photons);
        }
        numEmissivePhotons = tmpNumEmissivePhotons;
    }

    uint totalNumPhotons = numEmissivePhotons + analyticPhotons;

    if (analyticPhotons > 0 && analyticLights.size() > 0)
    {
        mAnalyticInvPdf =
            (static_cast<float>(totalNumPhotons) * static_cast<float>(analyticLights.size())) / static_cast<float>(analyticPhotons);
    }

    const uint blockSize = 16;
    const uint blockSizeSq = blockSize * blockSize;

    uint xPhotons = (totalNumPhotons / mMaxDispatchY) + 1;
    xPhotons += (xPhotons % blockSize == 0 && analyticPhotons > 0) ? blockSize : blockSize - (xPhotons % blockSize);

    std::vector<int32_t> lightIdxTex(xPhotons * mMaxDispatchY, 0);

    auto getIndex = [&](uint2 idx) { return idx.x + idx.y * xPhotons; };

    auto getBlockStartingIndex = [&](uint blockIdx)
    {
        blockIdx = blockIdx * blockSize;
        uint x = blockIdx % xPhotons;
        uint y = (blockIdx / xPhotons) * blockSize;
        return uint2(x, y);
    };

    if (analyticLights.size() > 0)
    {
        uint numCurrentLight = 0;
        uint step = analyticPhotons / static_cast<uint>(analyticLights.size());
        bool stop = false;
        for (uint i = 0; i <= analyticPhotons / blockSizeSq; i++)
        {
            if (stop)
                break;
            for (uint y = 0; y < blockSize; y++)
            {
                if (stop)
                    break;
                for (uint x = 0; x < blockSize; x++)
                {
                    if (numCurrentLight >= analyticPhotons)
                    {
                        stop = true;
                        break;
                    }
                    uint2 idx = getBlockStartingIndex(i);
                    idx += uint2(x, y);
                    int32_t lightIdx = static_cast<int32_t>((numCurrentLight / step) + 1);
                    lightIdx *= -1;
                    lightIdxTex[getIndex(idx)] = lightIdx;
                    numCurrentLight++;
                }
            }
        }
    }

    if (numEmissivePhotons > 0)
    {
        uint analyticEndBlock = analyticPhotons > 0 ? (analyticPhotons / blockSizeSq) + 1 : 0;
        uint currentActiveTri = 0;
        uint lightInActiveTri = 0;
        bool stop = false;
        for (uint i = 0; i <= numEmissivePhotons / blockSizeSq; i++)
        {
            if (stop)
                break;
            for (uint y = 0; y < blockSize; y++)
            {
                if (stop)
                    break;
                for (uint x = 0; x < blockSize; x++)
                {
                    if (currentActiveTri >= static_cast<uint>(numPhotonsPerTriangle.size()))
                    {
                        stop = true;
                        break;
                    }
                    uint2 idx = getBlockStartingIndex(i + analyticEndBlock);
                    idx += uint2(x, y);
                    int32_t lightIdx = static_cast<int32_t>(currentActiveTri + 1);
                    lightIdxTex[getIndex(idx)] = lightIdx;

                    lightInActiveTri++;
                    if (lightInActiveTri >= numPhotonsPerTriangle[currentActiveTri])
                    {
                        currentActiveTri++;
                        lightInActiveTri = 0;
                    }
                }
            }
        }
    }

    mLightSampleTex = mpDevice->createTexture2D(xPhotons, mMaxDispatchY, ResourceFormat::R32Int, 1, 1, lightIdxTex.data());
    mLightSampleTex->setName("PhotonMapperHash::LightSampleTex");

    if (numPhotonsPerTriangle.size() == 0)
    {
        numPhotonsPerTriangle.push_back(0);
    }
    mPhotonsPerTriangle = mpDevice->createStructuredBuffer(
        sizeof(uint),
        static_cast<uint32_t>(numPhotonsPerTriangle.size()),
        ResourceBindFlags::ShaderResource,
        MemoryType::DeviceLocal,
        numPhotonsPerTriangle.data()
    );
    mPhotonsPerTriangle->setName("PhotonMapperHash::mPhotonsPerTriangleEmissive");

    mPGDispatchX = xPhotons;

    mNumPhotons = mPGDispatchX * mMaxDispatchY;
    mNumPhotonsUI = mNumPhotons;
}

void RTPM::getActiveEmissiveTriangles(RenderContext* pRenderContext)
{
    auto lightCollection = mpScene->getLightCollection(pRenderContext);
    auto meshLightTriangles = lightCollection->getMeshLightTriangles(pRenderContext);

    mActiveEmissiveTriangles.clear();
    mActiveEmissiveTriangles.reserve(meshLightTriangles.size());

    for (uint32_t triIdx = 0; triIdx < (uint32_t)meshLightTriangles.size(); triIdx++)
    {
        if (meshLightTriangles[triIdx].flux > 0.f)
        {
            mActiveEmissiveTriangles.push_back(triIdx);
        }
    }
}

void RTPM::prepareFallbackTextures()
{
    if (!mpDefaultThp)
    {
        float4 defaultThp = float4(1, 1, 1, 1);
        mpDefaultThp = mpDevice->createTexture2D(1, 1, ResourceFormat::RGBA32Float, 1, 1, &defaultThp);
        mpDefaultThp->setName("RTPM::DefaultThp");
    }
    if (!mpDefaultEmissive)
    {
        float4 defaultEmissive = float4(0, 0, 0, 0);
        mpDefaultEmissive = mpDevice->createTexture2D(1, 1, ResourceFormat::RGBA32Float, 1, 1, &defaultEmissive);
        mpDefaultEmissive->setName("RTPM::DefaultEmissive");
    }
    if (!mpDefaultNormale)
    {
        float4 defaultNormale = float4(0, 0, 1, 0);
        mpDefaultNormale = mpDevice->createTexture2D(1, 1, ResourceFormat::RGBA32Float, 1, 1, &defaultNormale);
        mpDefaultNormale->setName("RTPM::DefaultNormale");
    }
}

void RTPM::prepareVars()
{
    FALCOR_ASSERT(mTracerGenerate.pProgram);

    mTracerGenerate.pProgram->addDefines(mpSampleGenerator->getDefines());
    mTracerGenerate.pProgram->setTypeConformances(mpScene->getTypeConformances());

    mTracerGenerate.pVars = RtProgramVars::create(mpDevice, mTracerGenerate.pProgram, mTracerGenerate.pBindingTable);

    auto var = mTracerGenerate.pVars->getRootVar();
    mpSampleGenerator->bindShaderData(var);
}

void RTPM::renderUI(Gui::Widgets& widget)
{
    float2 dummySpacing = float2(0, 10);
    bool dirty = false;

    mResetCS |= widget.checkbox("Photon visualization?", mUsePhotonVisualization);

    widget.text("Iterations: " + std::to_string(mFrameCount));
    widget.text("Photons: " + std::to_string(mPhotonCount) + " / " + std::to_string(mPhotonBuffers.maxSize));
    widget.tooltip("Photons for current Iteration / Buffer Size");

    widget.text("Current Photon Radius: " + std::to_string(mPhotonRadius));

    widget.dummy("", dummySpacing);
    widget.var("Number Photons", mNumPhotonsUI, 1000u, UINT_MAX, 1000u);
    widget.tooltip("The number of photons that are shot per iteration. Press \"Apply\" to apply the change");
    widget.var("Size Photon Buffer", mPhotonBufferSizeUI, 1000u, UINT_MAX, 1000u);
    mNumPhotonsChanged |= widget.button("Apply");
    widget.dummy("", float2(15, 0), true);
    mFitBuffersToPhotonShot |= widget.button("Fit Buffers", true);
    widget.tooltip("Fits the photon buffer to current number of photons shot + 10 %");
    widget.dummy("", dummySpacing);

    mNumPhotonsChanged |= mFitBuffersToPhotonShot;

    dirty |= widget.checkbox("Use SPPM", mUseStatisticProgressivePM);
    widget.tooltip("Activate Statistically Progressive Photon Mapping");

    if (mUseStatisticProgressivePM)
    {
        dirty |= widget.var("SPPM Alpha", mSPPMAlpha, 0.1f, 1.0f, 0.001f);
        widget.tooltip("Sets the alpha in SPPM for photons");
    }

    widget.dummy("", dummySpacing);
    dirty |= widget.slider("Max Recursion Depth", mMaxBounces, 1u, 32u);
    widget.tooltip("Maximum path length for Photon Bounces");
    mResetCS |= widget.checkbox("Use Photon Face Normal Rejection", mEnableFaceNormalRejection);
    widget.tooltip("Uses encoded Face Normal to reject photon hits on different surfaces (corners / other side of wall).");
    dirty |= mResetCS;

    widget.dummy("", dummySpacing);

    if (auto group = widget.group("Timer"))
    {
        bool resetTimer = false;
        resetTimer |= widget.checkbox("Enable Timer", mUseTimer);
        widget.tooltip("Enables the timer");
        if (mUseTimer)
        {
            uint sec = static_cast<uint>(mTimerDurationSec);
            if (sec != 0)
                widget.text("Elapsed seconds: " + std::to_string(mCurrentElapsedTime) + " / " + std::to_string(sec));
            if (mTimerMaxIterations != 0)
                widget.text("Iterations: " + std::to_string(mFrameCount) + " / " + std::to_string(mTimerMaxIterations));
            resetTimer |= widget.var("Timer Seconds", sec, 0u, UINT_MAX, 1u);
            widget.tooltip("Time in seconds needed to stop rendering. When 0 time is not used");
            resetTimer |= widget.var("Max Iterations", mTimerMaxIterations, 0u, UINT_MAX, 1u);
            widget.tooltip("Max iterations until stop. When 0 iterations are not used");
            mTimerDurationSec = static_cast<double>(sec);
            resetTimer |= widget.checkbox("Record Times", mTimerRecordTimes);
            resetTimer |= widget.button("Reset Timer");
            if (mTimerRecordTimes)
            {
                if (widget.button("Store Times", true))
                {
                    FileDialogFilterVec filters;
                    filters.push_back({"csv", "CSV Files"});
                    std::filesystem::path path;
                    if (saveFileDialog(filters, path))
                    {
                        mTimesOutputFilePath = path.string();
                        outputTimes();
                    }
                }
            }
        }
        mResetTimer |= resetTimer;
        dirty |= resetTimer;
    }

    if (auto group = widget.group("Radius Options"))
    {
        dirty |= widget.var("Photon Radius Start", mPhotonRadiusStart, kMinPhotonRadius, FLT_MAX, 0.001f);
        widget.tooltip("The start value for the photon search radius");
        dirty |= widget.var("Photon Store Probability", mPhotonStoreProbability, 0.001f, 1.f, 0.001f);
        widget.tooltip("Probability that a diffuse photon is saved. Stored photon flux is compensated by this value.");
    }

    if (auto group = widget.group("Material Options"))
    {
        dirty |= widget.var("Emissive Scalar", mIntensityScalar, 0.0f, FLT_MAX, 0.001f);
        widget.tooltip("Scales the intensity of all emissive Light Sources");
        dirty |= widget.var("SpecRoughCutoff", mSpecRoughCutoff, 0.0f, 1.0f, 0.01f);
        widget.tooltip("The cutoff for Specular Materials. All Reflections above this threshold are considered Diffuse");
        dirty |= widget.checkbox("Alpha Test", mUseAlphaTest);
        widget.tooltip("Enables Alpha Test for Photon Generation");
        dirty |= widget.checkbox("Adjust Shading Normals", mAdjustShadingNormals);
        widget.tooltip("Adjusts the shading normals in the Photon Generation");
    }

    if (auto group = widget.group("Hash Options"))
    {
        dirty |= widget.var("Quadratic Probe Iterations", mQuadraticProbeIterations, 0u, 100u, 1u);
        widget.tooltip("Max iterations that are used for quadratic probe");
        mResetCS |= widget.slider("Num Photons per bucket", mNumPhotonsPerBucket, 2u, 32u);
        widget.tooltip("Max number of photons that can be saved in a hash grid");
        mResetCS |= widget.slider("Bucket size (bits)", mNumBucketBits, 2u, 32u);
        widget.tooltip("Bucket size in 2^x. One bucket takes 20Byte + Num photons per bucket * 4 Byte");

        dirty |= mResetCS;
    }

    if (auto group = widget.group("Light Sample Tex"))
    {
        mRebuildLightTex |= widget.dropdown("Sample mode", kLightTexModeList, (uint32_t&)mLightTexMode);
        widget.tooltip("Changes photon distribution for the light sampling texture. Also rebuilds the texture.");
        mRebuildLightTex |= widget.button("Rebuild Light Tex");
        dirty |= mRebuildLightTex;
    }

    mPhotonInfoFormatChanged |= widget.dropdown("Photon Info size", kInfoTexDropdownList, mInfoTexFormat);
    widget.tooltip("Determines the resolution of each element of the photon info struct.");

    dirty |= mPhotonInfoFormatChanged;

    if (auto group = widget.group("Collect Options"))
    {
        dirty |= widget.checkbox("Stochastic Collection", mEnableStochasticCollection);
        widget.tooltip("Enables stochastic collection. A geometrically distributed random step is used for that");
        if (mEnableStochasticCollection)
        {
            dirty |= widget.slider("Stochastic Collection Probability", mStochasticCollectProbability, 0.0001f, 1.0f);
            widget.tooltip("Probability for the geometrically distributed random step");
        }
    }

    widget.dummy("", dummySpacing);
    widget.checkbox("Always Reset Iterations", mAlwaysResetIterations);
    widget.tooltip("Always Resets the Iterations, currently good for moving the camera");
    mResetIterations |= widget.button("Reset Iterations");
    widget.tooltip("Resets the iterations");
    dirty |= mResetIterations;

    if (dirty)
        mOptionsChanged = true;
}

void RTPM::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    resetPhotonMapper();

    mTracerGenerate.pProgram = nullptr;
    mTracerGenerate.pBindingTable = nullptr;
    mTracerGenerate.pVars = nullptr;
    mpCSCollect.reset();
    mSetConstantBuffers = true;

    mpScene = pScene;
    mpScene->setIsAnimated(false);

    if (mpScene)
    {
        if (mpScene->hasProceduralGeometry())
        {
            logWarning("RTPM: This render pass only supports triangles. Other types of geometry will be ignored.");
        }

        // Create ray tracing program.
        ProgramDesc desc;
        desc.addShaderModules(mpScene->getShaderModules());
        desc.addShaderLibrary(kShaderGeneratePhoton);
        desc.addTypeConformances(mpScene->getTypeConformances());
        desc.setMaxPayloadSize(kMaxPayloadSizeBytes);
        desc.setMaxAttributeSize(kMaxAttributeSizeBytes);
        desc.setMaxTraceRecursionDepth(kMaxRecursionDepth);

        mTracerGenerate.pBindingTable = RtBindingTable::create(2, 2, mpScene->getGeometryCount());
        auto& sbt = mTracerGenerate.pBindingTable;
        sbt->setRayGen(desc.addRayGen("rayGen"));
        sbt->setMiss(0, desc.addMiss("miss"));

        if (mpScene->hasGeometryType(Scene::GeometryType::TriangleMesh))
        {
            sbt->setHitGroup(0, mpScene->getGeometryIDs(Scene::GeometryType::TriangleMesh), desc.addHitGroup("closestHit", "anyHit"));
        }

        mTracerGenerate.pProgram = Program::create(mpDevice, desc, mpScene->getSceneDefines());
    }

    preparePhotonCounters();
}

void RTPM::resetPhotonMapper()
{
    mFrameCount = 0;

    mResizePhotonBuffers = true;
    mPhotonBuffersReady = false;
    mPhotonBuffers.maxSize = 0;
    mPhotonCount = 0;

    mResetCS = true;
    mSetConstantBuffers = true;

    mLightSampleTex = nullptr;
}

void RTPM::preparePhotonCounters()
{
    mPhotonCounterBuffer.counter = mpDevice->createStructuredBuffer(sizeof(uint), 1);
    mPhotonCounterBuffer.counter->setName("PhotonMapperHash::PhotonCounter");

    uint32_t zeroInit = 0;
    mPhotonCounterBuffer.reset = mpDevice->createBuffer(sizeof(uint32_t), ResourceBindFlags::None, MemoryType::DeviceLocal, &zeroInit);
    mPhotonCounterBuffer.reset->setName("PhotonMapperHash::PhotonCounterReset");

    mPhotonCounterBuffer.cpuCopy = mpDevice->createBuffer(sizeof(uint32_t), ResourceBindFlags::None, MemoryType::ReadBack);
    mPhotonCounterBuffer.cpuCopy->setName("PhotonMapperHash::PhotonCounterCPU");
}

void RTPM::checkTimer()
{
    if (!mUseTimer)
        return;

    if (mResetTimer)
    {
        mCurrentElapsedTime = 0.0;
        mTimerStartTime = std::chrono::steady_clock::now();
        mTimerStopRenderer = false;
        mResetTimer = false;
        if (mTimerRecordTimes)
        {
            mTimesList.clear();
            mTimesList.reserve(10000);
        }
        return;
    }

    if (mTimerStopRenderer)
        return;

    if (mTimerDurationSec != 0)
    {
        auto currentTime = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsedSec = currentTime - mTimerStartTime;
        mCurrentElapsedTime = elapsedSec.count();

        if (mTimerDurationSec <= mCurrentElapsedTime)
        {
            mTimerStopRenderer = true;
        }
    }

    if (mTimerMaxIterations != 0)
    {
        if (mTimerMaxIterations <= mFrameCount)
        {
            mTimerStopRenderer = true;
        }
    }

    if (mTimerRecordTimes)
    {
        mTimesList.push_back(mCurrentElapsedTime);
    }
}

void RTPM::outputTimes()
{
    if (mTimesOutputFilePath.empty() || mTimesList.empty())
        return;

    std::ofstream file = std::ofstream(mTimesOutputFilePath, std::ios::trunc);

    if (!file)
    {
        FALCOR_THROW(fmt::format("Failed to open file '{}'.", mTimesOutputFilePath));
        mTimesOutputFilePath.clear();
        return;
    }

    file << "Hash_Times" << std::endl;
    file << std::fixed << std::setprecision(16);
    for (size_t i = 0; i < mTimesList.size(); i++)
    {
        file << mTimesList[i];
        file << std::endl;
    }
    file.close();
}
