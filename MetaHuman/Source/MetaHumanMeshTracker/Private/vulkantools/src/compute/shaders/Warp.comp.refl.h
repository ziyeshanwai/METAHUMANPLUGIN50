// Copyright Epic Games, Inc. All Rights Reserved.


// Auto-generated by spvreflect please don't modify by hand

#pragma once

struct WarpReflection
{
	static constexpr uint32_t dsetCount { 1 };


	struct DescSet0_Init
	{
		VkDescriptorSetLayoutBinding bindings[4];
		uint32_t					 nBindings { 4 };

		DescSet0_Init()
		{
			// Binding for:input
			bindings[0] =
				VkDescriptorSetLayoutBinding
				{
					/*.binding*/ 0,
					/*.descriptorType*/ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					/*.descriptorCount*/ 1,
					/*.stageFlags*/ VK_SHADER_STAGE_COMPUTE_BIT,
					/*.pImmutableSamplers*/ nullptr
				};
			// Binding for:offsetX
			bindings[1] =
				VkDescriptorSetLayoutBinding
				{
					/*.binding*/ 1,
					/*.descriptorType*/ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					/*.descriptorCount*/ 1,
					/*.stageFlags*/ VK_SHADER_STAGE_COMPUTE_BIT,
					/*.pImmutableSamplers*/ nullptr
				};
			// Binding for:offsetY
			bindings[2] =
				VkDescriptorSetLayoutBinding
				{
					/*.binding*/ 2,
					/*.descriptorType*/ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					/*.descriptorCount*/ 1,
					/*.stageFlags*/ VK_SHADER_STAGE_COMPUTE_BIT,
					/*.pImmutableSamplers*/ nullptr
				};
			// Binding for:output
			bindings[3] =
				VkDescriptorSetLayoutBinding
				{
					/*.binding*/ 3,
					/*.descriptorType*/ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					/*.descriptorCount*/ 1,
					/*.stageFlags*/ VK_SHADER_STAGE_COMPUTE_BIT,
					/*.pImmutableSamplers*/ nullptr
				};
		}
	};

	struct DescSet0_Update
	{
		VkWriteDescriptorSet		writes[4];
		uint32_t					nWrites { 4 };

		VkDescriptorBufferInfo		inputDesc{ VK_NULL_HANDLE, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo		offsetXDesc{ VK_NULL_HANDLE, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo		offsetYDesc{ VK_NULL_HANDLE, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo		outputDesc{ VK_NULL_HANDLE, 0, VK_WHOLE_SIZE };

		DescSet0_Update(VkBuffer input, VkBuffer offsetX, VkBuffer offsetY, VkBuffer output)
		{
			inputDesc.buffer = input;

			writes[0] = {};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].pNext = nullptr;
            writes[0].dstSet = 0;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].pBufferInfo = &inputDesc;

			offsetXDesc.buffer = offsetX;

			writes[1] = {};
			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].pNext = nullptr;
            writes[1].dstSet = 0;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].pBufferInfo = &offsetXDesc;

			offsetYDesc.buffer = offsetY;

			writes[2] = {};
			writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[2].pNext = nullptr;
            writes[2].dstSet = 0;
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[2].pBufferInfo = &offsetYDesc;

			outputDesc.buffer = output;

			writes[3] = {};
			writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[3].pNext = nullptr;
            writes[3].dstSet = 0;
            writes[3].dstBinding = 3;
            writes[3].descriptorCount = 1;
            writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[3].pBufferInfo = &outputDesc;
		}

	};
};

