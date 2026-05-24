from falcor import *

def render_graph_PathTracerGT():
    g = RenderGraph('PathTracerGT')

    # VBuffer: Use Stratified sample pattern for better primary ray quality
    g.create_pass('VBufferRT', 'VBufferRT', {
        'outputSize': 'Default',
        'samplePattern': 'Stratified',
        'sampleCount': 16,
        'useAlphaTest': True,
        'adjustShadingNormals': True,
        'forceCullMode': False,
        'cull': 'Back',
        'useTraceRayInline': False,
        'useDOF': True
    })

    # PathTracer: 1 spp per pixel, accumulate via AccumulatePass for high spp
    # maxSurfaceBounces set high to capture complex indirect illumination (caustics etc.)
    g.create_pass('PathTracer', 'PathTracer', {
        'samplesPerPixel': 1,
        'maxSurfaceBounces': 10,
        'useRussianRoulette': True
    })

    # AccumulatePass: enabled, Single precision, no frame limit (manual stop)
    # overflowMode = 'Stop': stop accumulation when maxFrameCount is reached
    # maxFrameCount = 0 means infinite accumulation
    g.create_pass('AccumulatePass', 'AccumulatePass', {
        'enabled': True,
        'outputSize': 'Default',
        'autoReset': True,
        'precisionMode': 'Single',
        'maxFrameCount': 0,
        'overflowMode': 'Stop'
    })

    # ToneMapper: Linear mapping, disable auto exposure for precise comparison
    g.create_pass('ToneMapper', 'ToneMapper', {
        'outputSize': 'Default',
        'useSceneMetadata': True,
        'exposureCompensation': 0.0,
        'autoExposure': False,
        'operator': 'Linear',
        'clamp': True,
        'fNumber': 1.0,
        'shutter': 1.0,
        'exposureMode': 'AperturePriority'
    })

    # Connect render graph
    g.add_edge('VBufferRT.vbuffer', 'PathTracer.vbuffer')
    g.add_edge('VBufferRT.viewW', 'PathTracer.viewW')
    g.add_edge('VBufferRT.mvec', 'PathTracer.mvec')
    g.add_edge('PathTracer.color', 'AccumulatePass.input')
    g.add_edge('AccumulatePass.output', 'ToneMapper.src')

    # Output: ToneMapper result for display, AccumulatePass raw HDR output for comparison
    g.mark_output('ToneMapper.dst')
    g.mark_output('AccumulatePass.output')

    return g

PathTracerGT = render_graph_PathTracerGT()
try: m.addGraph(PathTracerGT)
except NameError: None
