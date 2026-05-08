from falcor import *

def render_graph_WorldSpaceReSTIRGI():
    g = RenderGraph("WorldSpaceReSTIRGI")

    GBufferRT = createPass("GBufferRT", {
        'samplePattern': 'Center',
        'sampleCount': 1,
        'texLOD': 'Mip0',
        'useAlphaTest': True,
    })
    g.addPass(GBufferRT, "GBufferRT")

    WorldSpaceReSTIRGIPass = createPass("WorldSpaceReSTIRGIPass")
    g.addPass(WorldSpaceReSTIRGIPass, "WorldSpaceReSTIRGIPass")

    AccumulatePass = createPass("AccumulatePass", {
        'enabled': False,
        'precisionMode': 'Single',
    })
    g.addPass(AccumulatePass, "AccumulatePass")

    ToneMapper = createPass("ToneMapper", {
        'autoExposure': False,
        'exposureCompensation': 0.0,
    })
    g.addPass(ToneMapper, "ToneMapper")

    g.addEdge("GBufferRT.vbuffer", "WorldSpaceReSTIRGIPass.vbuffer")
    g.addEdge("GBufferRT.depth", "WorldSpaceReSTIRGIPass.vDepth")
    g.addEdge("GBufferRT.guideNormalW", "WorldSpaceReSTIRGIPass.vNormW")

    g.addEdge("WorldSpaceReSTIRGIPass.outputColor", "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")

    g.markOutput("ToneMapper.dst")

    return g

WorldSpaceReSTIRGI = render_graph_WorldSpaceReSTIRGI()
try: m.addGraph(WorldSpaceReSTIRGI)
except NameError: None

m.loadScene('C:/code/MyFalcor/Falcor/media/test_scenes/cornell_box_bunny.pyscene')
