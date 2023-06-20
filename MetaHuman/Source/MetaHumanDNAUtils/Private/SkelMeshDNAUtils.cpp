// Copyright Epic Games, Inc. All Rights Reserved.
#include "SkelMeshDNAUtils2.h"
#include "DNAToSkelMeshMap.h"

#include "RenderResource.h"
#include "RHICommandList.h"
#include "Async/ParallelFor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "Misc/FileHelper.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "ComponentReregisterContext.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "LODUtilities.h"

#include "AnimationRuntime.h"

#include "riglogic/RigLogic.h"

DEFINE_LOG_CATEGORY(LogDNAUtils);
/** compare based on base mesh source vertex indices */
struct FCompareMorphTargetDeltas
{
	FORCEINLINE bool operator()(const FMorphTargetDelta& A, const FMorphTargetDelta& B) const
	{
		return ((int32)A.SourceIdx - (int32)B.SourceIdx) < 0;
	}
};

USkelMeshDNAUtils2::USkelMeshDNAUtils2(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FDNAToSkelMeshMap* USkelMeshDNAUtils2::CreateMapForUpdatingNeutralMesh(IDNAReader* InDNAReader, USkeletalMesh* InSkelMesh)
{
#if WITH_EDITORONLY_DATA
	FDNAToSkelMeshMap* DNAToSkelMeshMap = new FDNAToSkelMeshMap();

	//only vertex map is initialized in this pass so we can mix neutral meshes fast (e.g. on slider move);
	//playing animation on a such a mesh requires updating joints and skin weights
	//getting full quality animation requires mixing morph targets too
	DNAToSkelMeshMap->InitBaseMesh(InDNAReader, InSkelMesh);
	return DNAToSkelMeshMap;
#else
	return nullptr;
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
/** Updates bind pose using joint positions from DNA. */
void USkelMeshDNAUtils2::UpdateJoints(USkeletalMesh* InSkelMesh, IDNAReader* InDNAReader, FDNAToSkelMeshMap* InDNAToSkelMeshMap)
{
	{	// Scoping of RefSkelModifier
		FReferenceSkeletonModifier RefSkelModifier(InSkelMesh->GetRefSkeleton(), InSkelMesh->GetSkeleton());

		// copy here
		TArray<FTransform> RawBonePose = InSkelMesh->GetRefSkeleton().GetRawRefBonePose();

		// When we are mounting the head to different bodies than female average, we need to use
		// component space, as the joint to which the head root is snapped to will be on a different
		// position than in the head rig!

		// calculate component space ahead of current transform
		TArray<FTransform> ComponentTransforms;
		FAnimationRuntime::FillUpComponentSpaceTransforms(InSkelMesh->GetRefSkeleton(), RawBonePose, ComponentTransforms);

		const TArray<FMeshBoneInfo>& RawBoneInfo = InSkelMesh->GetRefSkeleton().GetRawRefBoneInfo();

		// Skipping root joint (index 0) to avoid blinking of the mesh due to bounding box issue
		for (uint16 JointIndex = 0; JointIndex < InDNAReader->GetJointCount(); JointIndex++)
		{
			int32 BoneIndex = InDNAToSkelMeshMap->GetUEBoneIndex(JointIndex);

			FTransform DNATransform = FTransform::Identity;

			// Updating bind pose affects just translations.
			FVector Translate = InDNAReader->GetNeutralJointTranslation(JointIndex);
			DNATransform.SetTranslation(Translate);
			FVector RotationVector = InDNAReader->GetNeutralJointRotation(JointIndex);
			FRotator Rotation(RotationVector.X, RotationVector.Y, RotationVector.Z);

			// Joint 0 (spine_04) is a root of GeneSplicer joint hierarchy, and is a special case
			// 1) it is parent to itself
			// 2) it is in DNA space, so we need to rotate it 90 degs on x axis to UE4 space
			// 3) the head joints below it in the skeletal mesh are not spliced, as they are not in DNA,
			//    so they will retain female average transforms

			if (InDNAReader->GetJointParentIndex(JointIndex) == JointIndex)  //parent to itself
			{
				Rotation.Pitch += 90;
				DNATransform.SetRotation(Rotation.Quaternion());

				DNATransform.SetTranslation(FVector(Translate.X, Translate.Z, -Translate.Y));

				ComponentTransforms[BoneIndex] = DNATransform;

			}
			else
			{
				DNATransform.SetRotation(Rotation.Quaternion());

				if (ensure(RawBoneInfo[BoneIndex].ParentIndex != INDEX_NONE))
				{
					ComponentTransforms[BoneIndex] = DNATransform * ComponentTransforms[RawBoneInfo[BoneIndex].ParentIndex];
				}
			}

			ComponentTransforms[BoneIndex].NormalizeRotation();
		}

		for (uint16 BoneIndex = 0; BoneIndex < RawBoneInfo.Num(); BoneIndex++)
		{
			FTransform LocalTransform;

			if (BoneIndex == 0)
			{
				LocalTransform = ComponentTransforms[BoneIndex];
			}
			else
			{
				LocalTransform = ComponentTransforms[BoneIndex].GetRelativeTransform(ComponentTransforms[RawBoneInfo[BoneIndex].ParentIndex]);
			}

			LocalTransform.NormalizeRotation();

			RefSkelModifier.UpdateRefPoseTransform(BoneIndex, LocalTransform);
		}
	}

	InSkelMesh->GetRefBasesInvMatrix().Reset();
	InSkelMesh->CalculateInvRefMatrices(); // Needs to be called after RefSkelModifier is destroyed
}

/** Updates base mesh vertices using data from DNA. */
void USkelMeshDNAUtils2::UpdateBaseMesh(USkeletalMesh* InSkelMesh, IDNAReader* InDNAReader, FDNAToSkelMeshMap* InDNAToSkelMeshMap, ELodUpdateOption2 InUpdateOption)
{

	FSkeletalMeshModel* ImportedModel = InSkelMesh->GetImportedModel();
	// Expects vertex map to be initialized beforehand
	int32 LODStart;
	int32 LODRangeSize;
	GetLODRange(InUpdateOption, ImportedModel->LODModels.Num(), LODStart, LODRangeSize);
	for (int32 LODIndex = LODStart; LODIndex < LODRangeSize; LODIndex++)
	{
		FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
		int32 SectionIndex = 0;
		for (FSkelMeshSection& Section : LODModel.Sections)
		{
			int32& DNAMeshIndex = InDNAToSkelMeshMap->ImportVtxToDNAMeshIndex[LODIndex][Section.GetVertexBufferIndex()];

			const int32 NumSoftVertices = Section.GetNumVertices();
			auto& OverlappingMap = InDNAToSkelMeshMap->OverlappingVertices[LODIndex][SectionIndex];
			int32 VertexBufferIndex = Section.GetVertexBufferIndex();
			for (int32 VertexIndex = 0; VertexIndex < NumSoftVertices; VertexIndex++)
			{
				int32& DNAVertexIndex = InDNAToSkelMeshMap->ImportVtxToDNAVtxIndex[LODIndex][VertexBufferIndex];

				if (DNAVertexIndex >= 0)
				{
					const FVector3f Position = FVector3f(InDNAReader->GetVertexPosition(DNAMeshIndex, DNAVertexIndex));
					FSoftSkinVertex& Vertex = Section.SoftVertices[VertexIndex];
					Vertex.Position = Position;

					// Check if the current vertex has overlapping vertices, and then update them as well.
					TArray<int32>& OverlappedIndices = OverlappingMap[VertexIndex];
					int32 OverlappingCount = OverlappedIndices.Num();
					for (int32 OverlappingIndex = 0; OverlappingIndex < OverlappingCount; ++OverlappingIndex)
					{
						int32 OverlappingVertexIndex = OverlappedIndices[OverlappingIndex];
						FSoftSkinVertex& OverlappingVertex = Section.SoftVertices[OverlappingVertexIndex];
						OverlappingVertex.Position = Position;
					}
				}
				VertexBufferIndex++;
			}
			SectionIndex++;
		}
	}
}

/** Updates Morph Targets using Blend Shapes from DNA.  */
void USkelMeshDNAUtils2::UpdateMorphTargets(USkeletalMesh* InSkelMesh, IDNAReader* InDNAReader, FDNAToSkelMeshMap* InDNAToSkelMeshMap, ELodUpdateOption2 InUpdateOption)
{
	TArray<FDNABlendShapeTarget>& MeshBlendShapeTargets = InDNAToSkelMeshMap->GetMeshBlendShapeTargets();
	if (MeshBlendShapeTargets.Num() == 0)
	{
		UE_LOG(LogDNAUtils, Warning, TEXT("No morph targets updated!"));
		return;
	}
	FSkeletalMeshModel* ImportedModel = InSkelMesh->GetImportedModel();

	uint16 ChannelCount = InDNAReader->GetBlendShapeChannelCount();
	ParallelFor(InSkelMesh->GetMorphTargets().Num(), [&](int32 MorphIndex)
	{
		UMorphTarget* MorphTarget = InSkelMesh->GetMorphTargets()[MorphIndex];
		const FDNABlendShapeTarget& MeshTarget = MeshBlendShapeTargets[MorphIndex];
		// First get all DNA deltas for current Morph Target.
		TArrayView<const uint32> BlendShapeVertexIndices = InDNAReader->GetBlendShapeTargetVertexIndices(MeshTarget.MeshIndex, MeshTarget.TargetIndex);
		const int32 NumOfDeltas = BlendShapeVertexIndices.Num();
		if (NumOfDeltas > 0)
		{
			int32 LODStart;
			int32 LODRangeSize;
			GetLODRange(InUpdateOption, MorphTarget->GetMorphLODModels().Num(), LODStart, LODRangeSize);
			for (int32 LODIndex = LODStart; LODIndex < LODRangeSize; LODIndex++)
			{
				// MorphTarget vertex indices refer to full vertex index buffer of imported mesh.
				FMorphTargetLODModel& MorphLODModel = MorphTarget->GetMorphLODModels()[LODIndex];
				MorphLODModel.NumBaseMeshVerts = NumOfDeltas;
				MorphLODModel.bGeneratedByEngine = false;
				MorphLODModel.SectionIndices.Empty();
				MorphLODModel.Vertices.Reset(NumOfDeltas);
				TArrayView<const float> DeltaXs = InDNAReader->GetBlendShapeTargetDeltaXs(MeshTarget.MeshIndex, MeshTarget.TargetIndex);
				TArrayView<const float> DeltaYs = InDNAReader->GetBlendShapeTargetDeltaYs(MeshTarget.MeshIndex, MeshTarget.TargetIndex);
				TArrayView<const float> DeltaZs = InDNAReader->GetBlendShapeTargetDeltaZs(MeshTarget.MeshIndex, MeshTarget.TargetIndex);

				for (int32 DeltaIndex = 0; DeltaIndex < NumOfDeltas; DeltaIndex++)
				{
					FVector3f PositionDelta = FVector3f(DeltaXs[DeltaIndex], DeltaYs[DeltaIndex], DeltaZs[DeltaIndex]);
					FMorphTargetDelta MorphDelta;
					int32 DNAVertexIndex = BlendShapeVertexIndices[DeltaIndex];
					int32 UEVertexIndex = InDNAToSkelMeshMap->ImportDNAVtxToUEVtxIndex[LODIndex][MeshTarget.MeshIndex][DNAVertexIndex];
					if (UEVertexIndex > INDEX_NONE)
					{
						MorphDelta.SourceIdx = (uint32)UEVertexIndex;
						MorphDelta.PositionDelta = PositionDelta;
						MorphDelta.TangentZDelta = FVector3f::ZeroVector;
						MorphLODModel.Vertices.Add(MorphDelta);
						// Find section indices that are involved in these morph deltas.
						int32 SectionIdx = InDNAToSkelMeshMap->UEVertexToSectionIndices[LODIndex][UEVertexIndex];
						if (!MorphLODModel.SectionIndices.Contains(SectionIdx) && SectionIdx > INDEX_NONE)
						{
							MorphLODModel.SectionIndices.Add(SectionIdx);
						}
					}
				}
				MorphLODModel.Vertices.Sort(FCompareMorphTargetDeltas());
			}
		}
		else
		{
			int32 LODStart;
			int32 LODRangeSize;
			GetLODRange(InUpdateOption, MorphTarget->GetMorphLODModels().Num(), LODStart, LODRangeSize);
			for (int32 LODIndex = LODStart; LODIndex < LODRangeSize; LODIndex++)
			{
				FMorphTargetLODModel& MorphLODModel = MorphTarget->GetMorphLODModels()[LODIndex];
				MorphLODModel.Reset();
			}
#ifdef DEBUG
			UE_LOG(LogDNAUtils, Warning, TEXT(" 0 deltas found for mesh %d and blend shape target %d"), MeshTarget.MeshIndex, MeshTarget.TargetIndex);
#endif
		}
	});
}

/* Updates Bone influences using Skin Weights from DNA. */
void USkelMeshDNAUtils2::UpdateSkinWeights(USkeletalMesh* InSkelMesh, IDNAReader* InDNAReader, FDNAToSkelMeshMap* InDNAToSkelMeshMap, ELodUpdateOption2 InUpdateOption)
{
	FSkeletalMeshModel* ImportedModel = InSkelMesh->GetImportedModel();

	bool InfluenceMismatch = false;

	int32 LODStart;
	int32 LODRangeSize;
	GetLODRange(InUpdateOption, ImportedModel->LODModels.Num(), LODStart, LODRangeSize);
	for (int32 LODIndex = LODStart; LODIndex < LODRangeSize; ++LODIndex)
	{
		FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];
		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
		{
			FSkelMeshSection& Section = LODModel.Sections[SectionIndex];
			int32 DNAMeshIndex = InDNAToSkelMeshMap->ImportVtxToDNAMeshIndex[LODIndex][Section.GetVertexBufferIndex()];

			const int32 NumEngineVertices = Section.GetNumVertices();
			for (int32 VertexIndex = 0; VertexIndex < NumEngineVertices; VertexIndex++)
			{
				const int32 VertexBufferIndex = VertexIndex + Section.GetVertexBufferIndex();
				int32 DNAVertexIndex = InDNAToSkelMeshMap->ImportVtxToDNAVtxIndex[LODIndex][VertexBufferIndex];

				if (DNAVertexIndex < 0) continue; // Skip vertex not in DNA.

				TArrayView<const float> DNASkinWeights = InDNAReader->GetSkinWeightsValues(DNAMeshIndex, DNAVertexIndex);
				TArrayView<const uint16> DNASkinJoints = InDNAReader->GetSkinWeightsJointIndices(DNAMeshIndex, DNAVertexIndex);
				uint16 SkinJointNum = DNASkinJoints.Num();

				uint16 WeightsSum = 0;  // store all influences to vertex to ensure they add up to 255 (fix rounding errors)
				uint16 MaxInfluenceIndex = 0;
				uint16 MaxInfluenceWeight = 0;

				FSoftSkinVertex& Vertex = Section.SoftVertices[VertexIndex];

				// Update skin weights only around eyes where blue channel is not 0.
				if (Vertex.Color.B == 0) {
					//UE_LOG(LogDNAUtils, Log, TEXT("Skipping vertex UE: %d, DNA: %d"), VertexIndex, DNAVertexIndex );
					continue;
				}

				// First we have to reset all influences that are not covered by DNA data.
				for (uint16 i = SkinJointNum; i < MAX_TOTAL_INFLUENCES; i++)
				{
					Vertex.InfluenceBones[i] = INDEX_NONE;
					Vertex.InfluenceWeights[i] = 0;
				}
				for (uint16 InfluenceIndex = 0; InfluenceIndex < FMath::Min(SkinJointNum, static_cast<uint16>(MAX_TOTAL_INFLUENCES)); ++InfluenceIndex)
				{
					uint16 EngineWeight = 0;
					// Find Engine bone for corresponding DNAJoint for the same influence.
					int32 UpdatedBoneId = InDNAToSkelMeshMap->GetUEBoneIndex(DNASkinJoints[InfluenceIndex]);
					// BoneMap holds subset of bones belonging to current section.
					int32 BoneMapIndex = Section.BoneMap.Find(UpdatedBoneId);

					// Update which bone in the subset influences this vertex.
					Vertex.InfluenceBones[InfluenceIndex] = BoneMapIndex;
					if (BoneMapIndex != INDEX_NONE)
					{
						// Update influence weight.
						float PreRoundValue = 255.0f * DNASkinWeights[InfluenceIndex];
						EngineWeight = static_cast<uint16>(FMath::Min(255.0f, FMath::RoundHalfFromZero(PreRoundValue)));  // convert RL float weight to 0-255 range
					}
					Vertex.InfluenceWeights[InfluenceIndex] = static_cast<uint8>(EngineWeight);
					WeightsSum += EngineWeight;

					if (EngineWeight > MaxInfluenceWeight)
					{
						MaxInfluenceIndex = InfluenceIndex;
						MaxInfluenceWeight = EngineWeight;
					}
				}
				// Add missing fraction to fill up to 255.
				int32 ValueToAdd = 255 - WeightsSum;
				uint8 OldValue = Vertex.InfluenceWeights[MaxInfluenceIndex];
				int32 NewValue = OldValue + ValueToAdd;
				Vertex.InfluenceWeights[MaxInfluenceIndex] = static_cast<uint8>(NewValue);
			}
		}
	}
}

/** Rebuilds render data from LODModel and inits resources. */
void USkelMeshDNAUtils2::RebuildRenderData(USkeletalMesh* InSkelMesh)
{
	FPlatformTime::InitTiming();

	double StartTime = FPlatformTime::Seconds();
	{
		InSkelMesh->FlushRenderState();
	}
	double TimeToFlush = FPlatformTime::Seconds();
	{
		FSkeletalMeshRenderData* RenderData = InSkelMesh->GetResourceForRendering();
		int32 LODIndex = 0;

		for (FSkeletalMeshLODRenderData& LODRenderData : RenderData->LODRenderData)
		{
			FSkeletalMeshLODModel& LODModelRef = InSkelMesh->GetImportedModel()->LODModels[LODIndex];
			for (int32 i = 0; i < LODModelRef.Sections.Num(); i++)
			{
				FSkelMeshSection& ModelSection = LODModelRef.Sections[i];
				ModelSection.CalcMaxBoneInfluences();
				ModelSection.CalcUse16BitBoneIndex();
			}

			const FSkeletalMeshLODModel* LODModelPtr = &LODModelRef;
			LODRenderData.BuildFromLODModel(LODModelPtr, 0);
			LODIndex++;
		}
	}
	double TimeToRebuildModel = FPlatformTime::Seconds();
	{
		if (FApp::CanEverRender())
		{
			// Reinitialize the static mesh's resources.
			InSkelMesh->InitResources();
		}
	}
	double TimeToInitResources = FPlatformTime::Seconds();
	{
		// Re-register scope
		TArray<UActorComponent*> ComponentsToReregister;
		for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		{
			USkeletalMeshComponent* MeshComponent = *It;
			if (MeshComponent && !MeshComponent->IsTemplate() && MeshComponent->SkeletalMesh == InSkelMesh)
			{
				ComponentsToReregister.Add(*It);
			}
		}
		FMultiComponentReregisterContext ReregisterContext(ComponentsToReregister);
	}
}

void USkelMeshDNAUtils2::RebuildRenderData_VertexPosition(USkeletalMesh* InSkelMesh)
{
	if (!FApp::CanEverRender())
	{
		return;
	}

	{
		FSkeletalMeshModel* MeshModel = InSkelMesh->GetImportedModel();
		FSkeletalMeshRenderData* RenderData = InSkelMesh->GetResourceForRendering();

		for (int32 LODIdx = 0; LODIdx < RenderData->LODRenderData.Num(); ++LODIdx)
		{
			FSkeletalMeshLODModel& LODModel = MeshModel->LODModels[LODIdx];
			FSkeletalMeshLODRenderData& LODRenderData = RenderData->LODRenderData[LODIdx];

			ENQUEUE_RENDER_COMMAND(FSkelMeshDNAUpdatePositions)
				([&LODModel, &LODRenderData](FRHICommandListImmediate& RHICmdList)
			{
				LLM_SCOPE(ELLMTag::SkeletalMesh);
				TArray<FSoftSkinVertex> Vertices;
				LODModel.GetVertices(Vertices);

				check(Vertices.Num() == LODRenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices());
				LODRenderData.StaticVertexBuffers.PositionVertexBuffer.Init(Vertices.Num());

				for (int32 i = 0; i < Vertices.Num(); i++)
				{
					LODRenderData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(i) = Vertices[i].Position;
				}

				auto& VertexBuffer = LODRenderData.StaticVertexBuffers.PositionVertexBuffer;
				void* VertexBufferData = RHILockBuffer(VertexBuffer.VertexBufferRHI, 0, VertexBuffer.GetNumVertices() * VertexBuffer.GetStride(), RLM_WriteOnly);
				FMemory::Memcpy(VertexBufferData, VertexBuffer.GetVertexData(), VertexBuffer.GetNumVertices() * VertexBuffer.GetStride());
				RHIUnlockBuffer(VertexBuffer.VertexBufferRHI);

			});

		}
	}
}

void USkelMeshDNAUtils2::UpdateJointBehavior(USkeletalMeshComponent* InSkelMeshComponent)
{
	//DNAAsset->SetBehaviorReader is called before calling this method
	//it is not here to avoid having DNAAsset in the API, as in future we might want
	//to generalize SkelMeshUpdate to be dna-independent

	//the rig behavior has changed, we need to force re-initializing of RigLogic
	//this will set RigLogic RigUnit to initial state
	InSkelMeshComponent->InitAnim(true);
}

void USkelMeshDNAUtils2::UpdateSourceData(USkeletalMesh* InSkelMesh, IDNAReader* InDNAReader, FDNAToSkelMeshMap* InDNAToSkelMeshMap)
{
	FSkeletalMeshModel* ImportedModel = InSkelMesh->GetImportedModel();
	const int32 LODCount = ImportedModel->LODModels.Num();
	const TArray<FTransform>& RawBonePose = InSkelMesh->GetRefSkeleton().GetRawRefBonePose();
	for (int32 LODIndex = 0; LODIndex < LODCount; LODIndex++)
	{
		// Update vertices.
		FString PointsBefore;
		FString PointsAfter;
		const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];

		FSkeletalMeshImportData ImportData;
		InSkelMesh->LoadLODImportedData(LODIndex, ImportData);

		const int32 LODMeshVtxCount = LODModel.MeshToImportVertexMap.Num();
		TArray<FSoftSkinVertex> LODVertices;
		LODModel.GetVertices(LODVertices);

		TArray<SkeletalMeshImportData::FRawBoneInfluence> NewInfluences;
		TArray<bool> HasOverlappingVertices;
		HasOverlappingVertices.AddZeroed(LODMeshVtxCount);
		for (int32 LODMeshVtxIndex = 0; LODMeshVtxIndex < LODMeshVtxCount; LODMeshVtxIndex++)
		{
			// Update points.
			int32 FbxVertexIndex = LODModel.MeshToImportVertexMap[LODMeshVtxIndex];
			if (!HasOverlappingVertices[FbxVertexIndex])
			{
				HasOverlappingVertices[FbxVertexIndex] = true;
				if (FbxVertexIndex <= LODModel.MaxImportVertex)
				{
					ImportData.Points[FbxVertexIndex] = LODVertices[LODMeshVtxIndex].Position;
				}

				// Update influences.
				int32 SectionIdx;
				int32 VertexIdx;
				LODModel.GetSectionFromVertexIndex(LODMeshVtxIndex, SectionIdx, VertexIdx);
				if (LODModel.Sections[SectionIdx].SoftVertices[VertexIdx].Color.B != 0)
				{
					int32 DNAMeshIndex = InDNAToSkelMeshMap->ImportVtxToDNAMeshIndex[LODIndex][LODMeshVtxIndex];
					int32 DNAVertexIndex = InDNAToSkelMeshMap->ImportVtxToDNAVtxIndex[LODIndex][LODMeshVtxIndex];

					if (DNAVertexIndex >= 0)
					{
						TArrayView<const float> DNASkinWeights = InDNAReader->GetSkinWeightsValues(DNAMeshIndex, DNAVertexIndex);
						TArrayView<const uint16> DNASkinJoints = InDNAReader->GetSkinWeightsJointIndices(DNAMeshIndex, DNAVertexIndex);
						uint16 SkinJointNum = DNASkinJoints.Num();
						for (uint16 InfluenceIndex = 0; InfluenceIndex < SkinJointNum; ++InfluenceIndex)
						{
							float InfluenceWeight = DNASkinWeights[InfluenceIndex];
							int32 UpdatedBoneId = InDNAToSkelMeshMap->GetUEBoneIndex(DNASkinJoints[InfluenceIndex]);

							SkeletalMeshImportData::FRawBoneInfluence Influence;
							Influence.VertexIndex = FbxVertexIndex;
							Influence.BoneIndex = UpdatedBoneId;
							Influence.Weight = InfluenceWeight;
							NewInfluences.Add(Influence);
						}
						ImportData.Influences.RemoveAll([FbxVertexIndex](const SkeletalMeshImportData::FRawBoneInfluence& BoneInfluence) { return FbxVertexIndex == BoneInfluence.VertexIndex; });
					}
				}
			}
		}
		ImportData.Influences.Append(NewInfluences);
		// Sort influences by vertex index.
		FLODUtilities::ProcessImportMeshInfluences(ImportData.Wedges.Num(), ImportData.Influences, InSkelMesh->GetPathName());

		// Update reference pose.
		const int32 JointCount = LODModel.RequiredBones.Num();
		if (ImportData.RefBonesBinary.Num() == JointCount)
		{
			for (int32 JointIndex = 0; JointIndex < JointCount; JointIndex++)
			{
				const int32 OriginalBoneIndex = LODModel.RequiredBones[JointIndex];
				const FTransform3f& UpdatedTransform = FTransform3f(RawBonePose[OriginalBoneIndex]);
				ImportData.RefBonesBinary[OriginalBoneIndex].BonePos.Transform = UpdatedTransform;
			}
		}

		// Update morph targets.
		const int32 MorphTargetCount = InSkelMesh->GetMorphTargets().Num();
		ImportData.MorphTargetModifiedPoints.Empty(MorphTargetCount);
		ImportData.MorphTargetNames.Empty(MorphTargetCount);
		ImportData.MorphTargets.Empty(MorphTargetCount);
		if (LODIndex == 0)
		{
			// Blend shapes are used only in LOD0.
			for (int32 MorphIndex = 0; MorphIndex < MorphTargetCount; MorphIndex++)
			{
				UMorphTarget* MorphTarget = InSkelMesh->GetMorphTargets()[MorphIndex];
				// Add Morph target name.
				ImportData.MorphTargetNames.Add(MorphTarget->GetName());
				FSkeletalMeshImportData MorphTargetImportDeltas;
				MorphTargetImportDeltas.bDiffPose = ImportData.bDiffPose;
				MorphTargetImportDeltas.bUseT0AsRefPose = ImportData.bUseT0AsRefPose;

				FMorphTargetLODModel& MorphLODModel = MorphTarget->GetMorphLODModels()[LODIndex];

				// Init deltas and vertices for the current morph target.
				int32 NumDeltas = MorphLODModel.Vertices.Num();
				MorphTargetImportDeltas.Points.Reserve(NumDeltas);
				TSet<uint32> MorphTargetImportVertices;
				MorphTargetImportVertices.Reserve(NumDeltas);

				FMorphTargetDelta* Deltas = MorphLODModel.Vertices.GetData();
				for (int32 DeltaIndex = 0; DeltaIndex < NumDeltas; DeltaIndex++)
				{
					const uint32 SourceIndex = LODModel.MeshToImportVertexMap[Deltas[DeltaIndex].SourceIdx];
					MorphTargetImportDeltas.Points.Add(ImportData.Points[SourceIndex] + Deltas[DeltaIndex].PositionDelta);
					MorphTargetImportVertices.Add(SourceIndex);
				}
				ImportData.MorphTargetModifiedPoints.Add(MorphTargetImportVertices);
				ImportData.MorphTargets.Add(MorphTargetImportDeltas);
			}

		}

		InSkelMesh->SaveLODImportedData(LODIndex, ImportData);
	}
}

// WIP: Switching to UE5 source data calculation.
void USkelMeshDNAUtils2::UpdateSourceData(USkeletalMesh* InSkelMesh)
{
	FSkeletalMeshModel* ImportedModel = InSkelMesh->GetImportedModel();
	const int32 LODCount = ImportedModel->LODModels.Num();
	for (int32 LODIndex = 0; LODIndex < LODCount; LODIndex++)
	{
		// Update vertices.
		const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[LODIndex];

		FSkeletalMeshImportData ImportData;
		InSkelMesh->LoadLODImportedData(LODIndex, ImportData);

		TArray<FSoftSkinVertex> LODVertices;
		LODModel.GetVertices(LODVertices);
		ImportData.Influences.Empty();

		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
		{
			const FSkelMeshSection& Section = LODModel.Sections[SectionIndex];
			int32 VerticeIndexStart = Section.BaseVertexIndex;
			int32 VerticeIndexEnd = VerticeIndexStart + Section.GetNumVertices();

			int32 MaterialIndex = Section.MaterialIndex;
			ImportData.MaxMaterialIndex = FMath::Max(ImportData.MaxMaterialIndex, static_cast<uint32>(MaterialIndex));

			for (int32 VerticeIndex = VerticeIndexStart; VerticeIndex < VerticeIndexEnd; ++VerticeIndex)
			{
				const FSoftSkinVertex& Vertex = LODVertices[VerticeIndex];
				int32 FbxVertexIndex = LODModel.MeshToImportVertexMap[VerticeIndex];

				if (ImportData.Points.IsValidIndex(FbxVertexIndex))
				{
					ImportData.Points[FbxVertexIndex] = Vertex.Position;
				}

				if (Vertex.Color.B != 0)
				{
					for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
					{
						float Weight = static_cast<float>(Vertex.InfluenceWeights[InfluenceIndex]) / 255.0f;
						if (FMath::IsNearlyZero(Weight))
						{
							break;
						}
						SkeletalMeshImportData::FRawBoneInfluence& Influence = ImportData.Influences.AddDefaulted_GetRef();
						Influence.VertexIndex = FbxVertexIndex;
						Influence.BoneIndex = Section.BoneMap[Vertex.InfluenceBones[InfluenceIndex]];
						Influence.Weight = static_cast<float>(Vertex.InfluenceWeights[InfluenceIndex]) / 255.0f;
					}
				}
			}
		}
		FLODUtilities::ProcessImportMeshInfluences(ImportData.Wedges.Num(), ImportData.Influences, InSkelMesh->GetPathName());

		// Update reference pose.
		const FReferenceSkeleton& ReferenceSkeleton = InSkelMesh->GetRefSkeleton();
		const TArray<FTransform>& RawBonePose = ReferenceSkeleton.GetRawRefBonePose();
		ImportData.RefBonesBinary.Empty();
		for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetRawBoneNum(); ++BoneIndex)
		{
			SkeletalMeshImportData::FBone& RefBone = ImportData.RefBonesBinary.AddDefaulted_GetRef();
			const FMeshBoneInfo& MeshBoneInfo = ReferenceSkeleton.GetRawRefBoneInfo()[BoneIndex];
			const FTransform3f& MeshBonePose = FTransform3f(RawBonePose[BoneIndex]);
			RefBone.Name = MeshBoneInfo.ExportName;
			RefBone.BonePos.Transform = MeshBonePose;
			RefBone.ParentIndex = MeshBoneInfo.ParentIndex;
			RefBone.NumChildren = 0;
			if (ImportData.RefBonesBinary.IsValidIndex(RefBone.ParentIndex))
			{
				SkeletalMeshImportData::FBone& ParentBone = ImportData.RefBonesBinary[RefBone.ParentIndex];
				ParentBone.NumChildren++;
			}
		}

		// Update morph targets.
		const int32 MorphTargetCount = InSkelMesh->GetMorphTargets().Num();
		ImportData.MorphTargetModifiedPoints.Empty(MorphTargetCount);
		ImportData.MorphTargetNames.Empty(MorphTargetCount);
		ImportData.MorphTargets.Empty(MorphTargetCount);
		if (LODIndex == 0)
		{
			// Blend shapes are used only in LOD0.
			for (int32 MorphIndex = 0; MorphIndex < MorphTargetCount; MorphIndex++)
			{
				UMorphTarget* MorphTarget = InSkelMesh->GetMorphTargets()[MorphIndex];
				// Add Morph target name.
				ImportData.MorphTargetNames.Add(MorphTarget->GetName());
				FSkeletalMeshImportData MorphTargetImportDeltas;
				MorphTargetImportDeltas.bDiffPose = ImportData.bDiffPose;
				MorphTargetImportDeltas.bUseT0AsRefPose = ImportData.bUseT0AsRefPose;

				FMorphTargetLODModel& MorphLODModel = MorphTarget->GetMorphLODModels()[LODIndex];

				// Init deltas and vertices for the current morph target.
				int32 NumDeltas = MorphLODModel.Vertices.Num();
				MorphTargetImportDeltas.Points.Reserve(NumDeltas);
				TSet<uint32> MorphTargetImportVertices;
				MorphTargetImportVertices.Reserve(NumDeltas);

				FMorphTargetDelta* Deltas = MorphLODModel.Vertices.GetData();
				for (int32 DeltaIndex = 0; DeltaIndex < NumDeltas; DeltaIndex++)
				{
					const uint32 SourceIndex = LODModel.MeshToImportVertexMap[Deltas[DeltaIndex].SourceIdx];
					MorphTargetImportDeltas.Points.Add(ImportData.Points[SourceIndex] + Deltas[DeltaIndex].PositionDelta);
					MorphTargetImportVertices.Add(SourceIndex);
				}
				ImportData.MorphTargetModifiedPoints.Add(MorphTargetImportVertices);
				ImportData.MorphTargets.Add(MorphTargetImportDeltas);
			}

		}

		InSkelMesh->SaveLODImportedData(LODIndex, ImportData);
		InSkelMesh->SetLODImportedDataVersions(LODIndex, ESkeletalMeshGeoImportVersions::LatestVersion, ESkeletalMeshSkinningImportVersions::LatestVersion);
	}
}

#endif // WITH_EDITORONLY_DATA
