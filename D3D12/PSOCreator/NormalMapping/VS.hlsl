#include "../ShaderUtils/CBuffers.hlsli"

struct Input {
	float3 mPosO : POSITION;
	float3 mNormalO : NORMAL;
	float3 mTangentO : TANGENT;
	float2 mTexCoordO : TEXCOORD;
};

ConstantBuffer<ObjectCBuffer> gObjConstants : register(b0);
ConstantBuffer<FrameCBuffer> gFrameConstants : register(b1);

struct Output {
	float4 mPosH : SV_POSITION;
	float3 mPosV : POS_VIEW;
	float3 mNormalV : NORMAL_VIEW;
	float3 mTangentV : TANGENT;
	float3 mBinormalV : BINORMAL;
	float2 mTexCoordO : TEXCOORD;
};

Output main(in const Input input) {
	const float4x4 wv = mul(gObjConstants.mW, gFrameConstants.mV);

	Output output;
	output.mPosV = mul(float4(input.mPosO, 1.0f), wv).xyz;
	output.mNormalV = mul(float4(input.mNormalO, 0.0f), wv).xyz;
	output.mPosH = mul(float4(output.mPosV, 1.0f), gFrameConstants.mP);
	output.mTexCoordO = gObjConstants.mTexTransform * input.mTexCoordO;
	output.mTangentV = normalize(mul(float4(normalize(input.mTangentO), 0.0f), wv).xyz);
	output.mBinormalV = normalize(cross(output.mNormalV, output.mTangentV));

	return output;
}