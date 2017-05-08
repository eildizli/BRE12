#include "Scene.h"

namespace BRE {
GeometryPassCommandListRecorders&
Scene::GetGeometryPassCommandListRecorders() noexcept
{
    return mGeometryCommandListRecorders;
}

ID3D12Resource*
&Scene::GetSkyBoxCubeMap() noexcept
{
    return mSkyBoxCubeMap;
}

ID3D12Resource*
&Scene::GetDiffuseIrradianceCubeMap() noexcept
{
    return mDiffuseIrradianceCubeMap;
}

ID3D12Resource*
&Scene::GetSpecularPreConvolvedCubeMap() noexcept
{
    return mSpecularPreConvolvedCubeMap;
}
}

