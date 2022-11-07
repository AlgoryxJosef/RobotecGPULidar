#include <graph/Nodes.hpp>

void FromMat3x4fRaysNode::setParameters(const Mat3x4f *raysRaw, size_t rayCount)
{
	rays->setData(raysRaw, rayCount);
}

void FromMat3x4fRaysNode::validate()
{
	// :)
}
