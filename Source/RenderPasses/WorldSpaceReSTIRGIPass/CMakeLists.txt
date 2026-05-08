add_plugin(WorldSpaceReSTIRGIPass)

target_sources(WorldSpaceReSTIRGIPass PRIVATE
    FinalShading.cs.slang
    LoadShadingData.slang
    Params.slang
    PathTracer.slang
    WorldSpaceReSTIRGIPass.cpp
    WorldSpaceReSTIRGIPass.h
    ReflectTypes.cs.slang
    TracePass.rt.slang
)

target_copy_shaders(WorldSpaceReSTIRGIPass RenderPasses/WorldSpaceReSTIRGIPass)

target_source_group(WorldSpaceReSTIRGIPass "RenderPasses")
