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
#include "ReSTIRPTPass.h"
#include "RenderGraph/RenderPassStandardFlags.h"
#include <cstdio>

namespace
{
    const char kShaderFile[] = "RenderPasses/ReSTIRPTPass/ReSTIRPTPass.cs.slang";

    const std::string kGeneratePathsFilename = "RenderPasses/ReSTIRPTPass/GeneratePaths.cs.slang";
    const std::string kTracePassFilename = "RenderPasses/ReSTIRPTPass/TracePass.cs.slang";
    const std::string kReflectTypesFile = "RenderPasses/ReSTIRPTPass/ReflectTypes.cs.slang";
    const std::string kSpatialReusePassFile = "RenderPasses/ReSTIRPTPass/SpatialReuse.cs.slang";
    const std::string kTemporalReusePassFile = "RenderPasses/ReSTIRPTPass/TemporalReuse.cs.slang";
    const std::string kSpatialPathRetraceFile = "RenderPasses/ReSTIRPTPass/SpatialPathRetrace.cs.slang";
    const std::string kTemporalPathRetraceFile = "RenderPasses/ReSTIRPTPass/TemporalPathRetrace.cs.slang";
    const std::string kComputePathReuseMISWeightsFile = "RenderPasses/ReSTIRPTPass/ComputePathReuseMISWeights.cs.slang";

    const std::string kInputVBuffer = "vbuffer";
    const std::string kInputMotionVectors = "motionVectors";
    const std::string kInputDirectLighting = "directLighting";

    const Falcor::ChannelList kInputChannels =
    {
        { kInputVBuffer,        "gVBuffer",         "Visibility buffer in packed format", false, ResourceFormat::Unknown },
        { kInputMotionVectors,  "gMotionVectors",   "Motion vector buffer (float format)", true, ResourceFormat::RG32Float },
        { kInputDirectLighting, "gDirectLighting",   "Direct lighting input", true, ResourceFormat::RGBA32Float },
    };

    const std::string kOutputColor = "color";
    const std::string kOutputAlbedo = "albedo";
    const std::string kOutputSpecularAlbedo = "specularAlbedo";
    const std::string kOutputIndirectAlbedo = "indirectAlbedo";
    const std::string kOutputNormal = "normal";
    const std::string kOutputReflectionPosW = "reflectionPosW";
    const std::string kOutputRayCount = "rayCount";
    const std::string kOutputPathLength = "pathLength";
    const std::string kOutputDebug = "debug";
    const std::string kOutputTime = "time";
    const std::string kOutputNRDDiffuseRadianceHitDist = "nrdDiffuseRadianceHitDist";
    const std::string kOutputNRDSpecularRadianceHitDist = "nrdSpecularRadianceHitDist";
    const std::string kOutputNRDResidualRadianceHitDist = "nrdResidualRadianceHitDist";
    const std::string kOutputNRDEmission = "nrdEmission";
    const std::string kOutputNRDDiffuseReflectance = "nrdDiffuseReflectance";
    const std::string kOutputNRDSpecularReflectance = "nrdSpecularReflectance";

    const Falcor::ChannelList kOutputChannels =
    {
        { kOutputColor,                 "gOutputColor",                 "Output color (linear)", true, ResourceFormat::RGBA32Float },
        { kOutputAlbedo,                "gOutputAlbedo",                "Output albedo (linear)", true, ResourceFormat::RGBA8Unorm },
        { kOutputNormal,                "gOutputNormal",                "Output normal (linear)", true, ResourceFormat::RGBA16Float },
        { kOutputRayCount,              "",                             "Per-pixel ray count", true, ResourceFormat::R32Uint },
        { kOutputPathLength,            "",                             "Per-pixel path length", true, ResourceFormat::R32Uint },
        { kOutputDebug,                 "",                             "Debug output", true, ResourceFormat::RGBA32Float },
        { kOutputTime,                  "",                             "Per-pixel time", true, ResourceFormat::R32Uint },
        { kOutputSpecularAlbedo,        "gOutputSpecularAlbedo",        "Output specular albedo (linear)", true, ResourceFormat::RGBA8Unorm },
        { kOutputIndirectAlbedo,        "gOutputIndirectAlbedo",        "Output indirect albedo (linear)", true, ResourceFormat::RGBA8Unorm },
        { kOutputReflectionPosW,        "gOutputReflectionPosW",        "Output reflection pos (world space)", true, ResourceFormat::RGBA32Float },
        { kOutputNRDDiffuseRadianceHitDist,     "gOutputNRDDiffuseRadianceHitDist",     "Output demodulated diffuse color (linear) and hit distance", true, ResourceFormat::RGBA32Float },
        { kOutputNRDSpecularRadianceHitDist,    "gOutputNRDSpecularRadianceHitDist",    "Output demodulated specular color (linear) and hit distance", true, ResourceFormat::RGBA32Float },
        { kOutputNRDResidualRadianceHitDist,    "gOutputNRDResidualRadianceHitDist",    "Output residual color (linear) and hit distance", true, ResourceFormat::RGBA32Float },
        { kOutputNRDEmission,                   "gOutputNRDEmission",                   "Output primary surface emission", true, ResourceFormat::RGBA32Float },
        { kOutputNRDDiffuseReflectance,         "gOutputNRDDiffuseReflectance",         "Output primary surface diffuse reflectance", true, ResourceFormat::RGBA16Float },
        { kOutputNRDSpecularReflectance,        "gOutputNRDSpecularReflectance",        "Output primary surface specular reflectance", true, ResourceFormat::RGBA16Float },
    };

    const Gui::DropdownList kColorFormatList =
    {
        { (uint32_t)(uint32_t)ColorFormat::RGBA32F, "RGBA32F (128bpp)" },
        { (uint32_t)(uint32_t)ColorFormat::LogLuvHDR, "LogLuvHDR (32bpp)" },
    };

    const Gui::DropdownList kMISHeuristicList =
    {
        { (uint32_t)(uint32_t)MISHeuristic::Balance, "Balance heuristic" },
        { (uint32_t)(uint32_t)MISHeuristic::PowerTwo, "Power heuristic (exp=2)" },
        { (uint32_t)(uint32_t)MISHeuristic::PowerExp, "Power heuristic" },
    };

    const Gui::DropdownList kShiftMappingList =
    {
        { (uint32_t)(uint32_t)ShiftMapping::Reconnection, "Reconnection" },
        { (uint32_t)(uint32_t)ShiftMapping::RandomReplay, "Random Replay" },
        { (uint32_t)(uint32_t)ShiftMapping::Hybrid, "Hybrid" },
    };

    const Gui::DropdownList kReSTIRMISList =
    {
        { (uint32_t)(uint32_t)ReSTIRMISKind::Constant, "Constant resampling MIS" },
        { (uint32_t)(uint32_t)ReSTIRMISKind::Talbot, "Talbot resampling MIS" },
        { (uint32_t)(uint32_t)ReSTIRMISKind::Pairwise, "Pairwise resampling MIS" },
        { (uint32_t)(uint32_t)ReSTIRMISKind::ConstantBinary, "Constant resampling MIS (1/|Z|)" },
        { (uint32_t)(uint32_t)ReSTIRMISKind::ConstantBiased, "Constant resampling MIS (biased)" },
    };

    const Gui::DropdownList kReSTIRMISList2 =
    {
        { (uint32_t)(uint32_t)ReSTIRMISKind::Constant, "Constant resampling MIS" },
        { (uint32_t)(uint32_t)ReSTIRMISKind::Talbot, "Talbot resampling MIS" },
        { (uint32_t)(uint32_t)ReSTIRMISKind::ConstantBinary, "Constant resampling MIS (1/|Z|)" },
        { (uint32_t)(uint32_t)ReSTIRMISKind::ConstantBiased, "Constant resampling MIS (biased)" },
    };

    const Gui::DropdownList kPathReusePatternList =
    {
        { (uint32_t)(uint32_t)PathReusePattern::Block, "Block" },
        { (uint32_t)(uint32_t)PathReusePattern::NRooks, "N-Rooks" },
        { (uint32_t)(uint32_t)PathReusePattern::NRooksShift, "N-Rooks Shift" },
    };

    const Gui::DropdownList kSpatialReusePatternList =
    {
        { (uint32_t)(uint32_t)SpatialReusePattern::Default, "Default" },
        { (uint32_t)(uint32_t)SpatialReusePattern::SmallWindow, "Small Window" },
    };

    const Gui::DropdownList kEmissiveSamplerList =
    {
        { (uint32_t)(uint32_t)EmissiveLightSamplerType::Uniform, "Uniform" },
        { (uint32_t)(uint32_t)EmissiveLightSamplerType::LightBVH, "LightBVH" },
        { (uint32_t)(uint32_t)EmissiveLightSamplerType::Power, "Power" },
    };

    const Gui::DropdownList kPathSamplingModeList =
    {
        { (uint32_t)(uint32_t)PathSamplingMode::ReSTIR, "ReSTIR PT" },
        { (uint32_t)(uint32_t)PathSamplingMode::PathReuse, "Bekaert-style Path Reuse" },
        { (uint32_t)(uint32_t)PathSamplingMode::PathTracing, "Path Tracing" }
    };

    const std::string kSamplesPerPixel = "samplesPerPixel";
    const std::string kMaxSurfaceBounces = "maxSurfaceBounces";
    const std::string kMaxDiffuseBounces = "maxDiffuseBounces";
    const std::string kMaxSpecularBounces = "maxSpecularBounces";
    const std::string kMaxTransmissionBounces = "maxTransmissionBounces";
    const std::string kAdjustShadingNormals = "adjustShadingNormals";
    const std::string kLODBias = "lodBias";
    const std::string kSampleGenerator = "sampleGenerator";
    const std::string kUseBSDFSampling = "useBSDFSampling";
    const std::string kUseNEE = "useNEE";
    const std::string kUseMIS = "useMIS";
    const std::string kUseRussianRoulette = "useRussianRoulette";
    const std::string kUseAlphaTest = "useAlphaTest";
    const std::string kMaxNestedMaterials = "maxNestedMaterials";
    const std::string kUseLightsInDielectricVolumes = "useLightsInDielectricVolumes";
    const std::string kLimitTransmission = "limitTransmission";
    const std::string kMaxTransmissionReflectionDepth = "maxTransmissionReflectionDepth";
    const std::string kMaxTransmissionRefractionDepth = "maxTransmissionRefractionDepth";
    const std::string kDisableCaustics = "disableCaustics";
    const std::string kSpecularRoughnessThreshold = "specularRoughnessThreshold";
    const std::string kDisableDirectIllumination = "disableDirectIllumination";
    const std::string kColorFormat = "colorFormat";
    const std::string kMISHeuristic = "misHeuristic";
    const std::string kMISPowerExponent = "misPowerExponent";
    const std::string kFixedSeed = "fixedSeed";
    const std::string kEmissiveSampler = "emissiveSampler";
    const std::string kLightBVHOptions = "lightBVHOptions";
    const std::string kPrimaryLodMode = "primaryLodMode";
    const std::string kUseNRDDemodulation = "useNRDDemodulation";
    const std::string kSpatialMisKind = "spatialMisKind";
    const std::string kTemporalMisKind = "temporalMisKind";
    const std::string kShiftStrategy = "shiftStrategy";
    const std::string kRejectShiftBasedOnJacobian = "rejectShiftBasedOnJacobian";
    const std::string kJacobianRejectionThreshold = "jacobianRejectionThreshold";
    const std::string kNearFieldDistance = "nearFieldDistance";
    const std::string kLocalStrategyType = "localStrategyType";
    const std::string kTemporalHistoryLength = "temporalHistoryLength";
    const std::string kUseMaxHistory = "useMaxHistory";
    const std::string kSeedOffset = "seedOffset";
    const std::string kEnableTemporalReuse = "enableTemporalReuse";
    const std::string kEnableSpatialReuse = "enableSpatialReuse";
    const std::string kNumSpatialRounds = "numSpatialRounds";
    const std::string kPathSamplingMode = "pathSamplingMode";
    const std::string kEnableTemporalReprojection = "enableTemporalReprojection";
    const std::string kNoResamplingForTemporalReuse = "noResamplingForTemporalReuse";
    const std::string kSpatialNeighborCount = "spatialNeighborCount";
    const std::string kFeatureBasedRejection = "featureBasedRejection";
    const std::string kSpatialReusePattern = "spatialReusePattern";
    const std::string kSmallWindowRestirWindowRadius = "smallWindowRestirWindowRadius";
    const std::string kSpatialReuseRadius = "spatialReuseRadius";
    const std::string kUseDirectLighting = "useDirectLighting";
    const std::string kSeparatePathBSDF = "separatePathBSDF";
    const std::string kCandidateSamples = "candidateSamples";
    const std::string kTemporalUpdateForDynamicScene = "temporalUpdateForDynamicScene";

    const uint32_t kNeighborOffsetCount = 8192;

    static void regReSTIRPTPass(pybind11::module& m)
    {
        pybind11::enum_<ShiftMapping> shiftMapping(m, "ShiftMapping");
        shiftMapping.value("Reconnection", ShiftMapping::Reconnection);
        shiftMapping.value("RandomReplay", ShiftMapping::RandomReplay);
        shiftMapping.value("Hybrid", ShiftMapping::Hybrid);

        pybind11::enum_<ReSTIRMISKind> misKind(m, "ReSTIRMISKind");
        misKind.value("Constant", ReSTIRMISKind::Constant);
        misKind.value("Talbot", ReSTIRMISKind::Talbot);
        misKind.value("Pairwise", ReSTIRMISKind::Pairwise);
        misKind.value("ConstantBinary", ReSTIRMISKind::ConstantBinary);
        misKind.value("ConstantBiased", ReSTIRMISKind::ConstantBiased);

        pybind11::enum_<PathSamplingMode> pathSamplingMode(m, "PathSamplingMode");
        pathSamplingMode.value("ReSTIR", PathSamplingMode::ReSTIR);
        pathSamplingMode.value("PathReuse", PathSamplingMode::PathReuse);
        pathSamplingMode.value("PathTracing", PathSamplingMode::PathTracing);

        pybind11::enum_<SpatialReusePattern> spatialReusePattern(m, "SpatialReusePattern");
        spatialReusePattern.value("Default", SpatialReusePattern::Default);
        spatialReusePattern.value("SmallWindow", SpatialReusePattern::SmallWindow);

        pybind11::class_<ReSTIRPTPass, RenderPass, ref<ReSTIRPTPass>> pass(m, "ReSTIRPTPass");
    }
} // namespace

extern "C" FALCOR_API_EXPORT void registerPlugin(Falcor::PluginRegistry& registry)
{
    registry.registerClass<RenderPass, ReSTIRPTPass>();
    ScriptBindings::registerBinding(regReSTIRPTPass);
}

void ReSTIRPTPass::registerBindings(pybind11::module& m) {}

void ReSTIRPTPass::Init()
{
    mStaticParams = StaticParams();
    mParams = RestirPathTracerParams();
    mEnableTemporalReuse = true;
    mEnableSpatialReuse = true;
    mSpatialReusePattern = (uint32_t)SpatialReusePattern::Default;
    mPathReusePattern = (uint32_t)PathReusePattern::NRooksShift;
    mSmallWindowRestirWindowRadius = 2;
    mSpatialNeighborCount = 3;
    mSpatialReuseRadius = 20.f;
    mNumSpatialRounds = 1;
    mEnableTemporalReprojection = false;
    mUseMaxHistory = true;
    mUseDirectLighting = true;
    mTemporalHistoryLength = 20;
    mNoResamplingForTemporalReuse = false;
}

ReSTIRPTPass::ReSTIRPTPass(ref<Device> pDevice, const Properties& props)
    : RenderPass(pDevice)
{
    if (!mpDevice->isFeatureSupported(Device::SupportedFeatures::RaytracingTier1_1))
        FALCOR_THROW("Raytracing Tier 1.1 is not supported by the current device");

    Init();
    parseProperties(props);
    validateOptions();

    // Load N-rook patterns
    std::vector<uint8_t> NRookArray(65536);
    bool loadedNROOK = false;
    const char* searchPaths[] = {
        "RenderPasses/ReSTIRPTPass/Data/16RooksPattern256.txt",
        "Data/16RooksPattern256.txt",
        "../../../../../Source/RenderPasses/ReSTIRPTPass/Data/16RooksPattern256.txt",
    };
    for (auto sp : searchPaths)
    {
        std::filesystem::path fullpath;
        if (findFileInShaderDirectories(sp, fullpath))
        {
            FILE* f = fopen(fullpath.string().c_str(), "r");
            if (f)
            {
                for (int i = 0; i < 8192; i++)
                    for (int j = 0; j < 8; j++)
                    {
                        int temp1, temp2;
                        fscanf(f, "%d %d", &temp1, &temp2);
                        NRookArray[8 * i + j] = (temp2 << 4) | temp1;
                    }
                fclose(f);
                loadedNROOK = true;
                break;
            }
        }
    }
    if (loadedNROOK)
        mNRooksPatternBuffer = mpDevice->createBuffer(65536, ResourceBindFlags::ShaderResource, MemoryType::DeviceLocal, NRookArray.data());
    else
    {
        logWarning("Could not load 16RooksPattern256.txt. Path reuse mode will not work.");
        mNRooksPatternBuffer = mpDevice->createBuffer(65536, ResourceBindFlags::ShaderResource, MemoryType::DeviceLocal, nullptr);
    }

    mpSampleGenerator = SampleGenerator::create(mpDevice, mStaticParams.sampleGenerator);
    mpNeighborOffsets = createNeighborOffsetTexture(kNeighborOffsetCount);

    auto defines = mStaticParams.getDefines();

    mpGeneratePaths = ComputePass::create(mpDevice, kGeneratePathsFilename, "main", defines, false);
    mpReflectTypes = ComputePass::create(mpDevice, kReflectTypesFile, "main", defines, false);

    {
        ProgramDesc desc;
        desc.addShaderLibrary(kTracePassFilename).csEntry("main").setShaderModel(ShaderModel::SM6_5);
        mpTracePass = ComputePass::create(mpDevice, desc, defines, false);
    }
    {
        ProgramDesc desc;
        desc.addShaderLibrary(kSpatialPathRetraceFile).csEntry("main").setShaderModel(ShaderModel::SM6_5);
        mpSpatialPathRetracePass = ComputePass::create(mpDevice, desc, defines, false);
    }
    {
        ProgramDesc desc;
        desc.addShaderLibrary(kTemporalPathRetraceFile).csEntry("main").setShaderModel(ShaderModel::SM6_5);
        mpTemporalPathRetracePass = ComputePass::create(mpDevice, desc, defines, false);
    }
    {
        ProgramDesc desc;
        desc.addShaderLibrary(kSpatialReusePassFile).csEntry("main").setShaderModel(ShaderModel::SM6_5);
        mpSpatialReusePass = ComputePass::create(mpDevice, desc, defines, false);
    }
    {
        ProgramDesc desc;
        desc.addShaderLibrary(kTemporalReusePassFile).csEntry("main").setShaderModel(ShaderModel::SM6_5);
        mpTemporalReusePass = ComputePass::create(mpDevice, desc, defines, false);
    }
    {
        ProgramDesc desc;
        desc.addShaderLibrary(kComputePathReuseMISWeightsFile).csEntry("main").setShaderModel(ShaderModel::SM6_5);
        mpComputePathReuseMISWeightsPass = ComputePass::create(mpDevice, desc, defines, false);
    }

    mpCounters = mpDevice->createBuffer((size_t)Counters::kCount * sizeof(uint32_t), ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr);
    mpCountersReadback = mpDevice->createBuffer((size_t)Counters::kCount * sizeof(uint32_t), ResourceBindFlags::None, MemoryType::ReadBack, nullptr);
}

void ReSTIRPTPass::parseProperties(const Properties& props)
{
    for (const auto& [key, value] : props)
    {
        if (key == kSamplesPerPixel) mStaticParams.samplesPerPixel = value;
        else if (key == kMaxSurfaceBounces) mStaticParams.maxSurfaceBounces = value;
        else if (key == kMaxDiffuseBounces) mStaticParams.maxDiffuseBounces = value;
        else if (key == kMaxSpecularBounces) mStaticParams.maxSpecularBounces = value;
        else if (key == kMaxTransmissionBounces) mStaticParams.maxTransmissionBounces = value;
        else if (key == kAdjustShadingNormals) mStaticParams.adjustShadingNormals = value;
        else if (key == kLODBias) mParams.lodBias = value;
        else if (key == kSampleGenerator) mStaticParams.sampleGenerator = value;
        else if (key == kFixedSeed) { mParams.fixedSeed = value; mParams.useFixedSeed = true; }
        else if (key == kUseBSDFSampling) mStaticParams.useBSDFSampling = value;
        else if (key == kUseNEE) mStaticParams.useNEE = value;
        else if (key == kUseMIS) mStaticParams.useMIS = value;
        else if (key == kUseRussianRoulette) mStaticParams.useRussianRoulette = value;
        else if (key == kUseAlphaTest) mStaticParams.useAlphaTest = value;
        else if (key == kMaxNestedMaterials) mStaticParams.maxNestedMaterials = value;
        else if (key == kUseLightsInDielectricVolumes) mStaticParams.useLightsInDielectricVolumes = value;
        else if (key == kLimitTransmission) mStaticParams.limitTransmission = value;
        else if (key == kMaxTransmissionReflectionDepth) mStaticParams.maxTransmissionReflectionDepth = value;
        else if (key == kMaxTransmissionRefractionDepth) mStaticParams.maxTransmissionRefractionDepth = value;
        else if (key == kDisableCaustics) mStaticParams.disableCaustics = value;
        else if (key == kSpecularRoughnessThreshold) mParams.specularRoughnessThreshold = value;
        else if (key == kDisableDirectIllumination) mStaticParams.disableDirectIllumination = value;
        else if (key == kPrimaryLodMode) mStaticParams.primaryLodMode = value;
        else if (key == kUseNRDDemodulation) mStaticParams.useNRDDemodulation = value;
        else if (key == kColorFormat) mStaticParams.colorFormat = value;
        else if (key == kMISHeuristic) mStaticParams.misHeuristic = value;
        else if (key == kMISPowerExponent) mStaticParams.misPowerExponent = value;
        else if (key == kEmissiveSampler) mStaticParams.emissiveSampler = value;
        else if (key == kLightBVHOptions) mLightBVHOptions = value;
        else if (key == kSpatialMisKind) mStaticParams.spatialMisKind = value;
        else if (key == kTemporalMisKind) mStaticParams.temporalMisKind = value;
        else if (key == kShiftStrategy) mStaticParams.shiftStrategy = value;
        else if (key == kRejectShiftBasedOnJacobian) mParams.rejectShiftBasedOnJacobian = value;
        else if (key == kJacobianRejectionThreshold) mParams.jacobianRejectionThreshold = value;
        else if (key == kNearFieldDistance) mParams.nearFieldDistance = value;
        else if (key == kTemporalHistoryLength) mTemporalHistoryLength = value;
        else if (key == kUseMaxHistory) mUseMaxHistory = value;
        else if (key == kSeedOffset) mSeedOffset = value;
        else if (key == kEnableTemporalReuse) mEnableTemporalReuse = value;
        else if (key == kEnableSpatialReuse) mEnableSpatialReuse = value;
        else if (key == kNumSpatialRounds) mNumSpatialRounds = value;
        else if (key == kPathSamplingMode) mStaticParams.pathSamplingMode = value;
        else if (key == kLocalStrategyType) mParams.localStrategyType = value;
        else if (key == kEnableTemporalReprojection) mEnableTemporalReprojection = value;
        else if (key == kNoResamplingForTemporalReuse) mNoResamplingForTemporalReuse = value;
        else if (key == kSpatialNeighborCount) mSpatialNeighborCount = value;
        else if (key == kFeatureBasedRejection) mFeatureBasedRejection = value;
        else if (key == kSpatialReusePattern) mSpatialReusePattern = value;
        else if (key == kSmallWindowRestirWindowRadius) mSmallWindowRestirWindowRadius = value;
        else if (key == kSpatialReuseRadius) mSpatialReuseRadius = value;
        else if (key == kUseDirectLighting) mUseDirectLighting = value;
        else if (key == kSeparatePathBSDF) mStaticParams.separatePathBSDF = value;
        else if (key == kCandidateSamples) mStaticParams.candidateSamples = value;
        else if (key == kTemporalUpdateForDynamicScene) mStaticParams.temporalUpdateForDynamicScene = value;
        else logWarning("Unknown field '{}' in ReSTIRPTPass dictionary", key);
    }

    if (mStaticParams.maxDiffuseBounces == (uint32_t)-1) mStaticParams.maxDiffuseBounces = mStaticParams.maxSurfaceBounces;
    if (mStaticParams.maxSpecularBounces == (uint32_t)-1) mStaticParams.maxSpecularBounces = mStaticParams.maxSurfaceBounces;
    if (mStaticParams.maxTransmissionBounces == (uint32_t)-1) mStaticParams.maxTransmissionBounces = mStaticParams.maxSurfaceBounces;

    mOptionsChanged = true;
    mRecompile = true;
    mParams.frameCount = 0;
}

void ReSTIRPTPass::validateOptions()
{
    if (mParams.specularRoughnessThreshold < 0.f || mParams.specularRoughnessThreshold > 1.f)
    {
        logWarning("'specularRoughnessThreshold' clamping to range [0,1].");
        mParams.specularRoughnessThreshold = math::clamp(mParams.specularRoughnessThreshold, 0.f, 1.f);
    }
    if (mStaticParams.samplesPerPixel < 1 || mStaticParams.samplesPerPixel > kMaxSamplesPerPixel)
    {
        logWarning("'samplesPerPixel' clamping to [1, {}].", kMaxSamplesPerPixel);
        mStaticParams.samplesPerPixel = std::clamp(mStaticParams.samplesPerPixel, 1u, kMaxSamplesPerPixel);
    }
    auto clampBounces = [](uint32_t& b, const char* n) { if (b > kMaxBounces) { logWarning("'{}' clamping to {}", n, kMaxBounces); b = kMaxBounces; } };
    clampBounces(mStaticParams.maxSurfaceBounces, "maxSurfaceBounces");
    clampBounces(mStaticParams.maxDiffuseBounces, "maxDiffuseBounces");
    clampBounces(mStaticParams.maxSpecularBounces, "maxSpecularBounces");
    clampBounces(mStaticParams.maxTransmissionBounces, "maxTransmissionBounces");

    uint32_t minSurfaceBounces = std::max(mStaticParams.maxDiffuseBounces, std::max(mStaticParams.maxSpecularBounces, mStaticParams.maxTransmissionBounces));
    mStaticParams.maxSurfaceBounces = std::max(mStaticParams.maxSurfaceBounces, minSurfaceBounces);

    if (mStaticParams.maxTransmissionReflectionDepth > mStaticParams.maxTransmissionBounces)
        mStaticParams.maxTransmissionReflectionDepth = mStaticParams.maxTransmissionBounces;
    if (mStaticParams.maxTransmissionRefractionDepth > mStaticParams.maxTransmissionBounces)
        mStaticParams.maxTransmissionRefractionDepth = mStaticParams.maxTransmissionBounces;
    if (mStaticParams.primaryLodMode == TexLODMode::RayCones)
    {
        logWarning("Unsupported tex lod mode. Defaulting to Mip0.");
        mStaticParams.primaryLodMode = TexLODMode::Mip0;
    }
}

Properties ReSTIRPTPass::getProperties() const
{
    Properties d;
    d[kSamplesPerPixel] = mStaticParams.samplesPerPixel;
    d[kMaxSurfaceBounces] = mStaticParams.maxSurfaceBounces;
    d[kMaxDiffuseBounces] = mStaticParams.maxDiffuseBounces;
    d[kMaxSpecularBounces] = mStaticParams.maxSpecularBounces;
    d[kMaxTransmissionBounces] = mStaticParams.maxTransmissionBounces;
    d[kAdjustShadingNormals] = mStaticParams.adjustShadingNormals;
    d[kLODBias] = mParams.lodBias;
    d[kSampleGenerator] = mStaticParams.sampleGenerator;
    if (mParams.useFixedSeed) d[kFixedSeed] = mParams.fixedSeed;
    d[kUseBSDFSampling] = mStaticParams.useBSDFSampling;
    d[kUseNEE] = mStaticParams.useNEE;
    d[kUseMIS] = mStaticParams.useMIS;
    d[kUseRussianRoulette] = mStaticParams.useRussianRoulette;
    d[kUseAlphaTest] = mStaticParams.useAlphaTest;
    d[kMaxNestedMaterials] = mStaticParams.maxNestedMaterials;
    d[kUseLightsInDielectricVolumes] = mStaticParams.useLightsInDielectricVolumes;
    d[kLimitTransmission] = mStaticParams.limitTransmission;
    d[kMaxTransmissionReflectionDepth] = mStaticParams.maxTransmissionReflectionDepth;
    d[kMaxTransmissionRefractionDepth] = mStaticParams.maxTransmissionRefractionDepth;
    d[kDisableCaustics] = mStaticParams.disableCaustics;
    d[kSpecularRoughnessThreshold] = mParams.specularRoughnessThreshold;
    d[kDisableDirectIllumination] = mStaticParams.disableDirectIllumination;
    d[kPrimaryLodMode] = mStaticParams.primaryLodMode;
    d[kColorFormat] = mStaticParams.colorFormat;
    d[kMISHeuristic] = mStaticParams.misHeuristic;
    d[kMISPowerExponent] = mStaticParams.misPowerExponent;
    d[kEmissiveSampler] = mStaticParams.emissiveSampler;
    d[kSpatialMisKind] = mStaticParams.spatialMisKind;
    d[kTemporalMisKind] = mStaticParams.temporalMisKind;
    d[kShiftStrategy] = mStaticParams.shiftStrategy;
    d[kRejectShiftBasedOnJacobian] = mParams.rejectShiftBasedOnJacobian;
    d[kJacobianRejectionThreshold] = mParams.jacobianRejectionThreshold;
    d[kNearFieldDistance] = mParams.nearFieldDistance;
    d[kTemporalHistoryLength] = mTemporalHistoryLength;
    d[kUseMaxHistory] = mUseMaxHistory;
    d[kSeedOffset] = mSeedOffset;
    d[kEnableTemporalReuse] = mEnableTemporalReuse;
    d[kEnableSpatialReuse] = mEnableSpatialReuse;
    d[kNumSpatialRounds] = mNumSpatialRounds;
    d[kPathSamplingMode] = mStaticParams.pathSamplingMode;
    d[kLocalStrategyType] = mParams.localStrategyType;
    d[kEnableTemporalReprojection] = mEnableTemporalReprojection;
    d[kNoResamplingForTemporalReuse] = mNoResamplingForTemporalReuse;
    d[kSpatialNeighborCount] = mSpatialNeighborCount;
    d[kFeatureBasedRejection] = mFeatureBasedRejection;
    d[kSpatialReusePattern] = mSpatialReusePattern;
    d[kSmallWindowRestirWindowRadius] = mSmallWindowRestirWindowRadius;
    d[kSpatialReuseRadius] = mSpatialReuseRadius;
    d[kUseDirectLighting] = mUseDirectLighting;
    d[kSeparatePathBSDF] = mStaticParams.separatePathBSDF;
    d[kCandidateSamples] = mStaticParams.candidateSamples;
    d[kTemporalUpdateForDynamicScene] = mStaticParams.temporalUpdateForDynamicScene;
    d[kUseNRDDemodulation] = mStaticParams.useNRDDemodulation;
    return d;
}

RenderPassReflection ReSTIRPTPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    addRenderPassInputs(reflector, kInputChannels);
    addRenderPassOutputs(reflector, kOutputChannels);
    return reflector;
}

void ReSTIRPTPass::compile(RenderContext* pContext, const CompileData& compileData)
{
    mParams.frameDim = compileData.defaultTexDims;
    if (mParams.frameDim.x > kMaxFrameDimension || mParams.frameDim.y > kMaxFrameDimension)
        logWarning("Frame dimensions up to {} pixels width/height are supported.", kMaxFrameDimension);

    FALCOR_ASSERT(isPowerOf2(kScreenTileDim.x) && isPowerOf2(kScreenTileDim.y));
    FALCOR_ASSERT(kScreenTileDim.x == (1u << kScreenTileBits.x) && kScreenTileDim.y == (1u << kScreenTileBits.y));
    mParams.screenTiles = div_round_up(mParams.frameDim, kScreenTileDim);
    mVarsChanged = true;
}

void ReSTIRPTPass::setScene(RenderContext* pRenderContext, const ref<Scene>& pScene)
{
    mpScene = pScene;
    mParams.frameCount = 0;
    resetLighting();

    if (mpScene)
    {
        if (mpScene->hasProceduralGeometry())
            logWarning("This render pass does not support custom/procedural primitives.");

        bool enableRobust = mpScene->hasAnimation() && mpScene->isAnimated();
        mParams.rejectShiftBasedOnJacobian = enableRobust;
        mStaticParams.temporalUpdateForDynamicScene = enableRobust;

        DefineList defines = mpScene->getSceneDefines();
        mpGeneratePaths->getProgram()->addDefines(defines);
        mpTracePass->getProgram()->addDefines(defines);
        mpReflectTypes->getProgram()->addDefines(defines);
        mpSpatialPathRetracePass->getProgram()->addDefines(defines);
        mpTemporalPathRetracePass->getProgram()->addDefines(defines);
        mpSpatialReusePass->getProgram()->addDefines(defines);
        mpTemporalReusePass->getProgram()->addDefines(defines);
        mpComputePathReuseMISWeightsPass->getProgram()->addDefines(defines);

        validateOptions();
        mRecompile = true;
    }
    mOptionsChanged = true;
}

void ReSTIRPTPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (!beginFrame(pRenderContext, renderData)) return;
    renderData.getDictionary()["enableScreenSpaceReSTIR"] = mUseDirectLighting;

    bool skipTemporalReuse = mReservoirFrameCount == 0;
    if (mStaticParams.pathSamplingMode != (uint32_t)PathSamplingMode::ReSTIR) mStaticParams.candidateSamples = 1;
    if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse)
    {
        mStaticParams.shiftStrategy = (uint32_t)ShiftMapping::Reconnection;
        mEnableSpatialReuse = true;
    }
    if (mStaticParams.shiftStrategy == (uint32_t)ShiftMapping::Hybrid)
    {
        mStaticParams.spatialMisKind = (uint32_t)ReSTIRMISKind::Pairwise;
        mStaticParams.temporalMisKind = (uint32_t)ReSTIRMISKind::Talbot;
    }

    uint32_t numPasses = mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathTracing ? 1 : mStaticParams.samplesPerPixel;

    for (uint32_t restir_i = 0; restir_i < numPasses; restir_i++)
    {
        updatePrograms();
        prepareResources(pRenderContext, renderData);

        if (const auto& tex = renderData.getTexture(kOutputTime))
            pRenderContext->clearUAV(tex->getUAV().get(), uint4(0));

        {
            FALCOR_ASSERT(mpCounters);
            pRenderContext->clearUAV(mpCounters->getUAV().get(), uint4(0));

            auto traceVar = mpTracePass->getRootVar();
            traceVar["gPathTracer"]["gSppId"] = restir_i;
            traceVar["gPathTracer"]["gNumSpatialRounds"] = mNumSpatialRounds;

            if (restir_i == 0)
                generatePaths(pRenderContext, renderData, 0);

            tracePass(pRenderContext, renderData, mpTracePass, "tracePass", 0);
        }

        if (mStaticParams.pathSamplingMode != (uint32_t)PathSamplingMode::PathTracing)
        {
            if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::ReSTIR)
            {
                if (mEnableTemporalReuse && !skipTemporalReuse)
                {
                    if (mStaticParams.shiftStrategy == (uint32_t)ShiftMapping::Hybrid)
                        PathRetracePass(pRenderContext, restir_i, renderData, true, 0);
                    PathReusePass(pRenderContext, restir_i, renderData, true, 0, !mEnableSpatialReuse);
                }
            }
            else if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse)
            {
                PathReusePass(pRenderContext, restir_i, renderData, false, -1, false);
            }

            if (mEnableSpatialReuse)
            {
                for (int r = 0; r < mNumSpatialRounds; r++)
                {
                    if (mStaticParams.shiftStrategy == (uint32_t)ShiftMapping::Hybrid)
                        PathRetracePass(pRenderContext, restir_i, renderData, false, r);
                    PathReusePass(pRenderContext, restir_i, renderData, false, r, r == mNumSpatialRounds - 1);
                }
            }

            if (restir_i == numPasses - 1)
                mReservoirFrameCount++;

            if (mEnableTemporalReuse && mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::ReSTIR)
            {
                if (!mEnableSpatialReuse || mNumSpatialRounds % 2 == 0)
                    pRenderContext->copyResource(mpTemporalReservoirs[restir_i].get(), mpOutputReservoirs.get());
                if (restir_i == numPasses - 1)
                    pRenderContext->copyResource(mpTemporalVBuffer.get(), renderData.getTexture(kInputVBuffer).get());
            }
        }
        mParams.seed++;
    }

    mParams.frameCount++;
    endFrame(pRenderContext, renderData);
}

void ReSTIRPTPass::renderUI(Gui::Widgets& widget)
{
    bool dirty = false;
    dirty |= renderRenderingUI(widget);
    dirty |= renderDebugUI(widget);
    if (dirty) { validateOptions(); mOptionsChanged = true; }
}

ref<Texture> ReSTIRPTPass::createNeighborOffsetTexture(uint32_t sampleCount)
{
    std::unique_ptr<int8_t[]> offsets(new int8_t[sampleCount * 2]);
    const int R = 254;
    const float phi2 = 1.f / 1.3247179572447f;
    float u = 0.5f, v = 0.5f;
    for (uint32_t idx = 0; idx < sampleCount * 2;)
    {
        u += phi2; v += phi2 * phi2;
        if (u >= 1.f) u -= 1.f;
        if (v >= 1.f) v -= 1.f;
        float rSq = (u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f);
        if (rSq > 0.25f) continue;
        offsets[idx++] = int8_t((u - 0.5f) * R);
        offsets[idx++] = int8_t((v - 0.5f) * R);
    }
    return mpDevice->createTexture1D(sampleCount, ResourceFormat::RG8Snorm, 1, 1, offsets.get(), ResourceBindFlags::ShaderResource);
}

bool ReSTIRPTPass::renderRenderingUI(Gui::Widgets& widget)
{
    bool dirty = false;

    dirty |= widget.checkbox("Direct lighting (ReSTIR DI)", mUseDirectLighting);

    bool modeChanged = widget.dropdown("Path Sampling Mode", kPathSamplingModeList, mStaticParams.pathSamplingMode);
    if (modeChanged)
    {
        if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse)
        { mStaticParams.shiftStrategy = (uint32_t)ShiftMapping::Reconnection; mStaticParams.separatePathBSDF = false; }
        else mStaticParams.separatePathBSDF = true;
    }

    if (auto group = widget.group("Path Reuse Controls", true))
    {
        dirty |= modeChanged;
        if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::ReSTIR)
        {
            if (widget.button("Clean Reservoirs")) mReservoirFrameCount = 0;
            dirty |= widget.var("Candidate Samples", mStaticParams.candidateSamples, 1u, 64u);

            if (auto sg = widget.group("Shift Mapping Options", true))
            {
                if (widget.dropdown("Shift Mapping", kShiftMappingList, mStaticParams.shiftStrategy)) dirty = true;
                dirty |= widget.checkbox("Reject Shift based on Jacobian", mParams.rejectShiftBasedOnJacobian);
                if (mParams.rejectShiftBasedOnJacobian)
                    dirty |= widget.var("Shift rejection threshold", mParams.jacobianRejectionThreshold, 0.f, 100.f);
            }
        }

        if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::ReSTIR && mStaticParams.shiftStrategy == (uint32_t)ShiftMapping::Hybrid)
        {
            if (auto ls = widget.group("Local Strategies", true))
            {
                bool rc = mParams.localStrategyType & (uint32_t)(uint32_t)LocalStrategy::RoughnessCondition;
                bool dc = mParams.localStrategyType & (uint32_t)(uint32_t)LocalStrategy::DistanceCondition;
                dirty |= widget.checkbox("Roughness Condition", rc);
                dirty |= widget.checkbox("Distance Condition", dc);
                mParams.localStrategyType = (dc << ((uint32_t)(uint32_t)LocalStrategy::DistanceCondition - 1)) | (rc << ((uint32_t)(uint32_t)LocalStrategy::RoughnessCondition - 1));
            }
            if (auto ct = widget.group("Classification thresholds", true))
            {
                dirty |= widget.var("Near field distance", mParams.nearFieldDistance, 0.f, 100.f);
                dirty |= widget.var("Specular roughness threshold", mParams.specularRoughnessThreshold, 0.f, 1.f);
            }
        }

        if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::ReSTIR)
        {
            dirty |= widget.checkbox("Spatial Reuse", mEnableSpatialReuse);
            dirty |= widget.checkbox("Temporal Reuse", mEnableTemporalReuse);
        }

        if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse)
            dirty |= widget.dropdown("Path Reuse Pattern", kPathReusePatternList, mPathReusePattern);
        else if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::ReSTIR && mEnableSpatialReuse)
        {
            if (auto sc = widget.group("Spatial reuse controls", true))
            {
                dirty |= widget.var("Num Spatial Rounds", mNumSpatialRounds, 1, 5);
                dirty |= widget.dropdown("Spatial Reuse Pattern", kSpatialReusePatternList, mSpatialReusePattern);
                dirty |= widget.checkbox("Feature-based rejection", mFeatureBasedRejection);
                if (mSpatialReusePattern == (uint32_t)SpatialReusePattern::SmallWindow)
                    dirty |= widget.var("Window radius", mSmallWindowRestirWindowRadius, 0u, 32u);
                else
                {
                    dirty |= widget.var("Spatial Neighbor Count", mSpatialNeighborCount, 0, 6);
                    dirty |= widget.var("Spatial Reuse Radius", mSpatialReuseRadius, 0.f, 100.f);
                }
                dirty |= widget.dropdown("Spatial Resampling MIS", kReSTIRMISList, mStaticParams.spatialMisKind);
            }
        }

        if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::ReSTIR && mEnableTemporalReuse)
        {
            if (auto tc = widget.group("Temporal reuse controls", true))
            {
                dirty |= widget.var("Temporal History Length", mTemporalHistoryLength, 0, 100);
                dirty |= widget.checkbox("Use M capping", mUseMaxHistory);
                dirty |= widget.checkbox("Temporal Reprojection", mEnableTemporalReprojection);
                dirty |= widget.checkbox("Temporal Update for Dynamic Scenes", mStaticParams.temporalUpdateForDynamicScene);
                dirty |= widget.checkbox("Disable Resampling in Temporal Reuse", mNoResamplingForTemporalReuse);
                dirty |= widget.dropdown("Temporal Resampling MIS", kReSTIRMISList2, mStaticParams.temporalMisKind);
            }
        }
    }

    if (auto group = widget.group("Shared Path Sampler Options", true))
    {
        dirty |= widget.var("Samples/pixel", mStaticParams.samplesPerPixel, 1u, kMaxSamplesPerPixel);
        dirty |= widget.checkbox("Use Sampled BSDFs", mStaticParams.separatePathBSDF);
        if (widget.var("Max bounces (override all)", mStaticParams.maxSurfaceBounces, 0u, kMaxBounces))
        {
            mStaticParams.maxDiffuseBounces = mStaticParams.maxSurfaceBounces;
            mStaticParams.maxSpecularBounces = mStaticParams.maxSurfaceBounces;
            mStaticParams.maxTransmissionBounces = mStaticParams.maxSurfaceBounces;
            dirty = true;
        }
        dirty |= widget.var("Max diffuse bounces", mStaticParams.maxDiffuseBounces, 0u, kMaxBounces);
        dirty |= widget.var("Max specular bounces", mStaticParams.maxSpecularBounces, 0u, kMaxBounces);
        dirty |= widget.var("Max transmission bounces", mStaticParams.maxTransmissionBounces, 0u, kMaxBounces);

        if (widget.dropdown("Sample generator", SampleGenerator::getGuiDropdownList(), mStaticParams.sampleGenerator))
        { mpSampleGenerator = SampleGenerator::create(mpDevice, mStaticParams.sampleGenerator); dirty = true; }

        dirty |= widget.checkbox("BSDF importance sampling", mStaticParams.useBSDFSampling);
        dirty |= widget.checkbox("Disable direct illumination", mStaticParams.disableDirectIllumination);
        dirty |= widget.checkbox("Russian roulette", mStaticParams.useRussianRoulette);
        dirty |= widget.checkbox("Next-event estimation (NEE)", mStaticParams.useNEE);

        if (mStaticParams.useNEE)
        {
            dirty |= widget.checkbox("Multiple importance sampling (MIS)", mStaticParams.useMIS);
            if (mStaticParams.useMIS)
            {
                dirty |= widget.dropdown("MIS heuristic", kMISHeuristicList, mStaticParams.misHeuristic);
                if (mStaticParams.misHeuristic == (uint32_t)MISHeuristic::PowerExp)
                    dirty |= widget.var("MIS power exponent", mStaticParams.misPowerExponent, 0.01f, 10.f);
            }
            if (mpScene && mpScene->useEmissiveLights())
            {
                if (auto eg = widget.group("Emissive sampler"))
                {
                    if (widget.dropdown("Emissive sampler", kEmissiveSamplerList, mStaticParams.emissiveSampler))
                    { resetLighting(); dirty = true; }
                    if (mpEmissiveSampler && mpEmissiveSampler->renderUI(eg)) mOptionsChanged = true;
                }
            }
        }
    }

    if (auto group = widget.group("Material controls"))
    {
        dirty |= widget.checkbox("Alpha test", mStaticParams.useAlphaTest);
        dirty |= widget.checkbox("Adjust shading normals on secondary hits", mStaticParams.adjustShadingNormals);
        dirty |= widget.var("Max nested materials", mStaticParams.maxNestedMaterials, 2u, 4u);
        dirty |= widget.checkbox("Use deterministic BSDF", mStaticParams.useDeterministicBSDF);
        dirty |= widget.checkbox("Use lights in dielectric volumes", mStaticParams.useLightsInDielectricVolumes);
        dirty |= widget.checkbox("Limit transmission", mStaticParams.limitTransmission);
        if (mStaticParams.limitTransmission)
        {
            dirty |= widget.var("Max transmission reflection depth", mStaticParams.maxTransmissionReflectionDepth, 0u, kMaxBounces);
            dirty |= widget.var("Max transmission refraction depth", mStaticParams.maxTransmissionRefractionDepth, 0u, kMaxBounces);
        }
        dirty |= widget.checkbox("Disable caustics", mStaticParams.disableCaustics);
        dirty |= widget.var("TexLOD bias", mParams.lodBias, -16.f, 16.f, 0.01f);
    }

    if (auto group = widget.group("Denoiser options"))
        dirty |= widget.checkbox("Use NRD demodulation", mStaticParams.useNRDDemodulation);

    if (auto group = widget.group("Output options"))
        dirty |= widget.dropdown("Color format", kColorFormatList, mStaticParams.colorFormat);

    if (dirty) mRecompile = true;
    return dirty;
}

bool ReSTIRPTPass::renderDebugUI(Gui::Widgets& widget)
{
    bool dirty = false;
    if (auto group = widget.group("Debugging", true))
    {
        dirty |= group.checkbox("Use fixed seed", mParams.useFixedSeed);
        if (mParams.useFixedSeed) dirty |= group.var("Seed", mParams.fixedSeed);
    }
    return dirty;
}

void ReSTIRPTPass::updatePrograms()
{
    if (!mRecompile) return;

    mStaticParams.rcDataOfflineMode = mSpatialNeighborCount > 3 && mStaticParams.shiftStrategy == (uint32_t)ShiftMapping::Hybrid;
    auto defines = mStaticParams.getDefines();
    defines.add(mpSampleGenerator->getDefines());

    mpGeneratePaths->getProgram()->addDefines(defines);
    mpTracePass->getProgram()->addDefines(defines);
    mpReflectTypes->getProgram()->addDefines(defines);
    mpSpatialPathRetracePass->getProgram()->addDefines(defines);
    mpTemporalPathRetracePass->getProgram()->addDefines(defines);
    mpSpatialReusePass->getProgram()->addDefines(defines);
    mpTemporalReusePass->getProgram()->addDefines(defines);
    mpComputePathReuseMISWeightsPass->getProgram()->addDefines(defines);

    mpGeneratePaths->setVars(nullptr);
    mpTracePass->setVars(nullptr);
    mpReflectTypes->setVars(nullptr);
    mpSpatialPathRetracePass->setVars(nullptr);
    mpTemporalPathRetracePass->setVars(nullptr);
    mpSpatialReusePass->setVars(nullptr);
    mpTemporalReusePass->setVars(nullptr);
    mpComputePathReuseMISWeightsPass->setVars(nullptr);

    mVarsChanged = true;
    mRecompile = false;
}

void ReSTIRPTPass::prepareResources(RenderContext* pRenderContext, const RenderData& renderData)
{
    uint32_t tileCount = mParams.screenTiles.x * mParams.screenTiles.y;
    const uint32_t reservoirCount = tileCount * kScreenTileDim.x * kScreenTileDim.y;

    auto var = mpReflectTypes->getRootVar();

    if (mStaticParams.pathSamplingMode != (uint32_t)PathSamplingMode::PathTracing)
    {
        if (mStaticParams.shiftStrategy == (uint32_t)ShiftMapping::Hybrid && (!mReconnectionDataBuffer ||
            (mStaticParams.rcDataOfflineMode && mReconnectionDataBuffer->getStructSize() != 512) ||
            (!mStaticParams.rcDataOfflineMode && mReconnectionDataBuffer->getStructSize() != 256)))
        {
            mReconnectionDataBuffer = mpDevice->createStructuredBuffer(var["reconnectionDataBuffer"], reservoirCount,
                ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
        }
        if (mStaticParams.shiftStrategy != (uint32_t)ShiftMapping::Hybrid)
            mReconnectionDataBuffer = nullptr;

        uint32_t baseReservoirSize = 88;
        uint32_t pathTreeReservoirSize = 128;

        if (mpOutputReservoirs &&
            ((mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse && mpOutputReservoirs->getStructSize() != pathTreeReservoirSize) ||
             (mStaticParams.pathSamplingMode != (uint32_t)PathSamplingMode::PathReuse && mpOutputReservoirs->getStructSize() != baseReservoirSize) ||
             (mpTemporalReservoirs.size() != mStaticParams.samplesPerPixel && mStaticParams.pathSamplingMode != (uint32_t)PathSamplingMode::PathReuse)))
        {
            mpOutputReservoirs = mpDevice->createStructuredBuffer(var["outputReservoirs"], reservoirCount,
                ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
            if (mStaticParams.pathSamplingMode != (uint32_t)PathSamplingMode::PathReuse)
            {
                mpTemporalReservoirs.resize(mStaticParams.samplesPerPixel);
                for (uint32_t i = 0; i < mStaticParams.samplesPerPixel; i++)
                    mpTemporalReservoirs[i] = mpDevice->createStructuredBuffer(var["outputReservoirs"], reservoirCount,
                        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
            }
            mVarsChanged = true;
        }

        if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse)
        {
            if (!mPathReuseMISWeightBuffer)
            {
                mPathReuseMISWeightBuffer = mpDevice->createStructuredBuffer(var["misWeightBuffer"], reservoirCount,
                    ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
                mVarsChanged = true;
            }
            mpTemporalReservoirs.clear();
        }
        else mPathReuseMISWeightBuffer = nullptr;

        if (!mpOutputReservoirs || reservoirCount != mpOutputReservoirs->getElementCount())
        {
            mpOutputReservoirs = mpDevice->createStructuredBuffer(var["outputReservoirs"], reservoirCount,
                ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);

            if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse)
            {
                mPathReuseMISWeightBuffer = mpDevice->createStructuredBuffer(var["misWeightBuffer"], reservoirCount,
                    ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
            }
            else
            {
                mpTemporalReservoirs.resize(mStaticParams.samplesPerPixel);
                for (uint32_t i = 0; i < mStaticParams.samplesPerPixel; i++)
                    mpTemporalReservoirs[i] = mpDevice->createStructuredBuffer(var["outputReservoirs"], reservoirCount,
                        ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess, MemoryType::DeviceLocal, nullptr, false);
            }
            mVarsChanged = true;
        }
    }

    if (!mpTemporalVBuffer || mpTemporalVBuffer->getHeight() != mParams.frameDim.y || mpTemporalVBuffer->getWidth() != mParams.frameDim.x)
    {
        mpTemporalVBuffer = mpDevice->createTexture2D(mParams.frameDim.x, mParams.frameDim.y,
            mpScene->getHitInfo().getFormat(), 1, 1, nullptr,
            ResourceBindFlags::ShaderResource | ResourceBindFlags::UnorderedAccess);
    }
}

void ReSTIRPTPass::setNRDData(const ShaderVar& var, const RenderData& renderData) const
{
    var["primaryHitEmission"] = renderData.getTexture(kOutputNRDEmission);
    var["primaryHitDiffuseReflectance"] = renderData.getTexture(kOutputNRDDiffuseReflectance);
    var["primaryHitSpecularReflectance"] = renderData.getTexture(kOutputNRDSpecularReflectance);
}

void ReSTIRPTPass::resetLighting()
{
    if (auto lbv = dynamic_cast<LightBVHSampler*>(mpEmissiveSampler.get()))
        mLightBVHOptions = lbv->getOptions();
    mpEmissiveSampler = nullptr;
    mpEnvMapSampler = nullptr;
    mRecompile = true;
}

bool ReSTIRPTPass::prepareLighting(RenderContext* pRenderContext)
{
    bool lightingChanged = false;

    if (is_set(mpScene->getUpdates(), Scene::UpdateFlags::RenderSettingsChanged))
    { lightingChanged = true; mRecompile = true; }

    if (is_set(mpScene->getUpdates(), Scene::UpdateFlags::EnvMapChanged))
    { mpEnvMapSampler = nullptr; lightingChanged = true; mRecompile = true; }

    if (mpScene->useEnvLight())
    {
        if (!mpEnvMapSampler)
        { mpEnvMapSampler = std::make_unique<EnvMapSampler>(mpDevice, mpScene->getEnvMap()); lightingChanged = true; mRecompile = true; }
    }
    else if (mpEnvMapSampler)
    { mpEnvMapSampler = nullptr; lightingChanged = true; mRecompile = true; }

    if (mpScene->getRenderSettings().useEmissiveLights)
        mpScene->getLightCollection(pRenderContext);

    if (mpScene->useEmissiveLights())
    {
        if (!mpEmissiveSampler)
        {
            const auto& pLights = mpScene->getLightCollection(pRenderContext);
            FALCOR_ASSERT(pLights && pLights->getActiveLightCount(pRenderContext) > 0);
            switch (mStaticParams.emissiveSampler)
            {
            case (uint32_t)EmissiveLightSamplerType::Uniform:
                mpEmissiveSampler = std::make_unique<EmissiveUniformSampler>(pRenderContext, pLights); break;
            case (uint32_t)EmissiveLightSamplerType::LightBVH:
                mpEmissiveSampler = std::make_unique<LightBVHSampler>(pRenderContext, pLights, mLightBVHOptions); break;
            case (uint32_t)EmissiveLightSamplerType::Power:
                mpEmissiveSampler = std::make_unique<EmissivePowerSampler>(pRenderContext, pLights); break;
            default: logWarning("Unknown emissive light sampler type");
            }
            lightingChanged = true; mRecompile = true;
        }
    }
    else if (mpEmissiveSampler)
    {
        if (auto lbv = dynamic_cast<LightBVHSampler*>(mpEmissiveSampler.get()))
            mLightBVHOptions = lbv->getOptions();
        mpEmissiveSampler = nullptr;
        lightingChanged = true; mRecompile = true;
    }

    if (mpEmissiveSampler)
    {
        lightingChanged |= mpEmissiveSampler->update(pRenderContext, mpScene->getLightCollection(pRenderContext));
        auto defs = mpEmissiveSampler->getDefines();
        if (mpTracePass->getProgram()->addDefines(defs)) mRecompile = true;
        if (mpSpatialPathRetracePass->getProgram()->addDefines(defs)) mRecompile = true;
        if (mpTemporalPathRetracePass->getProgram()->addDefines(defs)) mRecompile = true;
        if (mpSpatialReusePass->getProgram()->addDefines(defs)) mRecompile = true;
        if (mpTemporalReusePass->getProgram()->addDefines(defs)) mRecompile = true;
        if (mpComputePathReuseMISWeightsPass->getProgram()->addDefines(defs)) mRecompile = true;
    }

    return lightingChanged;
}

void ReSTIRPTPass::setShaderData(const ShaderVar& var, const RenderData& renderData, bool isPathTracer, bool isPathGenerator) const
{
    if (mVarsChanged)
    {
        if (isPathTracer && mpEnvMapSampler) mpEnvMapSampler->bindShaderData(var["envMapSampler"]);
    }

    var["params"].setBlob(mParams);
    var["vbuffer"] = renderData.getTexture(kInputVBuffer);
    var["outputColor"] = renderData.getTexture(kOutputColor);

    if (mOutputNRDData && isPathTracer)
    {
        setNRDData(var["outputNRD"], renderData);
        var["outputNRDDiffuseRadianceHitDist"] = renderData.getTexture(kOutputNRDDiffuseRadianceHitDist);
        var["outputNRDSpecularRadianceHitDist"] = renderData.getTexture(kOutputNRDSpecularRadianceHitDist);
        var["outputNRDResidualRadianceHitDist"] = renderData.getTexture(kOutputNRDResidualRadianceHitDist);
    }

    if (isPathTracer)
    {
        var["isLastRound"] = !mEnableSpatialReuse && !mEnableTemporalReuse;
        var["useDirectLighting"] = mUseDirectLighting;
        var["kUseEnvLight"] = mpScene->useEnvLight();
        var["kUseEmissiveLights"] = mpScene->useEmissiveLights();
        var["kUseAnalyticLights"] = mpScene->useAnalyticLights();
    }
    else if (isPathGenerator)
    {
        var["kUseEnvBackground"] = mpScene->useEnvBackground();
    }

    if (auto outputDebug = var.findMember("outputDebug"); outputDebug.isValid())
        outputDebug = renderData.getTexture(kOutputDebug);
    if (auto outputTime = var.findMember("outputTime"); outputTime.isValid())
        outputTime = renderData.getTexture(kOutputTime);

    if (isPathTracer && mpEmissiveSampler)
        mpEmissiveSampler->bindShaderData(var["emissiveSampler"]);
}

bool ReSTIRPTPass::beginFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (mOptionsChanged) mReservoirFrameCount = 0;

    const auto& pOutputDebug = renderData.getTexture(kOutputDebug);
    if (pOutputDebug) pRenderContext->clearUAV(pOutputDebug->getUAV().get(), float4(0.f));

    if (mpScene == nullptr)
    {
        const auto& pOutputColor = renderData.getTexture(kOutputColor);
        FALCOR_ASSERT(pOutputColor);
        pRenderContext->clearUAV(pOutputColor->getUAV().get(), float4(0.f));
        return false;
    }

    bool lightingChanged = prepareLighting(pRenderContext);

    auto& dict = renderData.getDictionary();
    if (mOptionsChanged || lightingChanged)
    {
        auto flags = dict.getValue(kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
        if (mOptionsChanged) flags |= Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
        if (lightingChanged) flags |= Falcor::RenderPassRefreshFlags::LightingChanged;
        dict[Falcor::kRenderPassRefreshFlags] = flags;
        mOptionsChanged = false;
    }

    bool gbufferAdjustShadingNormals = dict.getValue(Falcor::kRenderPassGBufferAdjustShadingNormals, false);
    if (gbufferAdjustShadingNormals != mGBufferAdjustShadingNormals)
    { mGBufferAdjustShadingNormals = gbufferAdjustShadingNormals; mRecompile = true; }

    mOutputNRDData =
        renderData.getTexture(kOutputNRDDiffuseRadianceHitDist) != nullptr
        || renderData.getTexture(kOutputNRDSpecularRadianceHitDist) != nullptr
        || renderData.getTexture(kOutputNRDResidualRadianceHitDist) != nullptr
        || renderData.getTexture(kOutputNRDEmission) != nullptr
        || renderData.getTexture(kOutputNRDDiffuseReflectance) != nullptr
        || renderData.getTexture(kOutputNRDSpecularReflectance) != nullptr;

    mOutputTime = renderData.getTexture(kOutputTime) != nullptr;

    int initialPasses = mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathTracing ? 1 : mStaticParams.samplesPerPixel;
    mParams.seed = mParams.useFixedSeed ? mParams.fixedSeed : mSeedOffset + initialPasses * mParams.frameCount;

    return true;
}

void ReSTIRPTPass::endFrame(RenderContext* pRenderContext, const RenderData& renderData)
{
    mVarsChanged = false;
}

void ReSTIRPTPass::generatePaths(RenderContext* pRenderContext, const RenderData& renderData, int sampleId)
{
    FALCOR_PROFILE(pRenderContext, "generatePaths");

    const uint32_t tileSize = kScreenTileDim.x * kScreenTileDim.y;
    FALCOR_ASSERT(kScreenTileDim.x == 16 && kScreenTileDim.y == 16);
    FALCOR_ASSERT(kScreenTileBits.x <= 4 && kScreenTileBits.y <= 4);

    mpGeneratePaths->addDefine("OUTPUT_TIME", mOutputTime ? "1" : "0");
    mpGeneratePaths->addDefine("OUTPUT_NRD_DATA", mOutputNRDData ? "1" : "0");

    auto var = mpGeneratePaths->getRootVar()["CB"]["gPathGenerator"];
    setShaderData(var, renderData, false, true);

    mpScene->bindShaderDataForRaytracing(pRenderContext, mpGeneratePaths->getRootVar()["gScene"]);
    var["gSampleId"] = sampleId;

    mpGeneratePaths->execute(pRenderContext, uint3(mParams.screenTiles.x * tileSize, mParams.screenTiles.y, 1u));
}

void ReSTIRPTPass::tracePass(RenderContext* pRenderContext, const RenderData& renderData, const ref<ComputePass>& pass, const std::string& passName, int sampleID)
{
    FALCOR_PROFILE(pRenderContext, passName);

    bool outputDebug = renderData.getTexture(kOutputDebug) != nullptr;
    pass->addDefine("OUTPUT_TIME", mOutputTime ? "1" : "0");
    pass->addDefine("OUTPUT_DEBUG", outputDebug ? "1" : "0");
    pass->addDefine("OUTPUT_NRD_DATA", mOutputNRDData ? "1" : "0");

    auto var = pass->getRootVar();
    mpScene->bindShaderDataForRaytracing(pRenderContext, var["gScene"]);

    if (mVarsChanged) mpSampleGenerator->bindShaderData(var);

    setShaderData(var["gPathTracer"], renderData, true, false);
    var["gPathTracer"]["outputReservoirs"] = mpOutputReservoirs;
    var["gPathTracer"]["directLighting"] = renderData.getTexture(kInputDirectLighting);

    var["CB"]["gSampleId"] = sampleID;

    auto frameDim = renderData.getDefaultTextureDims();
    pass->execute(pRenderContext, uint3(frameDim, 1u));
}

void ReSTIRPTPass::PathReusePass(RenderContext* pRenderContext, uint32_t restir_i, const RenderData& renderData, bool isTemporalReuse, int spatialRoundId, bool isLastRound)
{
    bool isMISWeight = spatialRoundId == -1;

    FALCOR_PROFILE(pRenderContext, isTemporalReuse ? "temporalReuse" : (isMISWeight ? "MISWeightComputation" : "spatialReuse"));

    ref<ComputePass> pass = isMISWeight ? mpComputePathReuseMISWeightsPass : (isTemporalReuse ? mpTemporalReusePass : mpSpatialReusePass);

    if (isMISWeight) { spatialRoundId = 0; restir_i = 0; }

    FALCOR_ASSERT(kScreenTileDim.x == 16 && kScreenTileDim.y == 16);
    FALCOR_ASSERT(kScreenTileBits.x <= 4 && kScreenTileBits.y <= 4);

    pass->addDefine("OUTPUT_TIME", mOutputTime ? "1" : "0");
    pass->addDefine("TEMPORAL_REUSE", isTemporalReuse ? "1" : "0");
    pass->addDefine("OUTPUT_NRD_DATA", mOutputNRDData ? "1" : "0");

    auto var = pass->getRootVar()["CB"]["gPathReusePass"];
    setShaderData(var, renderData, false, false);

    var["outputReservoirs"] = spatialRoundId % 2 == 1 ? mpTemporalReservoirs[restir_i] : mpOutputReservoirs;

    if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse)
        var["nRooksPattern"] = mNRooksPatternBuffer;

    if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse)
        var["misWeightBuffer"] = mPathReuseMISWeightBuffer;
    else if (!isMISWeight)
        var["temporalReservoirs"] = spatialRoundId % 2 == 0 ? mpTemporalReservoirs[restir_i] : mpOutputReservoirs;
    var["reconnectionDataBuffer"] = mReconnectionDataBuffer;
    var["gNumSpatialRounds"] = mNumSpatialRounds;

    if (isTemporalReuse)
    {
        var["temporalVbuffer"] = mpTemporalVBuffer;
        var["motionVectors"] = renderData.getTexture(kInputMotionVectors);
        var["gEnableTemporalReprojection"] = mEnableTemporalReprojection;
        var["gNoResamplingForTemporalReuse"] = mNoResamplingForTemporalReuse;
        if (!mUseMaxHistory) var["gTemporalHistoryLength"] = 1e30f;
        else var["gTemporalHistoryLength"] = (float)mTemporalHistoryLength;
    }
    else
    {
        var["gSpatialReusePattern"] = mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse ? (uint32_t)mPathReusePattern : (uint32_t)mSpatialReusePattern;

        if (!isMISWeight)
        {
            var["gNeighborCount"] = mSpatialNeighborCount;
            var["gGatherRadius"] = mSpatialReuseRadius;
            var["gSpatialRoundId"] = spatialRoundId;
            var["gSmallWindowRadius"] = mSmallWindowRestirWindowRadius;
            var["gFeatureBasedRejection"] = mFeatureBasedRejection;
            var["neighborOffsets"] = mpNeighborOffsets;
        }

        if (mOutputNRDData && !isMISWeight)
        {
            var["outputNRDDiffuseRadianceHitDist"] = renderData.getTexture(kOutputNRDDiffuseRadianceHitDist);
            var["outputNRDSpecularRadianceHitDist"] = renderData.getTexture(kOutputNRDSpecularRadianceHitDist);
            var["outputNRDResidualRadianceHitDist"] = renderData.getTexture(kOutputNRDResidualRadianceHitDist);
            var["primaryHitEmission"] = renderData.getTexture(kOutputNRDEmission);
            var["gSppId"] = restir_i;
        }
    }

    if (!isMISWeight)
    {
        var["directLighting"] = renderData.getTexture(kInputDirectLighting);
        var["useDirectLighting"] = mUseDirectLighting;
    }
    var["gIsLastRound"] = mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse || isLastRound;

    mpScene->bindShaderDataForRaytracing(pRenderContext, pass->getRootVar()["gScene"]);
    auto tv = pass->getRootVar()["gPathTracer"];
    setShaderData(tv, renderData, true, false);
    tv["outputReservoirs"] = mpOutputReservoirs;
    tv["directLighting"] = renderData.getTexture(kInputDirectLighting);

    if (mVarsChanged) mpSampleGenerator->bindShaderData(pass->getRootVar());

    pass->execute(pRenderContext, uint3(mParams.screenTiles.x * kScreenTileDim.x, mParams.screenTiles.y * kScreenTileDim.y, 1u));
}

void ReSTIRPTPass::PathRetracePass(RenderContext* pRenderContext, uint32_t restir_i, const RenderData& renderData, bool temporalReuse, int spatialRoundId)
{
    FALCOR_PROFILE(pRenderContext, temporalReuse ? "temporalPathRetrace" : "spatialPathRetrace");
    ref<ComputePass> pass = temporalReuse ? mpTemporalPathRetracePass : mpSpatialPathRetracePass;

    FALCOR_ASSERT(kScreenTileDim.x == 16 && kScreenTileDim.y == 16);
    FALCOR_ASSERT(kScreenTileBits.x <= 4 && kScreenTileBits.y <= 4);

    pass->addDefine("OUTPUT_TIME", mOutputTime ? "1" : "0");
    pass->addDefine("TEMPORAL_REUSE", temporalReuse ? "1" : "0");

    auto var = pass->getRootVar()["CB"]["gPathRetracePass"];
    setShaderData(var, renderData, false, false);
    var["outputReservoirs"] = spatialRoundId % 2 == 1 ? mpTemporalReservoirs[restir_i] : mpOutputReservoirs;

    if (mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse)
        var["nRooksPattern"] = mNRooksPatternBuffer;

    var["temporalReservoirs"] = spatialRoundId % 2 == 0 ? mpTemporalReservoirs[restir_i] : mpOutputReservoirs;
    var["reconnectionDataBuffer"] = mReconnectionDataBuffer;
    var["gNumSpatialRounds"] = mNumSpatialRounds;

    if (temporalReuse)
    {
        var["temporalVbuffer"] = mpTemporalVBuffer;
        var["motionVectors"] = renderData.getTexture(kInputMotionVectors);
        var["gEnableTemporalReprojection"] = mEnableTemporalReprojection;
        var["gNoResamplingForTemporalReuse"] = mNoResamplingForTemporalReuse;
        if (!mUseMaxHistory) var["gTemporalHistoryLength"] = 1e30f;
        else var["gTemporalHistoryLength"] = (float)mTemporalHistoryLength;
    }
    else
    {
        var["gSpatialRoundId"] = spatialRoundId;
        var["neighborOffsets"] = mpNeighborOffsets;
        var["gGatherRadius"] = mSpatialReuseRadius;
        var["gNeighborCount"] = mSpatialNeighborCount;
        var["gSmallWindowRadius"] = mSmallWindowRestirWindowRadius;
        var["gSpatialReusePattern"] = mStaticParams.pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse ? (uint32_t)mPathReusePattern : (uint32_t)mSpatialReusePattern;
        var["gFeatureBasedRejection"] = mFeatureBasedRejection;
    }

    mpScene->bindShaderDataForRaytracing(pRenderContext, pass->getRootVar()["gScene"]);
    auto tv = pass->getRootVar()["gPathTracer"];
    setShaderData(tv, renderData, true, false);
    tv["outputReservoirs"] = mpOutputReservoirs;
    tv["directLighting"] = renderData.getTexture(kInputDirectLighting);

    if (mVarsChanged) mpSampleGenerator->bindShaderData(pass->getRootVar());

    pass->execute(pRenderContext, uint3(mParams.screenTiles.x * kScreenTileDim.x, mParams.screenTiles.y * kScreenTileDim.y, 1u));
}

DefineList ReSTIRPTPass::StaticParams::getDefines() const
{
    DefineList defines;

    defines.add("SAMPLES_PER_PIXEL", std::to_string(samplesPerPixel));
    defines.add("CANDIDATE_SAMPLES", std::to_string(candidateSamples));
    defines.add("MAX_SURFACE_BOUNCES", std::to_string(maxSurfaceBounces));
    defines.add("MAX_DIFFUSE_BOUNCES", std::to_string(maxDiffuseBounces));
    defines.add("MAX_SPECULAR_BOUNCES", std::to_string(maxSpecularBounces));
    defines.add("MAX_TRANSMISSON_BOUNCES", std::to_string(maxTransmissionBounces));
    defines.add("ADJUST_SHADING_NORMALS", adjustShadingNormals ? "1" : "0");
    defines.add("USE_BSDF_SAMPLING", useBSDFSampling ? "1" : "0");
    defines.add("USE_NEE", useNEE ? "1" : "0");
    defines.add("USE_MIS", useMIS ? "1" : "0");
    defines.add("USE_RUSSIAN_ROULETTE", useRussianRoulette ? "1" : "0");
    defines.add("USE_ALPHA_TEST", useAlphaTest ? "1" : "0");
    defines.add("USE_LIGHTS_IN_DIELECTRIC_VOLUMES", useLightsInDielectricVolumes ? "1" : "0");
    defines.add("LIMIT_TRANSMISSION", limitTransmission ? "1" : "0");
    defines.add("MAX_TRANSMISSION_REFLECTION_DEPTH", std::to_string(maxTransmissionReflectionDepth));
    defines.add("MAX_TRANSMISSION_REFRACTION_DEPTH", std::to_string(maxTransmissionRefractionDepth));
    defines.add("DISABLE_CAUSTICS", disableCaustics ? "1" : "0");
    defines.add("DISABLE_DIRECT_ILLUMINATION", disableDirectIllumination ? "1" : "0");
    defines.add("PRIMARY_LOD_MODE", std::to_string((uint32_t)primaryLodMode));
    defines.add("USE_NRD_DEMODULATION", useNRDDemodulation ? "1" : "0");
    defines.add("COLOR_FORMAT", std::to_string((uint32_t)colorFormat));
    defines.add("MIS_HEURISTIC", std::to_string((uint32_t)misHeuristic));
    defines.add("MIS_POWER_EXPONENT", std::to_string(misPowerExponent));
    defines.add("_USE_DETERMINISTIC_BSDF", useDeterministicBSDF ? "1" : "0");
    defines.add("NEIGHBOR_OFFSET_COUNT", std::to_string(kNeighborOffsetCount));
    defines.add("SHIFT_STRATEGY", std::to_string((uint32_t)shiftStrategy));
    defines.add("PATH_SAMPLING_MODE", std::to_string((uint32_t)pathSamplingMode));
    defines.add("_USE_LEGACY_SHADING_CODE", "0");
    defines.add("INTERIOR_LIST_SLOT_COUNT", std::to_string(maxNestedMaterials));
    defines.add("GBUFFER_ADJUST_SHADING_NORMALS", "0");
    defines.add("OUTPUT_GUIDE_DATA", "0");
    defines.add("OUTPUT_TIME", "0");
    defines.add("OUTPUT_NRD_DATA", "0");
    defines.add("OUTPUT_NRD_ADDITIONAL_DATA", "0");
    defines.add("SPATIAL_RESTIR_MIS_KIND", std::to_string((uint32_t)spatialMisKind));
    defines.add("TEMPORAL_RESTIR_MIS_KIND", std::to_string((uint32_t)temporalMisKind));
    defines.add("TEMPORAL_UPDATE_FOR_DYNAMIC_SCENE", temporalUpdateForDynamicScene ? "1" : "0");
    defines.add("BPR", pathSamplingMode == (uint32_t)PathSamplingMode::PathReuse ? "1" : "0");
    defines.add("SEPARATE_PATH_BSDF", separatePathBSDF ? "1" : "0");
    defines.add("RCDATA_PATH_NUM", rcDataOfflineMode ? "12" : "6");
    defines.add("RCDATA_PAD_SIZE", rcDataOfflineMode ? "2" : "1");

    return defines;
}
