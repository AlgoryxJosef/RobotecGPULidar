// Copyright 2023 Robotec.AI
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>

#include <graph/NodesCore.hpp>
#include <graph/NodesPcl.hpp>
#include <gpu/nodeKernels.hpp>

void RadarPostprocessPointsNode::setParameters(float distanceSeparation, float azimuthSeparation)
{
	this->distanceSeparation = distanceSeparation;
	this->azimuthSeparation = azimuthSeparation;
}

void RadarPostprocessPointsNode::validateImpl()
{
	IPointsNodeSingleInput::validateImpl();

	// Needed to clear cache because fields in the pipeline may have changed
	// In fact, the cache manager is no longer useful here
	// To be kept/removed in some future refactor (when resolving comment in the `enqueueExecImpl`)
	cacheManager.clear();
}

void RadarPostprocessPointsNode::enqueueExecImpl()
{
	cacheManager.trigger();

	if (input->getPointCount() == 0) {
		filteredIndices->resize(0, false, false);
		return;
	}

	distanceInputHost->copyFrom(input->getFieldData(DISTANCE_F32));
	azimuthInputHost->copyFrom(input->getFieldData(AZIMUTH_F32));
	elevationInputHost->copyFrom(input->getFieldData(ELEVATION_F32));

	std::vector<RadarCluster> clusters;
	// Create first cluster with the first point
	clusters.emplace_back(0, distanceInputHost->getReadPtr()[0], azimuthInputHost->getReadPtr()[0],
	                      elevationInputHost->getReadPtr()[0]);

	for (int i = 1; i < input->getPointCount(); ++i) {
		auto distance = distanceInputHost->getReadPtr()[i];
		auto azimuth = azimuthInputHost->getReadPtr()[i];
		auto elevation = elevationInputHost->getReadPtr()[i];
		bool isPointClustered = false;
		for (auto&& cluster : clusters) {
			if (cluster.isCandidate(distance, azimuth, distanceSeparation, azimuthSeparation)) {
				cluster.addPoint(i, distance, azimuth, elevation);
				isPointClustered = true;
				break;
			}
		}

		if (!isPointClustered) {
			// Create a new cluster
			clusters.emplace_back(i, distance, azimuth, elevation);
		}
	}

	// Merge clusters if are close enough
	bool allClustersGood = false;
	while (clusters.size() > 1 && !allClustersGood) {
		allClustersGood = true;
		for (int i = 0; i < clusters.size(); ++i) {
			for (int j = i + 1; j < clusters.size(); ++j) {
				if (clusters[i].canMergeWith(clusters[j], distanceSeparation, azimuthSeparation)) {
					clusters[i].takeIndicesFrom(std::move(clusters[j]));
					clusters.erase(clusters.begin() + j);
					allClustersGood = false;
					break;
				}
			}
			if (!allClustersGood) {
				break;
			}
		}
	}

	filteredIndicesHost.clear();
	for (auto&& cluster : clusters) {
		filteredIndicesHost.push_back(
		    cluster.findDirectionalCenterIndex(azimuthInputHost->getReadPtr(), elevationInputHost->getReadPtr()));
	}

	filteredIndices->copyFromExternal(filteredIndicesHost.data(), filteredIndicesHost.size());

	// getFieldData may be called in client's thread from rgl_graph_get_result_data
	// Doing job there would be:
	// - unexpected (job was supposed to be done asynchronously)
	// - hard to implement:
	//     - to avoid blocking on yet-running graph stream, we would need do it in copy stream, which would require
	//       temporary rebinding DAAs to copy stream, which seems like nightmarish idea
	// Therefore, once we know what fields are requested, we compute them eagerly
	// This is supposed to be removed in some future refactor (e.g. when introducing LayeredSoA)
	for (auto&& field : cacheManager.getKeys()) {
		getFieldData(field);
	}
}

size_t RadarPostprocessPointsNode::getWidth() const
{
	this->synchronize();
	return filteredIndices->getCount();
}

IAnyArray::ConstPtr RadarPostprocessPointsNode::getFieldData(rgl_field_t field)
{
	std::lock_guard lock{getFieldDataMutex};

	if (!cacheManager.contains(field)) {
		auto fieldData = createArray<DeviceAsyncArray>(field, arrayMgr);
		fieldData->resize(filteredIndices->getCount(), false, false);
		cacheManager.insert(field, fieldData, true);
	}

	if (!cacheManager.isLatest(field)) {
		auto fieldData = cacheManager.getValue(field);
		fieldData->resize(filteredIndices->getCount(), false, false);
		char* outPtr = static_cast<char*>(fieldData->getRawWritePtr());
		auto fieldArray = input->getFieldData(field);
		if (!isDeviceAccessible(fieldArray->getMemoryKind())) {
			auto msg = fmt::format("RadarPostprocessPoints requires its input to be device-accessible, {} is not", field);
			throw InvalidPipeline(msg);
		}
		const char* inputPtr = static_cast<const char*>(fieldArray->getRawReadPtr());
		gpuFilter(getStreamHandle(), filteredIndices->getCount(), filteredIndices->getReadPtr(), outPtr, inputPtr,
		          getFieldSize(field));
		CHECK_CUDA(cudaStreamSynchronize(getStreamHandle()));
		cacheManager.setUpdated(field);
	}

	return cacheManager.getValue(field);
}

std::vector<rgl_field_t> RadarPostprocessPointsNode::getRequiredFieldList() const
{
	return {DISTANCE_F32, AZIMUTH_F32, ELEVATION_F32};
}

// RadarCluster methods implementation

RadarPostprocessPointsNode::RadarCluster::RadarCluster(Field<RAY_IDX_U32>::type index, float distance, float azimuth,
                                                       float elevation)
{
	indices.emplace_back(index);
	minMaxDistance = {distance, distance};
	minMaxAzimuth = {azimuth, azimuth};
	minMaxElevation = {elevation, elevation};
}

void RadarPostprocessPointsNode::RadarCluster::addPoint(Field<RAY_IDX_U32>::type index, float distance, float azimuth,
                                                        float elevation)
{
	indices.emplace_back(index);
	minMaxDistance[0] = std::min(minMaxDistance[0], distance);
	minMaxDistance[1] = std::max(minMaxDistance[1], distance);
	minMaxAzimuth[0] = std::min(minMaxAzimuth[0], azimuth);
	minMaxAzimuth[1] = std::max(minMaxAzimuth[1], azimuth);
	minMaxElevation[0] = std::min(minMaxElevation[0], elevation);
	minMaxElevation[1] = std::max(minMaxElevation[1], elevation);
}

inline bool RadarPostprocessPointsNode::RadarCluster::isCandidate(float distance, float azimuth, float distanceSeparation,
                                                                  float azimuthSeparation) const
{
	return (distance >= minMaxDistance[0] - distanceSeparation && distance <= minMaxDistance[1] + distanceSeparation) &&
	       (azimuth >= minMaxAzimuth[0] - azimuthSeparation && azimuth <= minMaxAzimuth[1] + azimuthSeparation);
}

inline bool RadarPostprocessPointsNode::RadarCluster::canMergeWith(const RadarCluster& other, float distanceSeparation,
                                                                   float azimuthSeparation) const
{
	bool isDistanceGood = std::abs(minMaxDistance[0] - other.minMaxDistance[1]) <= distanceSeparation &&
	                      std::abs(minMaxDistance[1] - other.minMaxDistance[0]) <= distanceSeparation;

	bool isAzimuthGood = std::abs(minMaxAzimuth[0] - other.minMaxAzimuth[1]) <= azimuthSeparation &&
	                     std::abs(minMaxAzimuth[1] - other.minMaxAzimuth[0]) <= azimuthSeparation;

	return isDistanceGood && isAzimuthGood;
}

void RadarPostprocessPointsNode::RadarCluster::takeIndicesFrom(RadarCluster&& other)
{
	minMaxDistance[0] = std::min(minMaxDistance[0], other.minMaxDistance[0]);
	minMaxDistance[1] = std::max(minMaxDistance[1], other.minMaxDistance[1]);
	minMaxAzimuth[0] = std::min(minMaxAzimuth[0], other.minMaxAzimuth[0]);
	minMaxAzimuth[1] = std::max(minMaxAzimuth[1], other.minMaxAzimuth[1]);
	minMaxElevation[0] = std::min(minMaxElevation[0], other.minMaxElevation[0]);
	minMaxElevation[1] = std::max(minMaxElevation[1], other.minMaxElevation[1]);

	// Move indices
	std::size_t n = indices.size();
	indices.resize(indices.size() + other.indices.size());
	std::move(other.indices.begin(), other.indices.end(), indices.begin() + n);
}

Field<RAY_IDX_U32>::type RadarPostprocessPointsNode::RadarCluster::findDirectionalCenterIndex(
    const Field<AZIMUTH_F32>::type* azimuths, const Field<ELEVATION_F32>::type* elevations) const
{
	auto meanAzimuth = (minMaxAzimuth[0] + minMaxAzimuth[1]) / 2.0f;
	auto meanElevation = (minMaxElevation[0] + minMaxElevation[1]) / 2.0f;

	float minDistance = FLT_MAX;
	uint32_t minIndex = indices.front();

	for (auto&& i : indices) {
		float distance = std::abs(azimuths[i] - meanAzimuth) + std::abs(elevations[i] - meanElevation);
		if (distance < minDistance) {
			minDistance = distance;
			minIndex = i;
		}
	}
	return minIndex;
}