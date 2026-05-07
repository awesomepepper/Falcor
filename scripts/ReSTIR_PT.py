from falcor import *

def render_graph_ReSTIR_PT():
    g = RenderGraph("ReSTIR_PT")
    VBufferRT = createPass("VBufferRT")
    g.addPass(VBufferRT, "VBufferRT")
    RTXDIPass = createPass("RTXDIPass")
    g.addPass(RTXDIPass, "RTXDIPass")
    ReSTIRPTPass = createPass("ReSTIRPTPass", {'samplesPerPixel': 1})
    g.addPass(ReSTIRPTPass, "ReSTIRPTPass")
    AccumulatePass = createPass("AccumulatePass", {'enabled': False, 'precisionMode': 'Single'})
    g.addPass(AccumulatePass, "AccumulatePass")
    ToneMapper = createPass("ToneMapper", {'autoExposure': False, 'exposureCompensation': 0.0})
    g.addPass(ToneMapper, "ToneMapper")
    g.addEdge("VBufferRT.vbuffer", "RTXDIPass.vbuffer")
    g.addEdge("VBufferRT.mvec", "RTXDIPass.mvec")
    g.addEdge("VBufferRT.vbuffer", "ReSTIRPTPass.vbuffer")
    g.addEdge("VBufferRT.mvec", "ReSTIRPTPass.motionVectors")
    g.addEdge("RTXDIPass.color", "ReSTIRPTPass.directLighting")
    g.addEdge("ReSTIRPTPass.color", "AccumulatePass.input")
    g.addEdge("AccumulatePass.output", "ToneMapper.src")
    g.markOutput("ToneMapper.dst")
    return g

ReSTIR_PT = render_graph_ReSTIR_PT()
try: m.addGraph(ReSTIR_PT)
except NameError: None

m.loadScene('C:/code/ReSTIR/ReSTIR_PT_LinDaqi/ReSTIR_PT/Tests/test_scenes/cornell_box_bunny.pyscene')

