// Copyright Epic Games, Inc. All Rights Reserved.


// Auto-generated by spvreflect please don't modify by hand

#pragma once

struct StereoMaskConstraintReflection
{
	static constexpr uint32_t dsetCount { 1 };


	struct DescSet0_Init
	{
		VkDescriptorSetLayoutBinding bindings[3];
		uint32_t					 nBindings { 3 };

		DescSet0_Init()
		{
			// Binding for:disparityImage
			bindings[0] =
				VkDescriptorSetLayoutBinding
				{
					/*.binding*/ 0,
					/*.descriptorType*/ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					/*.descriptorCount*/ 1,
					/*.stageFlags*/ VK_SHADER_STAGE_COMPUTE_BIT,
					/*.pImmutableSamplers*/ nullptr
				};
			// Binding for:leftMask
			bindings[1] =
				VkDescriptorSetLayoutBinding
				{
					/*.binding*/ 1,
					/*.descriptorType*/ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					/*.descriptorCount*/ 1,
					/*.stageFlags*/ VK_SHADER_STAGE_COMPUTE_BIT,
					/*.pImmutableSamplers*/ nullptr
				};
			// Binding for:rightMask
			bindings[2] =
				VkDescriptorSetLayoutBinding
				{
					/*.binding*/ 2,
					/*.descriptorType*/ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
					/*.descriptorCount*/ 1,
					/*.stageFlags*/ VK_SHADER_STAGE_COMPUTE_BIT,
					/*.pImmutableSamplers*/ nullptr
				};
		}
	};

	struct DescSet0_Update
	{
		VkWriteDescriptorSet		writes[3];
		uint32_t					nWrites { 3 };

		VkDescriptorBufferInfo		disparityImageDesc{ VK_NULL_HANDLE, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo		leftMaskDesc{ VK_NULL_HANDLE, 0, VK_WHOLE_SIZE };
		VkDescriptorBufferInfo		rightMaskDesc{ VK_NULL_HANDLE, 0, VK_WHOLE_SIZE };

		DescSet0_Update(VkBuffer disparityImage, VkBuffer leftMask, VkBuffer rightMask)
		{
			disparityImageDesc.buffer = disparityImage;

			writes[0] = {};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].pNext = nullptr;
            writes[0].dstSet = 0;
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[0].pBufferInfo = &disparityImageDesc;

			leftMaskDesc.buffer = leftMask;

			writes[1] = {};
			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].pNext = nullptr;
            writes[1].dstSet = 0;
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[1].pBufferInfo = &leftMaskDesc;

			rightMaskDesc.buffer = rightMask;

			writes[2] = {};
			writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[2].pNext = nullptr;
            writes[2].dstSet = 0;
            writes[2].dstBinding = 2;
            writes[2].descriptorCount = 1;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[2].pBufferInfo = &rightMaskDesc;
		}

	};
};

