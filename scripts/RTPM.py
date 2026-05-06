from falcor import *

def render_graph_RTPM():
    g = RenderGraph("RTPM")
    g.create_pass('VBufferRT', 'VBufferRT', {
        'outputSize': 'Default',
        'samplePattern': 'Stratified',
        'sampleCount': 16,
        'useAlphaTest': True,
    })
    g.create_pass('RTPM', 'RTPM', {
        'usePhotonVisualization': False,
    })
    g.create_pass('AccumulatePass', 'AccumulatePass', {
        'enabled': True,
        'outputSize': 'Default',
        'autoReset': True,
        'precisionMode': 'Single',
        'maxFrameCount': 0,
        'overflowMode': 'Stop',
    })
    g.create_pass('ToneMapper', 'ToneMapper', {
        'outputSize': 'Default',
        'useSceneMetadata': True,
        'exposureCompensation': 0.0,
        'autoExposure': False,
        'filmSpeed': 100.0,
        'whiteBalance': False,
        'whitePoint': 6500.0,
        'operator': 'Aces',
        'clamp': True,
        'whiteMaxLuminance': 1.0,
        'whiteScale': 11.2,
        'fNumber': 1.0,
        'shutter': 1.0,
        'exposureMode': 'AperturePriority',
    })
    g.add_edge('VBufferRT.vbuffer', 'RTPM.vbuffer')
    g.add_edge('VBufferRT.viewW', 'RTPM.viewW')
    g.add_edge('RTPM.PhotonImage', 'AccumulatePass.input')
    g.add_edge('AccumulatePass.output', 'ToneMapper.src')
    g.mark_output('ToneMapper.dst')
    return g

RTPM = render_graph_RTPM()
try: m.addGraph(RTPM)
except NameError: None

m.loadScene('C:/code/ReSTIR/ReSTIR_PT_LinDaqi/ReSTIR_PT/Tests/test_scenes/cornell_box_bunny.pyscene')
