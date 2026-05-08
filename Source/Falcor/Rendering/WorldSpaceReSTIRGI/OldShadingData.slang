/***************************************************************************
 # Old-style ReSTIRShadingData for ReSTIR PT compatibility.
 # Provides the exact old ReSTIRShadingData struct that legacy BSDF code expects.
 **************************************************************************/
#pragma once

#define ROUGHNESS_THRESHOLD 0.01

#include "Utils/HostDeviceShared.slangh"
__exported import IBxDF;
import Scene.Material.MaterialData;
import Scene.Material.MaterialData;
import Utils.Geometry.GeometryHelpers;

/** Old-style ReSTIRShadingData struct matching old Falcor API.
    This is used by the legacy MaterialShading BSDF code.
*/
struct ReSTIRShadingData
{
    float3  posW;
    float3  V;
    float3  N;
    float3  T;
    float3  B;
    float2  uv;
    float   NdotV;
    float   normalCurvature;

    float3  faceN;
    bool    frontFacing;

    uint    materialID;
    uint    flags;

    float3  diffuse;
    float   opacity;
    float3  specular;
    float   linearRoughness;
    float3  transmission;
    float3  emissive;
    float   IoR;
    float   metallic;
    float   diffuseTransmission;
    float   specularTransmission;
    float   eta;

    static const uint kMaterialTypeOffset = 0;
    static const uint kOldMaterialTypeBits = 16;
    static const uint kActiveLobesOffset = kMaterialTypeOffset + kOldMaterialTypeBits;
    static const uint kDoubleSidedOffset = kActiveLobesOffset + kLobeTypeBits;
    static const uint kThinSurfaceOffset = kDoubleSidedOffset + 1;

    [mutating] void setMaterialType(MaterialType type) { flags = PACK_BITS(kOldMaterialTypeBits, kMaterialTypeOffset, flags, (uint)type); }
    MaterialType getMaterialType() { return MaterialType(EXTRACT_BITS(kOldMaterialTypeBits, kMaterialTypeOffset, flags)); }
    [mutating] void setActiveLobes(uint activeLobes) { flags = PACK_BITS(kLobeTypeBits, kActiveLobesOffset, flags, activeLobes); }
    uint getActiveLobes() { return EXTRACT_BITS(kLobeTypeBits, kActiveLobesOffset, flags); }
    [mutating] void setDoubleSided(bool doubleSided) { flags = PACK_BITS(1, kDoubleSidedOffset, flags, doubleSided ? 1 : 0); }
    bool isDoubleSided() { return flags & (1u << kDoubleSidedOffset); }
    [mutating] void setThinSurface(bool thinSurface) { flags = PACK_BITS(1, kThinSurfaceOffset, flags, thinSurface ? 1 : 0); }
    bool isThinSurface() { return flags & (1u << kThinSurfaceOffset); }

    float3 computeNewRayOrigin(bool viewside = true)
    {
        return computeRayOrigin(posW, (frontFacing == viewside) ? faceN : -faceN);
    }

    float3 fromLocal(float3 v) { return T * v.x + B * v.y + N * v.z; }
    float3 toLocal(float3 v) { return float3(dot(v, T), dot(v, B), dot(v, N)); }
};
