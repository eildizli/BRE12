#include "SceneLoader.h"

#include <cstdint>
#include <d3d12.h>
#include <string>
#pragma warning( push )
#pragma warning( disable : 4127)
#include <yaml-cpp/yaml.h>
#pragma warning( pop ) 

#include <CommandManager\CommandAllocatorManager.h>
#include <CommandManager\CommandListManager.h>
#include <GeometryPass\Recorders\TextureCmdListRecorder.h>
#include <MathUtils\MathUtils.h>
#include <ModelManager\Model.h>
#include <Utils/DebugUtils.h>

using namespace DirectX;

SceneLoader::SceneLoader() 
	: mMaterialTechniqueLoader(mTextureLoader)
	, mDrawableObjectLoader(mMaterialPropertiesLoader, mMaterialTechniqueLoader, mModelLoader)
	, mEnvironmentLoader(mTextureLoader)
{

	mCommandAllocator = &CommandAllocatorManager::CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT);
	mCommandList = &CommandListManager::CreateCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, *mCommandAllocator);
	mCommandList->Close();
};

Scene* SceneLoader::LoadScene(const char* sceneFilePath) noexcept {
	ASSERT(sceneFilePath != nullptr);

	const YAML::Node rootNode = YAML::LoadFile(sceneFilePath);
	ASSERT(rootNode.IsDefined());

	mModelLoader.LoadModels(rootNode, *mCommandAllocator, *mCommandList);
	mTextureLoader.LoadTextures(rootNode, *mCommandAllocator, *mCommandList);
	mMaterialPropertiesLoader.LoadMaterialsProperties(rootNode);	
	mMaterialTechniqueLoader.LoadMaterialTechniques(rootNode);
	mDrawableObjectLoader.LoadDrawableObjects(rootNode);
	mEnvironmentLoader.LoadEnvironment(rootNode);

	Scene* scene = new Scene;
	GenerateGeometryPassRecorders(*scene);

	return scene;
}

void 
SceneLoader::GenerateGeometryPassRecorders(Scene& scene) noexcept {
	GenerateGeometryPassRecordersForTextureMapping(scene.GetGeometryPassCommandListRecorders());

	scene.GetSkyBoxCubeMap() = &mEnvironmentLoader.GetSkyBoxTexture();
	scene.GetDiffuseIrradianceCubeMap() = &mEnvironmentLoader.GetDiffuseIrradianceTexture();
	scene.GetSpecularPreConvolvedCubeMap() = &mEnvironmentLoader.GetSpecularPreConvolvedEnvironmentTexture();
}

void 
SceneLoader::GenerateGeometryPassRecordersForTextureMapping(Scene::GeometryPassCommandListRecorders& commandListRecorders) noexcept {
	const DrawableObjectLoader::DrawableObjectsByModelName& drawableObjectsByModelName = 
		mDrawableObjectLoader.GetDrawableObjectsByModelNameByTechniqueType(MaterialTechnique::TEXTURE_MAPPING);

	if (drawableObjectsByModelName.empty()) {
		return;
	}

	// Iterate over Drawable objects and fill containers needed
	// to initialize the command list recorder.
	TextureCmdListRecorder* commandListRecorder = new TextureCmdListRecorder;
	GeometryPassCmdListRecorder::GeometryDataVector geometryDataVector;
	std::vector<MaterialProperties> materialProperties;
	std::vector<ID3D12Resource*> diffuseTextures;

	std::size_t geometryDataVectorOffset = 0;
	for (const DrawableObjectLoader::DrawableObjectsByModelName::value_type& pair : drawableObjectsByModelName) {
		const std::vector<DrawableObject>& drawableObjects = pair.second;
		ASSERT(drawableObjects.empty() == false);

		// Build geometry data vertex and index buffers for all meshes
		const Model& model = drawableObjects[0].GetModel();
		const std::vector<Mesh>& meshes = model.GetMeshes();
		const std::size_t totalDataCount = meshes.size() * drawableObjects.size();
		geometryDataVector.reserve(geometryDataVector.size() + totalDataCount);
		for (std::uint32_t i = 0U; i < meshes.size(); ++i) {
			const Mesh& mesh = meshes[i];
			GeometryPassCmdListRecorder::GeometryData geometryData;
			geometryData.mVertexBufferData = mesh.GetVertexBufferData();
			geometryData.mIndexBufferData = mesh.GetIndexBufferData();
			geometryData.mWorldMatrices.reserve(meshes.size());
			geometryData.mInverseTransposeWorldMatrices.reserve(meshes.size());
			geometryDataVector.emplace_back(geometryData);
		}

		// Iterate over all drawable objects and fill buffers
		materialProperties.reserve(materialProperties.size() + totalDataCount);
		diffuseTextures.reserve(diffuseTextures.size() + totalDataCount);
		for (const DrawableObject& drawableObject : drawableObjects) {
			// Store material properties
			materialProperties.push_back(drawableObject.GetMaterialProperties());

			// Store textures
			const MaterialTechnique& materialTechnique = drawableObject.GetMaterialTechnique();
			diffuseTextures.push_back(&materialTechnique.GetDiffuseTexture());

			// Store world matrix
			const XMFLOAT4X4& worldMatrix = drawableObject.GetWorldMatrix();
			XMFLOAT4X4 inverseTransposeWorldMatrix;
			MathUtils::StoreInverseTransposeMatrix(worldMatrix, inverseTransposeWorldMatrix);
			for (std::uint32_t i = 0U; i < meshes.size(); ++i) {
				GeometryPassCmdListRecorder::GeometryData& geometryData = 
					geometryDataVector[geometryDataVectorOffset + i];

				geometryData.mWorldMatrices.push_back(worldMatrix);
				geometryData.mInverseTransposeWorldMatrices.push_back(inverseTransposeWorldMatrix);
			}
		}

		geometryDataVectorOffset += totalDataCount;
	}

	commandListRecorder->Init(geometryDataVector, materialProperties, diffuseTextures);

	commandListRecorders.push_back(std::unique_ptr<GeometryPassCmdListRecorder>(commandListRecorder));
}