#pragma once

#include <cstddef>

#include <optix_stubs.h>

#include <APIObject.hpp>
#include <Optix.hpp>
#include <DeviceBuffer.hpp>
#include <math/Vector.hpp>
#include <macros/cuda.hpp>
#include <macros/optix.hpp>
#include <scene/ASBuildScratchpad.hpp>

#include <filesystem>


struct Mesh : APIObject<Mesh>
{
	void setVertices(Vec3f *vertices, std::size_t vertexCount);
	OptixTraversableHandle getGAS();

private:
	void setIndices(Vec3i *indices, std::size_t indexCount);
	Mesh(Vec3f *vertices, std::size_t vertexCount,
		 Vec3i *indices, std::size_t indexCount);

	OptixTraversableHandle buildGAS();
	void updateGAS();

private:
	friend APIObject<Mesh>;
	friend struct Scene;
	ASBuildScratchpad scratchpad;
	bool gasNeedsUpdate;
	std::optional<OptixTraversableHandle> cachedGAS;
	DeviceBuffer<Vec3f> dVertices;
	DeviceBuffer<Vec3i> dIndices;

	// Shared between buildGAS() and updateGAS()
	OptixBuildInput buildInput;
	OptixAccelBuildOptions buildOptions;

};