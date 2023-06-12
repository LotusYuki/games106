/*
 * Vulkan Example - Variable rate shading
 *
 * Copyright (C) 2020-2022 by Sascha Willems - www.saschawillems.de
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */

/*
 * 首先，我们要创建三个纹理，一个preframeTexture用来存储上一帧的画面，另外两个是nasDataSurface和vrsSurface，这两个都是用来计算着色频率
 * 在framebuffer通过drawCmdBuffers写入到交换链后，就可以把framebuffer的view复制给preframeTexture
 * 直接复制上一帧的交换链图像有问题
 ** 尝试一，写多一个渲染通道输出画面
 ** 多写一个pass来输出上一帧画面,然后最后输出到交换链那一步直接采样该pass的图像（问题：会把UI也复制进去）
 ** 最终做法，先用一个pass渲染好这一帧，保存下来作为preframe texture，然后渲染到quad上，下一帧就可以用preframe texture来计算shading rate
 * ComputeNASData.hlsl (content adaptive shading)输入preframeTexture以及把信息写入nasDataSurface，
 * ComputeShadingRate.hlsl (motion adaptive shading)输入nasDataSurface输出vrsSurface
 * 关于复制的部分可以参考pbribl的示例generateIrradianceCube()。
 * nasDataSurface和vrsSurface应该可以参考computeshader的示例用法
 * 论文中似乎还要用到gbufferDepth，这部分可以参考deferred的示例。
 */

#include "homework2.h"

VulkanExample::VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
{
	title = "Variable rate shading";
	apiVersion = VK_API_VERSION_1_1;
	camera.type = Camera::CameraType::firstperson;
	camera.flipY = true;
	camera.setPosition(glm::vec3(0.0f, 1.0f, 0.0f));
	camera.setRotation(glm::vec3(0.0f, -90.0f, 0.0f));
	camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
	camera.setRotationSpeed(0.25f);
	enabledInstanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	enabledDeviceExtensions.push_back(VK_NV_SHADING_RATE_IMAGE_EXTENSION_NAME);

	preframeCmdBuffer = VK_NULL_HANDLE;
}

VulkanExample::~VulkanExample()
{
	vkDestroyPipeline(device, basePipelines.masked, nullptr);
	vkDestroyPipeline(device, basePipelines.opaque, nullptr);
	vkDestroyPipeline(device, shadingRatePipelines.masked, nullptr);
	vkDestroyPipeline(device, shadingRatePipelines.opaque, nullptr);
	vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
	vkDestroyImageView(device, shadingRateImage.view, nullptr);
	vkDestroyImage(device, shadingRateImage.image, nullptr);
	vkFreeMemory(device, shadingRateImage.memory, nullptr);
	shaderData.buffer.destroy();


	vkDestroyFramebuffer(device, preframeFramebuffer, nullptr);
	vkDestroySampler(device, colorSampler, nullptr);


	preframeTexture.destroy();
	nasDataSurface.destroy();
	vrsSurface.destroy();
}

void VulkanExample::getEnabledFeatures()
{
	enabledFeatures.samplerAnisotropy = deviceFeatures.samplerAnisotropy;
	// POI
	enabledPhysicalDeviceShadingRateImageFeaturesNV = {};
	enabledPhysicalDeviceShadingRateImageFeaturesNV.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_FEATURES_NV;
	enabledPhysicalDeviceShadingRateImageFeaturesNV.shadingRateImage = VK_TRUE;
	deviceCreatepNextChain = &enabledPhysicalDeviceShadingRateImageFeaturesNV;
}

/*
	If the window has been resized, we need to recreate the shading rate image
*/
void VulkanExample::handleResize()
{
	// Delete allocated resources
	vkDestroyImageView(device, shadingRateImage.view, nullptr);
	vkDestroyImage(device, shadingRateImage.image, nullptr);
	vkFreeMemory(device, shadingRateImage.memory, nullptr);
	// Recreate image
	prepareShadingRateImage();
	resized = false;
}

void VulkanExample::buildCommandBuffers()
{
	if (resized)
	{
		handleResize();
	}

	// VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	// VkClearValue clearValues[2];
	// clearValues[0].color = defaultClearColor;
	// clearValues[0].color = {{0.25f, 0.25f, 0.25f, 1.0f}};
	// ;
	// clearValues[1].depthStencil = {1.0f, 0};

	// VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
	// renderPassBeginInfo.renderPass = renderPass;
	// renderPassBeginInfo.renderArea.offset.x = 0;
	// renderPassBeginInfo.renderArea.offset.y = 0;
	// renderPassBeginInfo.renderArea.extent.width = width;
	// renderPassBeginInfo.renderArea.extent.height = height;
	// renderPassBeginInfo.clearValueCount = 2;
	// renderPassBeginInfo.pClearValues = clearValues;

	// const VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
	// const VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);

	// for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
	// {
	// 	renderPassBeginInfo.framebuffer = frameBuffers[i];
	// 	VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));
	// 	vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	// 	vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
	// 	vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);
	// 	vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

	// 	// POI: Bind the image that contains the shading rate patterns
	// 	if (enableShadingRate)
	// 	{
	// 		vkCmdBindShadingRateImageNV(drawCmdBuffers[i], shadingRateImage.view, VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV);
	// 	};

	// 	// Render the scene
	// 	Pipelines &pipelines = enableShadingRate ? shadingRatePipelines : basePipelines;
	// 	vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.opaque);
	// 	scene.draw(drawCmdBuffers[i], vkglTF::RenderFlags::BindImages | vkglTF::RenderFlags::RenderOpaqueNodes, pipelineLayout);
	// 	vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.masked);
	// 	scene.draw(drawCmdBuffers[i], vkglTF::RenderFlags::BindImages | vkglTF::RenderFlags::RenderAlphaMaskedNodes, pipelineLayout);

	// 	drawUI(drawCmdBuffers[i]);
	// 	vkCmdEndRenderPass(drawCmdBuffers[i]);
	// 	VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
	// }

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	VkClearValue clearValues[2];
	clearValues[0].color = defaultClearColor;
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
	renderPassBeginInfo.renderPass = renderPass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = width;
	renderPassBeginInfo.renderArea.extent.height = height;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;

	for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
	{
		// Set target frame buffer
		renderPassBeginInfo.framebuffer = frameBuffers[i];

		VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

		// Image memory barrier to make sure that compute shader writes are finished before sampling from the texture
		VkImageMemoryBarrier imageMemoryBarrier = {};
		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		// We won't be changing the layout of the image
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarrier.image = vrsSurface.image;
		imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vkCmdPipelineBarrier(
			drawCmdBuffers[i],
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_FLAGS_NONE,
			0, nullptr,
			0, nullptr,
			1, &imageMemoryBarrier);


		vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

		// if (enableShadingRate)
		// {
	 	// 	vkCmdBindShadingRateImageNV(drawCmdBuffers[i], shadingRateImage.view, VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV);
		// };

		// VkPipeline pipelines = enableShadingRate ? graphics.shadingRatePipelines : graphics.pipeline;
		vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipelineLayout, 0, 1, &graphics.descriptorSet, 0, NULL);
		vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphics.pipeline);

		vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

		drawUI(drawCmdBuffers[i]);

		vkCmdEndRenderPass(drawCmdBuffers[i]);

		VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
	}
}

void VulkanExample::loadAssets()
{
	vkglTF::descriptorBindingFlags = vkglTF::DescriptorBindingFlags::ImageBaseColor | vkglTF::DescriptorBindingFlags::ImageNormalMap;
	scene.loadFromFile(getAssetPath() + "models/sponza/sponza.gltf", vulkanDevice, queue, vkglTF::FileLoadingFlags::PreTransformVertices);
}

void VulkanExample::setupDescriptors()
{
	// Pool
	const std::vector<VkDescriptorPoolSize> poolSizes = {
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3),
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4),
		// Compute pipelines uses a storage image for image reads and writes
		vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4),
	};
	VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 3);
	VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

	// Descriptor set layout
	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)};
	VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

	// Pipeline layout
	const std::vector<VkDescriptorSetLayout> setLayouts = {
		descriptorSetLayout,
		vkglTF::descriptorSetLayoutImage,
	};
	VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(setLayouts.data(), 2);
	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));

	// Descriptor set
	VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
	std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
		vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &shaderData.buffer.descriptor),
		vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &preframeTexture.descriptor),
	};
	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);


	// Descriptor set layout for quad texture
	setLayoutBindings = {
		// Binding 0: Vertex shader uniform buffer
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0),
		// Binding 1: Fragment shader input image
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
	};

	descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &graphics.descriptorSetLayout));

	pPipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&graphics.descriptorSetLayout, 1);
	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &graphics.pipelineLayout));

	allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &graphics.descriptorSetLayout, 1);

	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &graphics.descriptorSet));
	writeDescriptorSets = {
		vks::initializers::writeDescriptorSet(graphics.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &shaderData.buffer.descriptor),
		vks::initializers::writeDescriptorSet(graphics.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &preframeTexture.descriptor)
	};
	vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);

}

// [POI]
void VulkanExample::prepareShadingRateImage()
{
	// Shading rate image size depends on shading rate texel size
	// For each texel in the target image, there is a corresponding shading texel size width x height block in the shading rate image
	VkExtent3D imageExtent{};
	imageExtent.width = static_cast<uint32_t>(ceil(width / (float)physicalDeviceShadingRateImagePropertiesNV.shadingRateTexelSize.width));	  // width/16
	imageExtent.height = static_cast<uint32_t>(ceil(height / (float)physicalDeviceShadingRateImagePropertiesNV.shadingRateTexelSize.height)); // height/16
	imageExtent.depth = 1;

	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = VK_FORMAT_R8_UINT;
	imageCI.extent = imageExtent;
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCI.usage = VK_IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &shadingRateImage.image));
	VkMemoryRequirements memReqs{};
	vkGetImageMemoryRequirements(device, shadingRateImage.image, &memReqs);

	VkDeviceSize bufferSize = imageExtent.width * imageExtent.height * sizeof(uint8_t);

	VkMemoryAllocateInfo memAllloc{};
	memAllloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memAllloc.allocationSize = memReqs.size;
	memAllloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAllloc, nullptr, &shadingRateImage.memory));
	VK_CHECK_RESULT(vkBindImageMemory(device, shadingRateImage.image, shadingRateImage.memory, 0));

	VkImageViewCreateInfo imageViewCI{};
	imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCI.image = shadingRateImage.image;
	imageViewCI.format = VK_FORMAT_R8_UINT;
	imageViewCI.subresourceRange.baseMipLevel = 0;
	imageViewCI.subresourceRange.levelCount = 1;
	imageViewCI.subresourceRange.baseArrayLayer = 0;
	imageViewCI.subresourceRange.layerCount = 1;
	imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &shadingRateImage.view));

	// Populate with lowest possible shading rate pattern
	uint8_t val = VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X4_PIXELS_NV;
	uint8_t *shadingRatePatternData = new uint8_t[bufferSize];
	memset(shadingRatePatternData, val, bufferSize);

	// Create a circular pattern with decreasing sampling rates outwards (max. range, pattern)
	std::map<float, VkShadingRatePaletteEntryNV> patternLookup = {
		{8.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_PIXEL_NV},
		{12.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X1_PIXELS_NV},
		{16.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_1X2_PIXELS_NV},
		{18.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X2_PIXELS_NV},
		{20.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X2_PIXELS_NV},
		{24.0f, VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X4_PIXELS_NV}};

	// Calculate where is the focus position （this is in screen center）
	uint8_t *ptrData = shadingRatePatternData;
	for (uint32_t y = 0; y < imageExtent.height; y++)
	{
		for (uint32_t x = 0; x < imageExtent.width; x++)
		{
			const float deltaX = (float)imageExtent.width / 2.0f - (float)x;
			const float deltaY = ((float)imageExtent.height / 2.0f - (float)y) * ((float)width / (float)height);
			const float dist = std::sqrt(deltaX * deltaX + deltaY * deltaY);
			for (auto pattern : patternLookup)
			{
				if (dist < pattern.first)
				{
					*ptrData = pattern.second;
					break;
				}
			}
			ptrData++;
		}
	}

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;

	VkBufferCreateInfo bufferCreateInfo{};
	bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCreateInfo.size = bufferSize;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &stagingBuffer));
	VkMemoryAllocateInfo memAllocInfo{};
	memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memReqs = {};
	vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &stagingMemory));
	VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0));

	uint8_t *mapped;
	VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, memReqs.size, 0, (void **)&mapped));
	memcpy(mapped, shadingRatePatternData, bufferSize);
	vkUnmapMemory(device, stagingMemory);

	delete[] shadingRatePatternData;

	// Upload
	VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.levelCount = 1;
	subresourceRange.layerCount = 1;
	{
		VkImageMemoryBarrier imageMemoryBarrier{};
		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageMemoryBarrier.srcAccessMask = 0;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.image = shadingRateImage.image;
		imageMemoryBarrier.subresourceRange = subresourceRange;
		vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	}
	VkBufferImageCopy bufferCopyRegion{};
	bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	bufferCopyRegion.imageSubresource.layerCount = 1;
	bufferCopyRegion.imageExtent.width = imageExtent.width;
	bufferCopyRegion.imageExtent.height = imageExtent.height;
	bufferCopyRegion.imageExtent.depth = 1;
	vkCmdCopyBufferToImage(copyCmd, stagingBuffer, shadingRateImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);
	{
		VkImageMemoryBarrier imageMemoryBarrier{};
		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV;
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstAccessMask = 0;
		imageMemoryBarrier.image = shadingRateImage.image;
		imageMemoryBarrier.subresourceRange = subresourceRange;
		vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	}
	vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

	vkFreeMemory(device, stagingMemory, nullptr);
	vkDestroyBuffer(device, stagingBuffer, nullptr);
}

void VulkanExample::preparePipelines()
{
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
	VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
	VkPipelineColorBlendAttachmentState blendAttachmentStateCI = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
	VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentStateCI);
	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
	VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
	VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
	const std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables.data(), static_cast<uint32_t>(dynamicStateEnables.size()), 0);
	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

	VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
	pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
	pipelineCI.pRasterizationState = &rasterizationStateCI;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pMultisampleState = &multisampleStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pDepthStencilState = &depthStencilStateCI;
	pipelineCI.pDynamicState = &dynamicStateCI;
	pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();
	pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Tangent});

	shaderStages[0] = loadShader(getHomeworkShadersPath() + "homework2/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	shaderStages[1] = loadShader(getHomeworkShadersPath() + "homework2/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

	// Properties for alpha masked materials will be passed via specialization constants
	struct SpecializationData
	{
		VkBool32 alphaMask;
		float alphaMaskCutoff;
	} specializationData;
	specializationData.alphaMask = false;
	specializationData.alphaMaskCutoff = 0.5f;
	const std::vector<VkSpecializationMapEntry> specializationMapEntries = {
		vks::initializers::specializationMapEntry(0, offsetof(SpecializationData, alphaMask), sizeof(SpecializationData::alphaMask)),
		vks::initializers::specializationMapEntry(1, offsetof(SpecializationData, alphaMaskCutoff), sizeof(SpecializationData::alphaMaskCutoff)),
	};
	VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(specializationMapEntries, sizeof(specializationData), &specializationData);
	shaderStages[1].pSpecializationInfo = &specializationInfo;

	// Create pipeline without shading rate
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &basePipelines.opaque));
	specializationData.alphaMask = true;
	rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &basePipelines.masked));
	rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
	specializationData.alphaMask = false;

	//[POI] Possible per-Viewport shading rate palette entries
	const std::vector<VkShadingRatePaletteEntryNV> shadingRatePaletteEntries = {
		VK_SHADING_RATE_PALETTE_ENTRY_NO_INVOCATIONS_NV,
		VK_SHADING_RATE_PALETTE_ENTRY_16_INVOCATIONS_PER_PIXEL_NV,
		VK_SHADING_RATE_PALETTE_ENTRY_8_INVOCATIONS_PER_PIXEL_NV,
		VK_SHADING_RATE_PALETTE_ENTRY_4_INVOCATIONS_PER_PIXEL_NV,
		VK_SHADING_RATE_PALETTE_ENTRY_2_INVOCATIONS_PER_PIXEL_NV,
		VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_PIXEL_NV,
		VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X1_PIXELS_NV,
		VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_1X2_PIXELS_NV,
		VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X2_PIXELS_NV,
		VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X2_PIXELS_NV,
		VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_2X4_PIXELS_NV,
		VK_SHADING_RATE_PALETTE_ENTRY_1_INVOCATION_PER_4X4_PIXELS_NV,
	};
	VkShadingRatePaletteNV shadingRatePalette{};
	shadingRatePalette.shadingRatePaletteEntryCount = static_cast<uint32_t>(shadingRatePaletteEntries.size());
	shadingRatePalette.pShadingRatePaletteEntries = shadingRatePaletteEntries.data();
	VkPipelineViewportShadingRateImageStateCreateInfoNV pipelineViewportShadingRateImageStateCI{};
	pipelineViewportShadingRateImageStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_SHADING_RATE_IMAGE_STATE_CREATE_INFO_NV;
	pipelineViewportShadingRateImageStateCI.shadingRateImageEnable = VK_TRUE;
	pipelineViewportShadingRateImageStateCI.viewportCount = 1;
	pipelineViewportShadingRateImageStateCI.pShadingRatePalettes = &shadingRatePalette;
	viewportStateCI.pNext = &pipelineViewportShadingRateImageStateCI;
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &shadingRatePipelines.opaque));
	specializationData.alphaMask = true;
	rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &shadingRatePipelines.masked));

}

void VulkanExample::prepareUniformBuffers()
{
	VK_CHECK_RESULT(vulkanDevice->createBuffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&shaderData.buffer,
		sizeof(shaderData.values)));
	VK_CHECK_RESULT(shaderData.buffer.map());
	updateUniformBuffers();
}

void VulkanExample::updateUniformBuffers()
{
	shaderData.values.projection = camera.matrices.perspective;
	shaderData.values.view = camera.matrices.view;
	shaderData.values.viewPos = camera.viewPos;
	shaderData.values.colorShadingRate = colorShadingRate;
	memcpy(shaderData.buffer.mapped, &shaderData.values, sizeof(shaderData.values));
}

void VulkanExample::prepare()
{
	VulkanExampleBase::prepare();
	loadAssets();
	

	// [POI]
	vkCmdBindShadingRateImageNV = reinterpret_cast<PFN_vkCmdBindShadingRateImageNV>(vkGetDeviceProcAddr(device, "vkCmdBindShadingRateImageNV"));
	physicalDeviceShadingRateImagePropertiesNV.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADING_RATE_IMAGE_PROPERTIES_NV;
	VkPhysicalDeviceProperties2 deviceProperties2{};
	deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	deviceProperties2.pNext = &physicalDeviceShadingRateImagePropertiesNV;
	vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties2);

	// 准备保存上一帧画面的Texture以及记录Compute shader计算结果的Texture
	// 只用Content adaptive时，Format格式要改一下
	prepareTextureTarget(&nasDataSurface,
						 static_cast<uint32_t>(ceil(width / (float)physicalDeviceShadingRateImagePropertiesNV.shadingRateTexelSize.width)),
						 static_cast<uint32_t>(ceil(height / (float)physicalDeviceShadingRateImagePropertiesNV.shadingRateTexelSize.height)),
						 VK_FORMAT_R16G16_SFLOAT);
	prepareTextureTarget(&vrsSurface,
						 static_cast<uint32_t>(ceil(width / (float)physicalDeviceShadingRateImagePropertiesNV.shadingRateTexelSize.width)),
						 static_cast<uint32_t>(ceil(height / (float)physicalDeviceShadingRateImagePropertiesNV.shadingRateTexelSize.height)),
						 VK_FORMAT_R8_UINT);

	prepareTextureTarget(&preframeTexture, width, height, VK_FORMAT_B8G8R8A8_UNORM);
	setupPreframeBuffer();

	prepareShadingRateImage();
	prepareUniformBuffers();

	setupDescriptors();
	preparePipelines();
	prepareTexturePipelines();

	prepareGraphics();
	prepareNasDataCompute();

	buildCommandBuffers();
	buildPreframeCommandBuffers();
	prepared = true;
}

void VulkanExample::draw()
{	
	// Wait for rendering finished
	VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

	// Submit compute commands
	VkSubmitInfo computeSubmitInfo = vks::initializers::submitInfo();
	computeSubmitInfo.commandBufferCount = 1;
	computeSubmitInfo.pCommandBuffers = &compute.commandBuffer;
	computeSubmitInfo.waitSemaphoreCount = 1;
	computeSubmitInfo.pWaitSemaphores = &graphics.semaphore;
	computeSubmitInfo.pWaitDstStageMask = &waitStageMask;
	computeSubmitInfo.signalSemaphoreCount = 1;
	computeSubmitInfo.pSignalSemaphores = &compute.semaphore;
	VK_CHECK_RESULT(vkQueueSubmit(compute.queue, 1, &computeSubmitInfo, VK_NULL_HANDLE));

	VulkanExampleBase::prepareFrame();
	VkPipelineStageFlags graphicsWaitStageMasks[] = { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSemaphore graphicsWaitSemaphores[] = { compute.semaphore, semaphores.presentComplete };
	VkSemaphore graphicsSignalSemaphores[] = { graphics.semaphore, semaphores.renderComplete };

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &preframeCmdBuffer;
	submitInfo.waitSemaphoreCount = 2;
	submitInfo.pWaitSemaphores = graphicsWaitSemaphores;
	submitInfo.pWaitDstStageMask = graphicsWaitStageMasks;
	submitInfo.signalSemaphoreCount = 2;
	submitInfo.pSignalSemaphores = graphicsSignalSemaphores;
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
	submitInfo.waitSemaphoreCount = 2;
	submitInfo.pWaitSemaphores = graphicsWaitSemaphores;
	submitInfo.pWaitDstStageMask = graphicsWaitStageMasks;
	submitInfo.signalSemaphoreCount = 2;
	submitInfo.pSignalSemaphores = graphicsSignalSemaphores;
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
	VulkanExampleBase::submitFrame();
}

void VulkanExample::render()
{
	if (!prepared)
		return;
	VulkanExample::draw();
	if (camera.updated)
	{
		updateUniformBuffers();
	}
}

void VulkanExample::OnUpdateUIOverlay(vks::UIOverlay *overlay)
{
	if (overlay->checkBox("Enable shading rate", &enableShadingRate))
	{
		buildCommandBuffers();
		buildPreframeCommandBuffers();
	}
	if (overlay->checkBox("Color shading rates", &colorShadingRate))
	{
		updateUniformBuffers();
	}
}

/*
以下是要额外用到函数-------------------------------------------------
*/

/*
Content adaptive----------------------------------------------------
*/

void VulkanExample::prepareNasDataCompute()
{
	// Get a compute queue from the device
	vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.compute, 0, &compute.queue);

	// Create compute pipeline
	// Compute pipelines are created separate from graphics pipelines even if they use the same queue

	std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
		// Binding 0: Input image (read-only)
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 0),
		// Binding 1: Output image (write)
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1),
	};


	VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device,	&descriptorLayout, nullptr, &compute.descriptorSetLayout));

	VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
		vks::initializers::pipelineLayoutCreateInfo(&compute.descriptorSetLayout, 1);

	VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &compute.pipelineLayout));

	VkDescriptorSetAllocateInfo allocInfo =
		vks::initializers::descriptorSetAllocateInfo(descriptorPool, &compute.descriptorSetLayout, 1);

	VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &compute.descriptorSet));
	std::vector<VkWriteDescriptorSet> computeWriteDescriptorSets = {
		vks::initializers::writeDescriptorSet(compute.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, &preframeTexture.descriptor),
		vks::initializers::writeDescriptorSet(compute.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &vrsSurface.descriptor)
	};
	vkUpdateDescriptorSets(device, computeWriteDescriptorSets.size(), computeWriteDescriptorSets.data(), 0, NULL);

	// Create compute shader pipelines
	VkComputePipelineCreateInfo computePipelineCreateInfo =
		vks::initializers::computePipelineCreateInfo(compute.pipelineLayout, 0);

	computePipelineCreateInfo.stage = loadShader(getHomeworkShadersPath() + "homework2/computeNasData.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
	VkPipeline pipeline;
	VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &pipeline));
	compute.pipelines.push_back(pipeline);

	// Separate command pool as queue family for compute may be different than graphics
	VkCommandPoolCreateInfo cmdPoolInfo = {};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.queueFamilyIndex = vulkanDevice->queueFamilyIndices.compute;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &compute.commandPool));

	// Create a command buffer for compute operations
	VkCommandBufferAllocateInfo cmdBufAllocateInfo =
		vks::initializers::commandBufferAllocateInfo(
			compute.commandPool,
			VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			1);

	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &compute.commandBuffer));

	// Semaphore for compute & graphics sync
	VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
	VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &compute.semaphore));

	// Build a single command buffer containing the compute dispatch commands
	buildComputeNasDataCommandBuffer();
}

void VulkanExample::buildComputeNasDataCommandBuffer()
{
	// Flush the queue if we're rebuilding the command buffer after a pipeline change to ensure it's not currently in use
	vkQueueWaitIdle(compute.queue);

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	VK_CHECK_RESULT(vkBeginCommandBuffer(compute.commandBuffer, &cmdBufInfo));

	vkCmdBindPipeline(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelines[0]);
	vkCmdBindDescriptorSets(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineLayout, 0, 1, &compute.descriptorSet, 0, 0);

	vkCmdDispatch(compute.commandBuffer, vrsSurface.width, vrsSurface.height, 1);

	vkEndCommandBuffer(compute.commandBuffer);
}


void VulkanExample::prepareGraphics()
{
	// Semaphore for compute & graphics sync
	VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
	VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &graphics.semaphore));

	// Signal the semaphore
	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &graphics.semaphore;
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK_RESULT(vkQueueWaitIdle(queue));	
}

/*
把渲染改成渲染到texture上面再显示到屏幕上，用于获取上一帧的画面给下一帧使用
*/

void VulkanExample::prepareTexturePipelines()
{
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
		vks::initializers::pipelineInputAssemblyStateCreateInfo(
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
			0,
			VK_FALSE);

	VkPipelineRasterizationStateCreateInfo rasterizationState =
		vks::initializers::pipelineRasterizationStateCreateInfo(
			VK_POLYGON_MODE_FILL,
			VK_CULL_MODE_NONE,
			VK_FRONT_FACE_COUNTER_CLOCKWISE,
			0);

	VkPipelineColorBlendAttachmentState blendAttachmentState =
		vks::initializers::pipelineColorBlendAttachmentState(
			0xf,
			VK_FALSE);

	VkPipelineColorBlendStateCreateInfo colorBlendState =
		vks::initializers::pipelineColorBlendStateCreateInfo(
			1,
			&blendAttachmentState);

	VkPipelineDepthStencilStateCreateInfo depthStencilState =
		vks::initializers::pipelineDepthStencilStateCreateInfo(
			VK_TRUE,
			VK_TRUE,
			VK_COMPARE_OP_LESS_OR_EQUAL);

	VkPipelineViewportStateCreateInfo viewportState =
		vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

	VkPipelineMultisampleStateCreateInfo multisampleState =
		vks::initializers::pipelineMultisampleStateCreateInfo(
			VK_SAMPLE_COUNT_1_BIT,
			0);

	std::vector<VkDynamicState> dynamicStateEnables = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicState =
		vks::initializers::pipelineDynamicStateCreateInfo(
			dynamicStateEnables);
	rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
	// Rendering pipeline
	// Load shaders
	std::array<VkPipelineShaderStageCreateInfo,2> shaderStages;

	shaderStages[0] = loadShader(getHomeworkShadersPath() + "homework2/texture.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
	shaderStages[1] = loadShader(getHomeworkShadersPath() + "homework2/texture.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

	VkGraphicsPipelineCreateInfo pipelineCreateInfo =
		vks::initializers::pipelineCreateInfo(
			graphics.pipelineLayout,
			renderPass,
			0);
	VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
	pipelineCreateInfo.pVertexInputState = &emptyInputState;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.pDynamicState = &dynamicState;
	pipelineCreateInfo.stageCount = shaderStages.size();
	pipelineCreateInfo.pStages = shaderStages.data();
	pipelineCreateInfo.renderPass = renderPass;

	VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &graphics.pipeline));

}

void VulkanExample::buildPreframeCommandBuffers()
{
	if (preframeCmdBuffer == VK_NULL_HANDLE)
	{
		preframeCmdBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
	}

	VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

	VkClearValue clearValues[2];
	clearValues[0].color = defaultClearColor;
	clearValues[0].color = {{0.25f, 0.25f, 0.25f, 1.0f}};
	;
	clearValues[1].depthStencil = {1.0f, 0};

	VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
	renderPassBeginInfo.renderPass = preframeRenderPass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = width;
	renderPassBeginInfo.renderArea.extent.height = height;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;

	const VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
	const VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);

	
	renderPassBeginInfo.framebuffer = preframeFramebuffer;
	VK_CHECK_RESULT(vkBeginCommandBuffer(preframeCmdBuffer, &cmdBufInfo));
	vkCmdBeginRenderPass(preframeCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdSetViewport(preframeCmdBuffer, 0, 1, &viewport);
	vkCmdSetScissor(preframeCmdBuffer, 0, 1, &scissor);
	vkCmdBindDescriptorSets(preframeCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

	// POI: Bind the image that contains the shading rate patterns
	if (enableShadingRate)
	{
		vkCmdBindShadingRateImageNV(preframeCmdBuffer, vrsSurface.view, VK_IMAGE_LAYOUT_SHADING_RATE_OPTIMAL_NV);
	};

	// Render the scene
	Pipelines &pipelines = enableShadingRate ? shadingRatePipelines : basePipelines;

	vkCmdBindPipeline(preframeCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.opaque);
	scene.draw(preframeCmdBuffer, vkglTF::RenderFlags::BindImages | vkglTF::RenderFlags::RenderOpaqueNodes, pipelineLayout);
	vkCmdBindPipeline(preframeCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.masked);
	scene.draw(preframeCmdBuffer, vkglTF::RenderFlags::BindImages | vkglTF::RenderFlags::RenderAlphaMaskedNodes, pipelineLayout);

	vkCmdEndRenderPass(preframeCmdBuffer);
	VK_CHECK_RESULT(vkEndCommandBuffer(preframeCmdBuffer));
	
}

void VulkanExample::prepareTextureTarget(vks::Texture *tex, uint32_t width, uint32_t height, VkFormat format, VkImageLayout imageLayout)
{
	VkFormatProperties formatProperties;

	// Get device properties for the requested texture format
	vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
	// Check if requested image format supports image storage operations
	assert(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);

	// Prepare blit target texture
	tex->width = width;
	tex->height = height;

	VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = format;
	imageCreateInfo.extent = {width, height, 1};
	imageCreateInfo.mipLevels = 1;
	imageCreateInfo.arrayLayers = 1;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	// Image will be sampled in the fragment shader and used as storage target in the compute shader
	imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	imageCreateInfo.flags = 0;
	// If compute and graphics queue family indices differ, we create an image that can be shared between them
	// This can result in worse performance than exclusive sharing mode, but save some synchronization to keep the sample simple
	std::vector<uint32_t> queueFamilyIndices;
	if (vulkanDevice->queueFamilyIndices.graphics != vulkanDevice->queueFamilyIndices.compute)
	{
		queueFamilyIndices = {
			vulkanDevice->queueFamilyIndices.graphics,
			vulkanDevice->queueFamilyIndices.compute};
		imageCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
		imageCreateInfo.queueFamilyIndexCount = 2;
		imageCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();
	}

	VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
	VkMemoryRequirements memReqs;

	VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &tex->image));

	vkGetImageMemoryRequirements(device, tex->image, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &tex->deviceMemory));
	VK_CHECK_RESULT(vkBindImageMemory(device, tex->image, tex->deviceMemory, 0));

	VkCommandBuffer layoutCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	tex->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	vks::tools::setImageLayout(
		layoutCmd, tex->image,
		VK_IMAGE_ASPECT_COLOR_BIT,
		imageLayout,
		tex->imageLayout);

	vulkanDevice->flushCommandBuffer(layoutCmd, queue, true);

	// Create sampler
	VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	sampler.addressModeV = sampler.addressModeU;
	sampler.addressModeW = sampler.addressModeU;
	sampler.mipLodBias = 0.0f;
	sampler.maxAnisotropy = 1.0f;
	sampler.compareOp = VK_COMPARE_OP_NEVER;
	sampler.minLod = 0.0f;
	sampler.maxLod = tex->mipLevels;
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &tex->sampler));

	// Create image view
	VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
	view.image = VK_NULL_HANDLE;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	view.image = tex->image;
	VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &tex->view));

	// Initialize a descriptor for later use
	tex->descriptor.imageLayout = tex->imageLayout;
	tex->descriptor.imageView = tex->view;
	tex->descriptor.sampler = tex->sampler;
	tex->device = vulkanDevice;
}


void VulkanExample::setupPreframeBuffer()
{
	std::array<VkAttachmentDescription, 2> attachments = {};
	// Color attachment
	attachments[0].format = swapChain.colorFormat;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	// Depth attachment
	attachments[1].format = depthFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorReference = {};
	colorReference.attachment = 0;
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthReference = {};
	depthReference.attachment = 1;
	depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpassDescription = {};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = &depthReference;
	subpassDescription.inputAttachmentCount = 0;
	subpassDescription.pInputAttachments = nullptr;
	subpassDescription.preserveAttachmentCount = 0;
	subpassDescription.pPreserveAttachments = nullptr;
	subpassDescription.pResolveAttachments = nullptr;

	// Subpass dependencies for layout transitions
	std::array<VkSubpassDependency, 2> dependencies;

	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	dependencies[0].dependencyFlags = 0;

	dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].dstSubpass = 0;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcAccessMask = 0;
	dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	dependencies[1].dependencyFlags = 0;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &preframeRenderPass));

	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = depthFormat;
	imageCI.extent = {width, height, 1};
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &preframeDepthStencil.image));
	VkMemoryRequirements memReqs{};
	vkGetImageMemoryRequirements(device, preframeDepthStencil.image, &memReqs);

	VkMemoryAllocateInfo memAllloc{};
	memAllloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memAllloc.allocationSize = memReqs.size;
	memAllloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAllloc, nullptr, &preframeDepthStencil.mem));
	VK_CHECK_RESULT(vkBindImageMemory(device, preframeDepthStencil.image, preframeDepthStencil.mem, 0));

	VkImageViewCreateInfo imageViewCI{};
	imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCI.image = preframeDepthStencil.image;
	imageViewCI.format = depthFormat;
	imageViewCI.subresourceRange.baseMipLevel = 0;
	imageViewCI.subresourceRange.levelCount = 1;
	imageViewCI.subresourceRange.baseArrayLayer = 0;
	imageViewCI.subresourceRange.layerCount = 1;
	imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	// Stencil aspect should only be set on depth + stencil formats (VK_FORMAT_D16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8_UINT
	if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT)
	{
		imageViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &preframeDepthStencil.view));

	VkImageView viewAttachments[2];

	// Depth/Stencil attachment is the same for all frame buffers
	viewAttachments[1] = preframeDepthStencil.view;

	VkFramebufferCreateInfo frameBufferCreateInfo = {};
	frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	frameBufferCreateInfo.pNext = NULL;
	frameBufferCreateInfo.renderPass = preframeRenderPass;
	frameBufferCreateInfo.attachmentCount = 2;
	frameBufferCreateInfo.pAttachments = viewAttachments;
	frameBufferCreateInfo.width = width;
	frameBufferCreateInfo.height = height;
	frameBufferCreateInfo.layers = 1;

	// Create frame buffers for every swap chain image

	viewAttachments[0] = preframeTexture.view;
	VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &preframeFramebuffer));
}

VULKAN_EXAMPLE_MAIN()
