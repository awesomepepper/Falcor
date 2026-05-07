add_plugin(ReSTIR_FG)

target_sources(ReSTIR_FG PRIVATE
    ReSTIR_FG.h
    ReSTIR_FG.cpp
    Shader/Reservoir.slang
    Shader/SurfaceDataFG.slang
    Shader/FinalGatherData.slang
    Shader/Hash.slang
    Shader/TraceTransmissionDelta.rt.slang
    Shader/GenerateFinalGatherSamples.rt.slang
    Shader/GenerateGIPathSamples.rt.slang
    Shader/GeneratePhotons.rt.slang
    Shader/CollectPhotons.rt.slang
    Shader/ResamplingPass.cs.slang
    Shader/CausticResamplingPass.cs.slang
    Shader/FinalShading.cs.slang
    Shader/DirectAnalytic.cs.slang
)

target_copy_shaders(ReSTIR_FG RenderPasses/ReSTIR_FG)
target_source_group(ReSTIR_FG "RenderPasses")
