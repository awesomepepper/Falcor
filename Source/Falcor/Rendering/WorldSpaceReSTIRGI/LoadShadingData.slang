#include "Scene/SceneDefines.slangh"

import Scene.Scene;
import Scene.Material.BasicMaterialData;
import Scene.Material.TextureSampler;
import Utils.Color.ColorHelpers;
import Utils.Math.MathHelpers;
import Utils.Math.ShadingFrame;
import Rendering.Materials.TexLODHelpers;
__exported import Scene.HitInfo;
__exported import OldShadingData;

VertexData LoadVertexData(const HitInfo hit, const float3 viewDir, out uint materialID)
{
    VertexData vd = {};
    materialID = {};

#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_TRIANGLE_MESH)
    if (hit.getType() == HitType::Triangle)
    {
        const TriangleHit thit = hit.getTriangleHit();
        vd = gScene.getVertexData(thit);
        materialID = gScene.getMaterialID(thit.instanceID);
    }
#endif
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_DISPLACED_TRIANGLE_MESH)
    if (hit.getType() == HitType::DisplacedTriangle)
    {
        const DisplacedTriangleHit thit = hit.getDisplacedTriangleHit();
        vd = gScene.getVertexData(thit, -viewDir);
        materialID = gScene.getMaterialID(thit.instanceID);
    }
#endif
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_CURVE)
    if (hit.getType() == HitType::Curve)
    {
        const CurveHit curveHit = hit.getCurveHit();
        vd = gScene.getVertexDataFromCurve(curveHit);
        materialID = gScene.getMaterialID(curveHit.instanceID);
    }
#endif

    return vd;
}

VertexData LoadVertexData(const HitInfo hit)
{
    uint materialID = {};
    return LoadVertexData(hit, float3(0.f), materialID);
}

float ComputeMetallicFromDiffuseSpecular(float3 diffuse, float3 specular)
{
    float d = luminance(diffuse);
    float s = luminance(specular);
    if (s == 0.f) return 0.f;

    float b = s + d - 0.08f;
    float c = 0.04f - s;
    float root = sqrt(max(0.f, b * b - 0.16f * c));
    return saturate((root - b) * 12.5f);
}

void AdjustShadingNormalForPrimary(inout ReSTIRShadingData sd)
{
    float3 Ng = sd.frontFacing ? sd.faceN : -sd.faceN;
    float signN = dot(sd.N, Ng) >= 0.f ? 1.f : -1.f;
    float3 Ns = signN * sd.N;

    const float kCosThetaThreshold = 0.1f;
    float cosTheta = dot(sd.V, Ns);
    if (cosTheta <= kCosThetaThreshold)
    {
        float t = saturate(cosTheta * (1.f / kCosThetaThreshold));
        float tangentSign = dot(cross(sd.N, sd.T), sd.B) >= 0.f ? 1.f : -1.f;
        ShadingFrame frame = ShadingFrame(signN * lerp(Ng, Ns, t), float4(sd.T, tangentSign));
        sd.N = frame.N;
        sd.T = frame.T;
        sd.B = frame.B;
        sd.NdotV = dot(sd.N, sd.V);
    }
}

ReSTIRShadingData PopulateShadingData(const VertexData vd, uint materialID, float3 viewDir, const float lod, const bool adjustPrimaryNormal)
{
    ReSTIRShadingData sd = {};
    float3 V = normalize(-viewDir);

    sd.posW = vd.posW;
    sd.uv = vd.texC;
    sd.V = V;
    sd.faceN = vd.faceNormalW;
    sd.frontFacing = dot(sd.V, sd.faceN) >= 0.f;
    sd.normalCurvature = 0.f;

    bool valid;
    ShadingFrame frame = ShadingFrame::createSafe(vd.normalW, vd.tangentW, valid);
    sd.N = frame.N;
    sd.T = frame.T;
    sd.B = frame.B;
    sd.NdotV = dot(sd.N, sd.V);

    sd.materialID = materialID;
    sd.diffuse = float3(0.f);
    sd.opacity = 1.f;
    sd.specular = float3(0.04f);
    sd.linearRoughness = 1.f;
    sd.transmission = float3(1.f);
    sd.emissive = float3(0.f);
    sd.metallic = 0.f;
    sd.diffuseTransmission = 0.f;
    sd.specularTransmission = 0.f;

    const MaterialHeader header = gScene.materials.getMaterialHeader(materialID);
    const float materialIoR = header.getIoR();
    sd.IoR = materialIoR;
    sd.eta = sd.frontFacing ? (1.f / materialIoR) : materialIoR;

    if (gScene.materials.isBasicMaterial(materialID))
    {
        const BasicMaterialData data = gScene.materials.getBasicMaterialData(materialID);
        SamplerState samplerState = gScene.materials.getTextureSampler(header.getDefaultTextureSamplerID());
        ExplicitLodTextureSampler textureLOD = { lod };

        const float4 baseColor = gScene.materials.sampleTexture(data.texBaseColor, samplerState, sd.uv, data.baseColor, textureLOD);
        const float4 spec = gScene.materials.sampleTexture(data.texSpecular, samplerState, sd.uv, data.specular, textureLOD);

#if MATERIAL_SYSTEM_HAS_SPEC_GLOSS_MATERIALS
        if (data.getShadingModel() == ShadingModel::MetalRough)
#endif
        {
            float f = (materialIoR - 1.f) / (materialIoR + 1.f);
            float F0 = f * f;
            sd.diffuse = lerp(baseColor.rgb, float3(0.f), spec.b);
            sd.specular = lerp(float3(F0), baseColor.rgb, spec.b);
            sd.linearRoughness = spec.g;
            sd.metallic = spec.b;
        }
#if MATERIAL_SYSTEM_HAS_SPEC_GLOSS_MATERIALS
        else
        {
            sd.diffuse = baseColor.rgb;
            sd.specular = spec.rgb;
            sd.linearRoughness = 1.f - spec.a;
            sd.metallic = ComputeMetallicFromDiffuseSpecular(sd.diffuse, sd.specular);
        }
#endif

        sd.opacity = baseColor.a;
        sd.diffuseTransmission = data.diffuseTransmission;
        sd.specularTransmission = data.specularTransmission;
        sd.transmission = gScene.materials.sampleTexture(data.texTransmission, samplerState, sd.uv, float4(data.transmission, 0.f), textureLOD).rgb;

        if (sd.frontFacing)
        {
            sd.emissive = gScene.materials.sampleTexture(data.texEmissive, samplerState, sd.uv, float4(data.emissive, 1.f), textureLOD).rgb * data.emissiveFactor;
        }
    }

    sd.flags = 0;
    sd.setMaterialType(header.getMaterialType());
    sd.setActiveLobes(0xff);
    sd.setDoubleSided(header.isDoubleSided());
    sd.setThinSurface(header.isThinSurface());

    if (!sd.frontFacing && sd.isDoubleSided())
    {
        sd.N = -sd.N;
        sd.NdotV = -sd.NdotV;
    }

    if (adjustPrimaryNormal) AdjustShadingNormalForPrimary(sd);

    return sd;
}

ReSTIRShadingData LoadShadingData(const HitInfo hit, const float3 viewDir, const float lod, const bool isPrimary)
{
    uint materialID = {};
    VertexData vd = LoadVertexData(hit, viewDir, materialID);
    return PopulateShadingData(vd, materialID, viewDir, lod, isPrimary);
}
