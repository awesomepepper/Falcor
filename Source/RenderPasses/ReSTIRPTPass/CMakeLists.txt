add_plugin(ReSTIRPTPass)

target_sources(ReSTIRPTPass PRIVATE
    ReSTIRPTPass.h
    ReSTIRPTPass.cpp
    Params.slang
    StaticParams.slang
    PathTracer.slang
    PathState.slang
    PathReservoir.slang
    PathBuilder.slang
    Shift.slang
    LoadShadingData.slang
    NRDHelpers.slang
    GeneratePaths.cs.slang
    TracePass.cs.slang
    SpatialReuse.cs.slang
    TemporalReuse.cs.slang
    SpatialPathRetrace.cs.slang
    TemporalPathRetrace.cs.slang
    ComputePathReuseMISWeights.cs.slang
    ReflectTypes.cs.slang
    OldShadingData.slang
    MaterialShading.slang
    BxDF.slang
    IBxDF.slang
    MaterialHelpers.slang
    BxDFConfig.slangh
    Microfacet.slang
)

target_copy_shaders(ReSTIRPTPass RenderPasses/ReSTIRPTPass)
target_source_group(ReSTIRPTPass "RenderPasses")
