#include <graph/Nodes.hpp>
#include <gpu/nodeKernels.hpp>
#include <RGLFields.hpp>

std::size_t FormatNode::getPointSize() const
{
	std::size_t size = 0;
	for (auto&& field : fields) {
		size += getFieldSize(field);
	}
	return size;
}

void FormatNode::validate()
{
	input = getValidInput<IPointCloudNode>();
}

void FormatNode::schedule(cudaStream_t stream)
{
	std::size_t pointCount = input->getWidth() * input->getHeight();
	output->resize(pointCount * getPointSize(), false, false);
	auto gpuFields = VArrayProxy<GPUFieldDesc>::create(fields.size());
	std::size_t offset = 0;
	std::size_t gpuFieldIdx = 0;
	for (size_t i = 0; i < fields.size(); ++i) {
		if (!isDummy(fields[i])) {
			(*gpuFields)[gpuFieldIdx] = GPUFieldDesc {
				.data = static_cast<char*>(input->getFieldData(fields[i], stream)->getDevicePtr()),
				.size = getFieldSize(fields[i]),
				.dstOffset = offset,
			};
			gpuFieldIdx += 1;
		}
		offset += getFieldSize(fields[i]);
	}
	char* outputPtr = static_cast<char*>(output->getDevicePtr());
	gpuFormat(stream, pointCount, getPointSize(), fields.size(), gpuFields->getDevicePtr(), outputPtr);
}