#pragma once

#include <d3d12.h>
#include <memory>
#include <tbb\concurrent_hash_map.h>
#include <wrl.h>

#include <DXUtils\d3dx12.h>

class RootSignatureManager {
public:
	static std::unique_ptr<RootSignatureManager> gManager;

	explicit RootSignatureManager(ID3D12Device& device) : mDevice(device) {}
	RootSignatureManager(const RootSignatureManager&) = delete;
	const RootSignatureManager& operator=(const RootSignatureManager&) = delete;

	// Asserts if name was already registered
	std::size_t CreateRootSignature(const char* name, const CD3DX12_ROOT_SIGNATURE_DESC& desc, ID3D12RootSignature* &rootSign) noexcept;

	// Asserts id was not already registered
	ID3D12RootSignature& GetRootSignature(const std::size_t id) noexcept;

	// Asserts if id is not present
	void Erase(const std::size_t id) noexcept;

	// This will invalidate all ids.
	void Clear() noexcept { mRootSignatureById.clear(); }

private:
	ID3D12Device& mDevice;

	using RootSignatureById = tbb::concurrent_hash_map<std::size_t, Microsoft::WRL::ComPtr<ID3D12RootSignature>>;
	RootSignatureById mRootSignatureById;
};