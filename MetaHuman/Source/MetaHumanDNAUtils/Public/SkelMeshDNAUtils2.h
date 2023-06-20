// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DNAToSkelMeshMap.h"
#include "Engine/SkeletalMesh.h"
#include "SkelMeshDNAUtils2.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDNAUtils, Log, All);

class IDNAReader;
class FDNAToSkelMeshMap;

UENUM()
enum class ELodUpdateOption2 : uint8
{
	LOD0Only, 		// LOD0 Only
	LOD1AndHigher, 	// LOD1 and higher
	All				// All LODs
};

/** A utility class for updating SkeletalMesh joints, base mesh, morph targets and skin weights according to DNA data.
  * After the update, the render data is re-chunked.
 **/
UCLASS(transient)
class METAHUMANDNAUTILS_API USkelMeshDNAUtils2: public UObject
{
	GENERATED_UCLASS_BODY()

public:

	/** Prepare context object that will allow mapping of DNA structures to SkelMesh ones for updating **/
	static FDNAToSkelMeshMap* CreateMapForUpdatingNeutralMesh(IDNAReader* InDNAReader, USkeletalMesh* InSkelMesh);

#if WITH_EDITORONLY_DATA
	/** Updates the positions, orientation and scale in the joint hierarchy using the data from DNA file **/
	static void UpdateJoints(USkeletalMesh* InSkelMesh, IDNAReader* InDNAReader, FDNAToSkelMeshMap* InDNAToSkelMeshMap);
	/** Updates the base mesh vertex positions for all mesh sections of all LODs, using the data from DNA file 
	  * NOTE: Not calling RebuildRenderData automatically, it needs to be called explicitly after the first update
	  *       As the topology doesn't change, for subsequent updates it can be ommited to gain performance **/
	static void UpdateBaseMesh(USkeletalMesh* InSkelMesh, IDNAReader* InDNAReader, FDNAToSkelMeshMap* InDNAToSkelMeshMap, ELodUpdateOption2 InUpdateOption = ELodUpdateOption2::LOD0Only);
	/** Updates the morph targets for all mesh sections of LODs, using the data from DNA file **/
	static void UpdateMorphTargets(USkeletalMesh* InSkelMesh, IDNAReader* InDNAReader, FDNAToSkelMeshMap* InDNAToSkelMeshMap, ELodUpdateOption2 InUpdateOption = ELodUpdateOption2::LOD0Only);
	/** Updates the skin weights for all LODs using the data from DNA file **/
	static void UpdateSkinWeights(USkeletalMesh* InSkelMesh, IDNAReader* InDNAReader, FDNAToSkelMeshMap* InDNAToSkelMeshMap, ELodUpdateOption2 InUpdateOption = ELodUpdateOption2::LOD0Only);
	/** Rechunks the mesh after the update **/
	static void RebuildRenderData(USkeletalMesh* InSkelMesh);
	/** Re-initialize vertex positions for rendering after the update **/
	static void RebuildRenderData_VertexPosition(USkeletalMesh* InSkelMesh);
	/** Update joint behavior **/
	/*  NOTE: DNAAsset->SetBehaviorReader needs to be called before this */
	static void UpdateJointBehavior(USkeletalMeshComponent* InSkelMeshComponent);
#endif // WITH_EDITORONLY_DATA
	/** Converts DNA vertex coordinates to UE4 coordinate system **/
	inline static FVector ConvertDNAVertexToUE4CoordSystem(FVector InVertexPositionInDNA)
	{
		return FVector{-InVertexPositionInDNA.X, InVertexPositionInDNA.Y, -InVertexPositionInDNA.Z};
	}

	/** Converts UE4 coordinate system to DNA vertex coordinates **/
	inline static FVector ConvertUE4CoordSystemToDNAVertex(FVector InVertexPositionInUE4)
	{
		return FVector{-InVertexPositionInUE4.X, InVertexPositionInUE4.Y, -InVertexPositionInUE4.Z};
	}

	/** Updates source skeleton data for the purpose of character cooking and export.  */
	static void UpdateSourceData(USkeletalMesh* InSkelMesh);
	static void UpdateSourceData(USkeletalMesh* InSkelMesh, IDNAReader* InDNAReader, FDNAToSkelMeshMap* InDNAToSkelMeshMap);
	
private:

	inline static void GetLODRange(ELodUpdateOption2 InUpdateOption, const int32& LODNum, int32& LODStart, int32& LODRangeSize)
	{
		LODStart = 0;
		LODRangeSize = LODNum;
		if (InUpdateOption == ELodUpdateOption2::LOD1AndHigher)
		{
			LODStart = 1;
		}
		else if (InUpdateOption == ELodUpdateOption2::LOD0Only && LODRangeSize > 0)
		{
			LODRangeSize = 1;
		}
	}
};
