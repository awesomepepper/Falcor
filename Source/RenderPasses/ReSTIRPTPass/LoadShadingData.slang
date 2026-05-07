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
#include "Scene/SceneDefines.slangh"
import Scene.Scene;
__exported import Scene.HitInfo;
import OldShadingData;
import Scene.Material.BasicMaterialData;
import Scene.Material.TextureSampler;
import Utils.Math.MathHelpers;
import Utils.Math.ShadingFrame;
import Utils.Color.ColorHelpers;
import Rendering.Materials.TexLODHelpers;
import Params;

/** Compute derivatives at primary triangle hit.
*/
void computeDerivativesAtPrimaryTriangleHit(const TriangleHit hit, uint2 pixel, uint2 frameDim, out float2 ddx, out float2 ddy)
{
    VertexData v = gScene.getVertexData(hit);
    float hitT = length(v.posW - gScene.camera.getPosition());
    const float3 nonNormalizedRayDir = gScene.camera.computeNonNormalizedRayDirPinhole(pixel, frameDim);
    float3 rayDir = normalize(nonNormalizedRayDir);

    float3 dDdx, dDdy;
    computeRayDirectionDifferentials(nonNormalizedRayDir, gScene.camera.data.cameraU, gScene.camera.data.cameraV, frameDim, dDdx, dDdy);

    RayDiff rd = RayDiff(float3(0.f), float3(0.f), dDdx, dDdy);
    rd = rd.propagate(v.posW.xyz, rayDir, hitT, v.faceNormalW);

    const float4x4 worldMat = gScene.getWorldMatrix(hit.instanceID);
    const float3x3 worldInvTransposeMat = gScene.getInverseTransposeWorldMatrix(hit.instanceID);
    const uint3 vertexIndices = gScene.getIndices(hit.instanceID, hit.primitiveIndex);
    StaticVertexData vertices[3] = { gScene.getVertex(vertexIndices[0]), gScene.getVertex(vertexIndices[1]), gScene.getVertex(vertexIndices[2]) };
    float2 dBarydx, dBarydy;
    float3 unnormalizedN, normals[3];

    prepareRayDiffAtHitPoint(v, vertices, hit.getBarycentricWeights(), rayDir, worldMat, worldInvTransposeMat, rd, unnormalizedN, normals, dBarydx, dBarydy, ddx, ddy);
}

/** Setup vertex data based on hit point.
*/
VertexData loadVertexData(const HitInfo hit, const float3 viewDir, out uint materialID)
{
    VertexData v = {};
    materialID = {};

#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_TRIANGLE_MESH)
    if (hit.getType() == HitType::Triangle)
    {
        const TriangleHit triangleHit = hit.getTriangleHit();
        StaticVertexData vertices[3];
        v = gScene.getVertexData(triangleHit, vertices);
        materialID = gScene.getMaterialID(triangleHit.instanceID);
    }
#endif
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_DISPLACED_TRIANGLE_MESH)
    if (hit.getType() == HitType::DisplacedTriangle)
    {
        const DisplacedTriangleHit displacedTriangleHit = hit.getDisplacedTriangleHit();
        v = gScene.getVertexData(displacedTriangleHit, -viewDir);
        materialID = gScene.getMaterialID(displacedTriangleHit.instanceID);
    }
#endif
#if SCENE_HAS_GEOMETRY_TYPE(GEOMETRY_TYPE_CURVE)
    if (hit.getType() == HitType::Curve)
    {
        const CurveHit curveHit = hit.getCurveHit();
        v = gScene.getVertexDataFromCurve(curveHit);
        materialID = gScene.getMaterialID(curveHit.instanceID);
    }
#endif
    return v;
}

/** UE/Substance-style metallic estimate used by Falcor's spec/gloss fallback.
*/
float computeMetallicFromDiffuseSpecular(float3 diffuse, float3 specular)
{
    float d = luminance(diffuse);
    float s = luminance(specular);
    if (s == 0.f) return 0.f;

    float a = 0.04f;
    float b = s + d - 0.08f;
    float c = 0.04f - s;
    float root = sqrt(max(0.f, b * b - 0.16f * c));
    return saturate((root - b) * 12.5f);
}

/** Fill old-style ReSTIRShadingData from Falcor 8 material data.
*/
ReSTIRShadingData populateShadingData(const VertexData v, uint materialID, float3 viewDir, const float lod = 0.f)
{
    ReSTIRShadingData sd = {};

    // Geometry data
    sd.posW = v.posW;
    sd.uv = v.texC;
    sd.V = viewDir;
    sd.N = v.normalW;
    sd.faceN = v.faceNormalW;
    sd.frontFacing = dot(sd.V, sd.faceN) >= 0.f;
    sd.NdotV = sd.V.z;
    sd.normalCurvature = 0.f;

    // Tangent/bitangent
    bool valid;
    ShadingFrame frame = ShadingFrame::createSafe(v.normalW, v.tangentW, valid);
    sd.N = frame.N;
    sd.T = frame.T;
    sd.B = frame.B;
    sd.NdotV = dot(sd.N, sd.V);

    // Material data
    sd.materialID = materialID;
    const MaterialHeader header = gScene.materials.getMaterialHeader(materialID);

    const float materialIoR = header.getIoR();
    sd.IoR = materialIoR;
    sd.eta = sd.frontFacing ? (1.f / materialIoR) : materialIoR;
    sd.opacity = 1.f;
    sd.diffuse = float3(0.f);
    sd.specular = float3(0.04f);
    sd.linearRoughness = 1.f;
    sd.metallic = 0.f;
    sd.transmission = float3(1.f);
    sd.emissive = float3(0.f);
    sd.diffuseTransmission = 0.f;
    sd.specularTransmission = 0.f;

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
            sd.metallic = computeMetallicFromDiffuseSpecular(sd.diffuse, sd.specular);
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

    // Flags: material type, active lobes, double sided, thin surface
    sd.flags = 0;
    sd.setMaterialType(header.getMaterialType());
    sd.setActiveLobes(0xff);
    sd.setDoubleSided(header.isDoubleSided());
    sd.setThinSurface(header.isThinSurface());

    // Match the old Falcor ShadingData path. Transmissive StandardMaterial is
    // made double-sided on the host; backfacing hits must flip the shading
    // normal so the legacy BSDF receives wo in its positive hemisphere.
    if (!sd.frontFacing && sd.isDoubleSided())
    {
        sd.N = -sd.N;
        sd.NdotV = -sd.NdotV;
    }

    return sd;
}

/** Setup ReSTIRShadingData based on loaded vertex/material attributes for a hit point.
*/
ReSTIRShadingData loadShadingData(const HitInfo hit, const float3 viewDir, const bool isPrimary, const float lod = 0.f)
{
    uint materialID = {};
    VertexData v = loadVertexData(hit, viewDir, materialID);
    return populateShadingData(v, materialID, viewDir, lod);
}

/** Setup ReSTIRShadingData with previous vertex position for directional reuse.
*/
ReSTIRShadingData loadShadingDataWithPrevVertexPosition(const HitInfo hit, const float3 prevVertexPosition, const bool isPrimary, const float lod = 0.f)
{
    uint materialID = {};
    VertexData v = loadVertexData(hit, float3(0.f), materialID);
    float3 viewDir = normalize(prevVertexPosition - v.posW);
    return populateShadingData(v, materialID, viewDir, lod);
}

/** Setup ReSTIRShadingData based on loaded vertex/material attributes with footprint.
*/
ReSTIRShadingData loadShadingDataFootprint(const HitInfo hit, const float3 viewDir, const bool isPrimary, const float2 ddx, const float2 ddy)
{
    uint materialID = {};
    VertexData v = loadVertexData(hit, viewDir, materialID);
    return populateShadingData(v, materialID, viewDir);
}
