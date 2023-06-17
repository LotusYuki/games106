/*
* Vulkan Example - Variable rate shading
*
* Copyright (C) 2020 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase
{
public:
	vkglTF::Model scene;

	struct ShadingRateImage {
		VkImage image;
		VkDeviceMemory memory;
		VkImageView view;
	} shadingRateImage;

	bool enableShadingRate = true;
	bool colorShadingRate = false;

	struct ShaderData {
		vks::Buffer buffer;
		struct Values {
			glm::mat4 projection;
			glm::mat4 view;
			glm::mat4 model = glm::mat4(1.0f);
			glm::vec4 lightPos = glm::vec4(0.0f, 2.5f, 0.0f, 1.0f);
			glm::vec4 viewPos;
			int32_t colorShadingRate;
		} values;
	} shaderData;

	struct Pipelines {
		VkPipeline opaque;
		VkPipeline masked;
	};

/*****************************************/
	struct {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	} preframeDepthStencil;
	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;
	struct {
		VkDescriptorSetLayout descriptorSetLayout;	// Image display shader binding layout
		VkDescriptorSet descriptorSet;	// Image display shader bindings before compute shader image manipulation
		VkPipeline pipeline;						// Image display pipeline
	    VkPipeline shadingRatePipelines;			// Image display pipeline
		VkPipelineLayout pipelineLayout;			// Layout of the graphics pipeline
		VkSemaphore semaphore;                      // Execution dependency between compute & graphic submission
	} graphics;

	struct {
		vks::Buffer computeNASDataBuffer;
		vks::Buffer adaptiveShadingConstantsBuffer;
	} uniformBuffers;

	struct ComputeNASDataConstants{
		float brightnessSensitivity = 0.07;
	}computeNASDataConstants;

	struct AdaptiveShadingConstants{
		glm::mat4 reprojectionMatrix;
		glm::vec2 previousViewOrigin;
		glm::vec2 previousViewSize;
		glm::vec2 sourceTextureSizeInv;
		float errorSensitivity = 0.07f;
		float motionSensitivity = 0.5f;
	}adaptiveShadingConstants;

	struct PreviousMatrices{
		glm::mat4 perspective;
		glm::mat4 view;
	} previousMatrices;

	struct Viewport {
		float    x;
		float    y;
		float    width;
		float    height;
		float    minX = 0.0f;
		float    minY = 0.0f;
	} preViewport;

	// Resources for the compute part of the example
	struct Compute {
		VkQueue queue;								// Separate queue for compute commands (queue family may differ from the one used for graphics)
		VkCommandPool commandPool;					// Use a separate command pool (queue family may differ from the one used for graphics)
		VkCommandBuffer commandBuffer;				// Command buffer storing the dispatch commands and barriers
		VkSemaphore semaphore;                      // Execution dependency between compute & graphic submission
		VkDescriptorSetLayout descriptorSetLayout;	// Compute shader binding layout
		VkDescriptorSet descriptorSet;				// Compute shader bindings
		VkPipelineLayout pipelineLayout;			// Layout of the compute pipeline
		std::vector<VkPipeline> pipelines;			// Compute pipelines for image filters
		int32_t pipelineIndex = 0;					// Current image filtering compute pipeline index
	} compute,computeShadingRate;
	

	VkSampler colorSampler;
	VkRenderPass preframeRenderPass;
	VkFramebuffer preframeFramebuffer;
	VkCommandBuffer preframeCmdBuffer;
	vks::Texture2D preframeTexture;
	vks::Texture2D nasDataSurface;
	vks::Texture2D vrsSurface;
	void prepareTextureTarget(vks::Texture *tex, uint32_t width, uint32_t height, VkFormat format, VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED);
	void setupPreframeBuffer();
	void buildPreframeCommandBuffers();
	void prepareTexturePipelines();
	void prepareGraphics();
	void prepareNasDataCompute();
	void buildComputeNasDataCommandBuffer();
	void prepareShadingRateCompute();
	void buildComputeShadingRateCommandBuffer();
	void initPreView();
/*****************************************/

	Pipelines basePipelines;
	Pipelines shadingRatePipelines;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	VkPhysicalDeviceShadingRateImagePropertiesNV physicalDeviceShadingRateImagePropertiesNV{};
	VkPhysicalDeviceShadingRateImageFeaturesNV enabledPhysicalDeviceShadingRateImageFeaturesNV{};
	PFN_vkCmdBindShadingRateImageNV vkCmdBindShadingRateImageNV;

	VulkanExample();
	~VulkanExample();
	virtual void getEnabledFeatures();
	void handleResize();
	void buildCommandBuffers();
	void loadglTFFile(std::string filename);
	void loadAssets();
	void prepareShadingRateImage();
	void setupDescriptors();
	void preparePipelines();
	void prepareUniformBuffers();
	void updateUniformBuffers();
	void prepare();
	void draw();
	virtual void render();
	virtual void OnUpdateUIOverlay(vks::UIOverlay* overlay);
};