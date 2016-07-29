#include "../Lighting.hlsli"

#define BRDF_FROSTBITE 

struct Input {
	float4 mPosH : SV_POSITION;
	float3 mPosV : POS_VIEW;
	float3 mNormalV : NORMAL_VIEW;
};

struct Material {
	float4 mBaseColor_MetalMask;
	float4 mReflectance_Smoothness;
};
ConstantBuffer<Material> gMaterial : register(b0);

struct FrameConstants {
	float4x4 mV;
};
ConstantBuffer<FrameConstants> gFrameConstants : register(b1);

float4 main(const in Input input) : SV_TARGET{
	PunctualLight light;	
	light.mPosV = mul(float4(0.0f, 0.0f, 0.0f, 1.0f), gFrameConstants.mV).xyz;
	light.mRange = 8000.0f;
	light.mColor = float3(1.0f, 1.0f, 1.0f);
	light.mPower = 2000.0f;
	
	const float3 luminance = computeLuminance(light, input.mPosV);

	const float3 baseColor = gMaterial.mBaseColor_MetalMask.xyz;
	const float metalMask = gMaterial.mBaseColor_MetalMask.w;
	const float smoothness = gMaterial.mReflectance_Smoothness.w;
	const float3 reflectance = gMaterial.mReflectance_Smoothness.xyz;
	const float3 lightDirV = light.mPosV - input.mPosV;
	const float3 normalV = normalize(input.mNormalV);

	float3 illuminance;
#ifdef BRDF_FROSTBITE
	illuminance = brdf_FrostBite(normalV, normalize(-input.mPosV), normalize(lightDirV), baseColor, smoothness, reflectance, metalMask);
#else
	illuminance = brdf_CookTorrance(normalV, normalize(-input.mPosV), normalize(lightDirV), baseColor, smoothness, reflectance, metalMask);
#endif
	return float4(luminance * illuminance, 1.0f);
}