#include "HeightCmdListRecorder.h"

#include <DirectXMath.h>

#include <DescriptorManager\CbvSrvUavDescriptorManager.h>
#include <DirectXManager\DirectXManager.h>
#include <MaterialManager/Material.h>
#include <MathUtils/MathUtils.h>
#include <PSOManager/PSOManager.h>
#include <ResourceManager/ResourceManager.h>
#include <ResourceManager/UploadBuffer.h>
#include <ShaderUtils\CBuffers.h>
#include <Utils/DebugUtils.h>

// Root signature:
// "DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_VERTEX), " \ 0 -> Object CBuffers
// "CBV(b1, visibility = SHADER_VISIBILITY_VERTEX), " \ 1 -> Frame CBuffer
// "CBV(b0, visibility = SHADER_VISIBILITY_DOMAIN), " \ 2 -> Frame CBuffer
// "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_DOMAIN), " \ 3 -> Height Texture
// "DescriptorTable(CBV(b0), visibility = SHADER_VISIBILITY_PIXEL), " \ 4 -> Material CBuffers
// "CBV(b1, visibility = SHADER_VISIBILITY_PIXEL), " \ 5 -> Frame CBuffer
// "DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), " \ 6 -> Diffuse Texture
// "DescriptorTable(SRV(t1), visibility = SHADER_VISIBILITY_PIXEL), " \ 7 -> Normal Texture

namespace {
	ID3D12PipelineState* sPSO{ nullptr };
	ID3D12RootSignature* sRootSign{ nullptr };
}

void HeightCmdListRecorder::InitPSO(const DXGI_FORMAT* geometryBufferFormats, const std::uint32_t geometryBufferCount) noexcept {
	ASSERT(geometryBufferFormats != nullptr);
	ASSERT(geometryBufferCount > 0U);
	ASSERT(sPSO == nullptr);
	ASSERT(sRootSign == nullptr);

	// Build pso and root signature
	PSOManager::PSOCreationData psoData{};
	psoData.mDSFilename = "GeometryPass/Shaders/HeightMapping/DS.cso";
	psoData.mHSFilename = "GeometryPass/Shaders/HeightMapping/HS.cso";
	psoData.mInputLayout = D3DFactory::GetPosNormalTangentTexCoordInputLayout();
	psoData.mPSFilename = "GeometryPass/Shaders/HeightMapping/PS.cso";
	psoData.mRootSignFilename = "GeometryPass/Shaders/HeightMapping/RS.cso";
	psoData.mVSFilename = "GeometryPass/Shaders/HeightMapping/VS.cso";
	psoData.mTopology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	psoData.mNumRenderTargets = geometryBufferCount;
	memcpy(psoData.mRtFormats, geometryBufferFormats, sizeof(DXGI_FORMAT) * psoData.mNumRenderTargets);
	PSOManager::Get().CreateGraphicsPSO(psoData, sPSO, sRootSign);

	ASSERT(sPSO != nullptr);
	ASSERT(sRootSign != nullptr);
}

void HeightCmdListRecorder::Init(
	const GeometryData* geometryDataVec,
	const std::uint32_t numGeomData,
	const Material* materials,
	ID3D12Resource** textures,
	ID3D12Resource** normals,
	ID3D12Resource** heights,
	const std::uint32_t numResources) noexcept
{
	ASSERT(IsDataValid() == false);
	ASSERT(geometryDataVec != nullptr);
	ASSERT(numGeomData != 0U);
	ASSERT(materials != nullptr);
	ASSERT(numResources > 0UL);
	ASSERT(textures != nullptr);
	ASSERT(normals != nullptr);
	ASSERT(heights != nullptr);

	// Check that the total number of matrices (geometry to be drawn) will be equal to available materials
#ifdef _DEBUG
	std::size_t totalNumMatrices{ 0UL };
	for (std::size_t i = 0UL; i < numGeomData; ++i) {
		const std::size_t numMatrices{ geometryDataVec[i].mWorldMatrices.size() };
		totalNumMatrices += numMatrices;
		ASSERT(numMatrices != 0UL);
	}
	ASSERT(totalNumMatrices == numResources);
#endif
	mGeometryDataVec.reserve(numGeomData);
	for (std::uint32_t i = 0U; i < numGeomData; ++i) {
		mGeometryDataVec.push_back(geometryDataVec[i]);
	}

	BuildBuffers(materials, textures, normals, heights, numResources);

	ASSERT(IsDataValid());
}

void HeightCmdListRecorder::RecordAndPushCommandLists(const FrameCBuffer& frameCBuffer) noexcept {
	ASSERT(IsDataValid());
	ASSERT(sPSO != nullptr);
	ASSERT(sRootSign != nullptr);
	ASSERT(mCmdListQueue != nullptr);
	ASSERT(mGeometryBuffersCpuDescs != nullptr);
	ASSERT(mGeometryBuffersCpuDescCount != 0U);
	ASSERT(mDepthBufferCpuDesc.ptr != 0U);

	ID3D12CommandAllocator* cmdAlloc{ mCmdAlloc[mCurrFrameIndex] };
	ASSERT(cmdAlloc != nullptr);

	// Update frame constants
	UploadBuffer& uploadFrameCBuffer(*mFrameCBuffer[mCurrFrameIndex]);
	uploadFrameCBuffer.CopyData(0U, &frameCBuffer, sizeof(frameCBuffer));

	CHECK_HR(cmdAlloc->Reset());
	CHECK_HR(mCmdList->Reset(cmdAlloc, sPSO));

	mCmdList->RSSetViewports(1U, &SettingsManager::sScreenViewport);
	mCmdList->RSSetScissorRects(1U, &SettingsManager::sScissorRect);
	mCmdList->OMSetRenderTargets(mGeometryBuffersCpuDescCount, mGeometryBuffersCpuDescs, false, &mDepthBufferCpuDesc);

	ID3D12DescriptorHeap* heaps[] = { &CbvSrvUavDescriptorManager::Get().GetDescriptorHeap() };
	mCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
	mCmdList->SetGraphicsRootSignature(sRootSign);

	const std::size_t descHandleIncSize{ DirectXManager::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) };
	D3D12_GPU_DESCRIPTOR_HANDLE objectCBufferGpuDesc(mObjectCBufferGpuDescBegin);
	D3D12_GPU_DESCRIPTOR_HANDLE materialsCBufferGpuDesc(mMaterialsCBufferGpuDescBegin);
	D3D12_GPU_DESCRIPTOR_HANDLE texturesBufferGpuDesc(mTexturesBufferGpuDescBegin);
	D3D12_GPU_DESCRIPTOR_HANDLE normalsBufferGpuDesc(mNormalsBufferGpuDescBegin);
	D3D12_GPU_DESCRIPTOR_HANDLE heightsBufferGpuDesc(mHeightsBufferGpuDescBegin);

	mCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

	// Set frame constants root parameters
	D3D12_GPU_VIRTUAL_ADDRESS frameCBufferGpuVAddress(uploadFrameCBuffer.Resource()->GetGPUVirtualAddress());
	mCmdList->SetGraphicsRootConstantBufferView(1U, frameCBufferGpuVAddress);
	mCmdList->SetGraphicsRootConstantBufferView(2U, frameCBufferGpuVAddress);
	mCmdList->SetGraphicsRootConstantBufferView(5U, frameCBufferGpuVAddress);
	
	// Draw objects
	const std::size_t geomCount{ mGeometryDataVec.size() };
	for (std::size_t i = 0UL; i < geomCount; ++i) {
		GeometryData& geomData{ mGeometryDataVec[i] };
		mCmdList->IASetVertexBuffers(0U, 1U, &geomData.mVertexBufferData.mBufferView);
		mCmdList->IASetIndexBuffer(&geomData.mIndexBufferData.mBufferView);
		const std::size_t worldMatsCount{ geomData.mWorldMatrices.size() };
		for (std::size_t j = 0UL; j < worldMatsCount; ++j) {
			mCmdList->SetGraphicsRootDescriptorTable(0U, objectCBufferGpuDesc);
			objectCBufferGpuDesc.ptr += descHandleIncSize;

			mCmdList->SetGraphicsRootDescriptorTable(3U, heightsBufferGpuDesc);
			heightsBufferGpuDesc.ptr += descHandleIncSize;

			mCmdList->SetGraphicsRootDescriptorTable(4U, materialsCBufferGpuDesc);
			materialsCBufferGpuDesc.ptr += descHandleIncSize;

			mCmdList->SetGraphicsRootDescriptorTable(6U, texturesBufferGpuDesc);
			texturesBufferGpuDesc.ptr += descHandleIncSize;

			mCmdList->SetGraphicsRootDescriptorTable(7U, normalsBufferGpuDesc);
			normalsBufferGpuDesc.ptr += descHandleIncSize;
			
			mCmdList->DrawIndexedInstanced(geomData.mIndexBufferData.mCount, 1U, 0U, 0U, 0U);
		}
	}

	mCmdList->Close();

	mCmdListQueue->push(mCmdList);

	// Next frame
	mCurrFrameIndex = (mCurrFrameIndex + 1) % SettingsManager::sQueuedFrameCount;
}

bool HeightCmdListRecorder::IsDataValid() const noexcept {
	const bool result =
		GeometryPassCmdListRecorder::IsDataValid() &&
		mTexturesBufferGpuDescBegin.ptr != 0UL &&
		mNormalsBufferGpuDescBegin.ptr != 0UL &&
		mHeightsBufferGpuDescBegin.ptr != 0UL;

	return result;
}

void HeightCmdListRecorder::BuildBuffers(
	const Material* materials,
	ID3D12Resource** textures,
	ID3D12Resource** normals,
	ID3D12Resource** heights,
	const std::uint32_t dataCount) noexcept {

	ASSERT(materials != nullptr);
	ASSERT(textures != nullptr);
	ASSERT(normals != nullptr);
	ASSERT(heights != nullptr);
	ASSERT(dataCount != 0UL);

#ifdef _DEBUG
	for (std::uint32_t i = 0U; i < SettingsManager::sQueuedFrameCount; ++i) {
		ASSERT(mFrameCBuffer[i] == nullptr);
	}
#endif
	ASSERT(mObjectCBuffer == nullptr);
	ASSERT(mMaterialsCBuffer == nullptr);

	// Create object cbuffer and fill it
	const std::size_t objCBufferElemSize{ UploadBuffer::CalcConstantBufferByteSize(sizeof(ObjectCBuffer)) };
	ResourceManager::Get().CreateUploadBuffer(objCBufferElemSize, dataCount, mObjectCBuffer);
	std::uint32_t k = 0U;
	const std::size_t numGeomData{ mGeometryDataVec.size() };
	ObjectCBuffer objCBuffer;
	for (std::size_t i = 0UL; i < numGeomData; ++i) {
		GeometryData& geomData{ mGeometryDataVec[i] };
		const std::uint32_t worldMatsCount{ static_cast<std::uint32_t>(geomData.mWorldMatrices.size()) };
		for (std::uint32_t j = 0UL; j < worldMatsCount; ++j) {
			const DirectX::XMMATRIX wMatrix = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&geomData.mWorldMatrices[j]));
			DirectX::XMStoreFloat4x4(&objCBuffer.mWorldMatrix, wMatrix);
			mObjectCBuffer->CopyData(k + j, &objCBuffer, sizeof(objCBuffer));
		}

		k += worldMatsCount;
	}

	// Create materials cbuffer		
	const std::size_t matCBufferElemSize{ UploadBuffer::CalcConstantBufferByteSize(sizeof(Material)) };
	ResourceManager::Get().CreateUploadBuffer(matCBufferElemSize, dataCount, mMaterialsCBuffer);

	D3D12_GPU_VIRTUAL_ADDRESS materialsGpuAddress{ mMaterialsCBuffer->Resource()->GetGPUVirtualAddress() };
	D3D12_GPU_VIRTUAL_ADDRESS objCBufferGpuAddress{ mObjectCBuffer->Resource()->GetGPUVirtualAddress() };

	// Create object / materials cbuffers descriptors
	// Create textures SRV descriptors
	std::vector<D3D12_CONSTANT_BUFFER_VIEW_DESC> objectCbufferViewDescVec;
	objectCbufferViewDescVec.reserve(dataCount);

	std::vector<D3D12_CONSTANT_BUFFER_VIEW_DESC> materialCbufferViewDescVec;
	materialCbufferViewDescVec.reserve(dataCount);

	std::vector<ID3D12Resource*> textureResVec;
	textureResVec.reserve(dataCount);
	std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC> textureSrvDescVec;
	textureSrvDescVec.reserve(dataCount);

	std::vector<ID3D12Resource*> normalResVec;
	normalResVec.reserve(dataCount);
	std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC> normalSrvDescVec;
	normalSrvDescVec.reserve(dataCount);

	std::vector<ID3D12Resource*> heightResVec;
	heightResVec.reserve(dataCount);
	std::vector<D3D12_SHADER_RESOURCE_VIEW_DESC> heightSrvDescVec;
	heightSrvDescVec.reserve(dataCount);
	for (std::size_t i = 0UL; i < dataCount; ++i) {
		// Object cbuffer desc
		D3D12_CONSTANT_BUFFER_VIEW_DESC cBufferDesc{};
		cBufferDesc.BufferLocation = objCBufferGpuAddress + i * objCBufferElemSize;
		cBufferDesc.SizeInBytes = static_cast<std::uint32_t>(objCBufferElemSize);
		objectCbufferViewDescVec.push_back(cBufferDesc);

		// Material cbuffer desc
		cBufferDesc.BufferLocation = materialsGpuAddress + i * matCBufferElemSize;
		cBufferDesc.SizeInBytes = static_cast<std::uint32_t>(matCBufferElemSize);
		materialCbufferViewDescVec.push_back(cBufferDesc);

		// Texture descriptor
		textureResVec.push_back(textures[i]);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		srvDesc.Format = textureResVec.back()->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = textureResVec.back()->GetDesc().MipLevels;
		textureSrvDescVec.push_back(srvDesc);

		// Normal descriptor
		normalResVec.push_back(normals[i]);

		srvDesc = D3D12_SHADER_RESOURCE_VIEW_DESC{};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		srvDesc.Format = normalResVec.back()->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = normalResVec.back()->GetDesc().MipLevels;
		normalSrvDescVec.push_back(srvDesc);

		// Height descriptor
		heightResVec.push_back(heights[i]);

		srvDesc = D3D12_SHADER_RESOURCE_VIEW_DESC{};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		srvDesc.Format = heightResVec.back()->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = heightResVec.back()->GetDesc().MipLevels;
		heightSrvDescVec.push_back(srvDesc);

		mMaterialsCBuffer->CopyData(static_cast<std::uint32_t>(i), &materials[i], sizeof(Material));
	}
	mObjectCBufferGpuDescBegin =
		CbvSrvUavDescriptorManager::Get().CreateConstantBufferViews(
			objectCbufferViewDescVec.data(), 
			static_cast<std::uint32_t>(objectCbufferViewDescVec.size()));
	mMaterialsCBufferGpuDescBegin =
		CbvSrvUavDescriptorManager::Get().CreateConstantBufferViews(
			materialCbufferViewDescVec.data(), 
			static_cast<std::uint32_t>(materialCbufferViewDescVec.size()));
	mTexturesBufferGpuDescBegin =
		CbvSrvUavDescriptorManager::Get().CreateShaderResourceViews(
			textureResVec.data(), 
			textureSrvDescVec.data(), 
			static_cast<std::uint32_t>(textureSrvDescVec.size()));
	mNormalsBufferGpuDescBegin =
		CbvSrvUavDescriptorManager::Get().CreateShaderResourceViews(
			normalResVec.data(), 
			normalSrvDescVec.data(), 
			static_cast<std::uint32_t>(normalSrvDescVec.size()));
	mHeightsBufferGpuDescBegin =
		CbvSrvUavDescriptorManager::Get().CreateShaderResourceViews(
			heightResVec.data(), 
			heightSrvDescVec.data(), 
			static_cast<std::uint32_t>(heightSrvDescVec.size()));

	// Create frame cbuffers
	const std::size_t frameCBufferElemSize{ UploadBuffer::CalcConstantBufferByteSize(sizeof(FrameCBuffer)) };
	for (std::uint32_t i = 0U; i < SettingsManager::sQueuedFrameCount; ++i) {
		ResourceManager::Get().CreateUploadBuffer(frameCBufferElemSize, 1U, mFrameCBuffer[i]);
	}
}