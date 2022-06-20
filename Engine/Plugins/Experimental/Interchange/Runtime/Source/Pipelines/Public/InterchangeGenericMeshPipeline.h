// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InterchangeGenericAssetsPipelineSharedSettings.h"
#include "InterchangePipelineBase.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeGenericMeshPipeline.generated.h"

class UInterchangeGenericAssetsPipeline;
class UInterchangeMeshNode;
class UInterchangePipelineMeshesUtilities;
class UInterchangeSkeletalMeshFactoryNode;
class UInterchangeSkeletalMeshLodDataNode;
class UInterchangeSkeletonFactoryNode;
class UInterchangeStaticMeshFactoryNode;
class UInterchangeStaticMeshLodDataNode;
class UPhysicsAsset;


UCLASS(BlueprintType, Experimental)
class INTERCHANGEPIPELINES_API UInterchangeGenericMeshPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()

public:

	//Common Meshes Properties Settings Pointer
	UPROPERTY(Transient)
	TObjectPtr<UInterchangeGenericCommonMeshesProperties> CommonMeshesProperties;

	//Common SkeletalMeshes And Animations Properties Settings Pointer
	UPROPERTY(Transient)
	TObjectPtr<UInterchangeGenericCommonSkeletalMeshesAndAnimationsProperties> CommonSkeletalMeshesAndAnimationsProperties;
	
	//////	STATIC_MESHES_CATEGORY Properties //////

	/** If enabled, import the static mesh asset found in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Meshes")
	bool bImportStaticMeshes = true;

	/** If enabled, all translated static mesh nodes will be imported as a single static mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Meshes")
	bool bCombineStaticMeshes = false;

	/**
	 * If enabled, meshes with certain prefixes will be imported as collision primitives for the mesh with the corresponding unprefixed name.
	 * 
	 * Supported prefixes are:
	 * UBX_ Box collision
	 * UCP_ Capsule collision
	 * USP_ Sphere collision
	 * UCX_ Convex collision
	 */

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Meshes")
	bool bImportCollisionAccordingToMeshName = true;

	/** If enabled, each UCX collision mesh will be imported as a single convex hull. If disabled, a UCX mesh will be decomposed into its separate pieces and a convex hull generated for each. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Static Meshes")
	bool bOneConvexHullPerUCX = true;

	
	//////	SKELETAL_MESHES_CATEGORY Properties //////

	/** If enable, import the animation asset find in the sources. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bImportSkeletalMeshes = true;

	/** Re-import partially or totally a skeletal mesh. You can choose betwwen geometry, skinning or everything.*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes", meta = (DisplayName = "Import Content Type"))
	EInterchangeSkeletalMeshContentType SkeletalMeshImportContentType;
	
	/** The value of the content type during the last import. This cannot be edited and is set only on successful import or re-import*/
	UPROPERTY()
	EInterchangeSkeletalMeshContentType LastSkeletalMeshImportContentType;

	/** If enable all translated skinned mesh node will be imported has a one skeletal mesh, note that it can still create several skeletal mesh for each different skeleton root joint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bCombineSkeletalMeshes = true;

	/** If enable any morph target shape will be imported. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bImportMorphTargets = true;

	/** Enable this option to update Skeleton (of the mesh)'s reference pose. Mesh's reference pose is always updated.  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bUpdateSkeletonReferencePose = false;

	/** If checked, create new PhysicsAsset if it doesn't have it */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes")
	bool bCreatePhysicsAsset = true;

	/** If this is set, use this specified PhysicsAsset. If its not set and bCreatePhysicsAsset is false, the importer will not generate or set any physic asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Skeletal Meshes", meta = (editcondition = "!bCreatePhysicsAsset"))
	TObjectPtr<UPhysicsAsset> PhysicsAsset;

protected:
	virtual void ExecutePreImportPipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& InSourceDatas) override;

	virtual void ExecutePostImportPipeline(const UInterchangeBaseNodeContainer* InBaseNodeContainer, const FString& NodeKey, UObject* CreatedAsset, bool bIsAReimport) override;

	virtual bool CanExecuteOnAnyThread(EInterchangePipelineTask PipelineTask) override
	{
		return true;
	}

	virtual void SetReimportSourceIndex(UClass* ReimportObjectClass, const int32 SourceFileIndex) override;

private:

	/* Meshes utilities, to parse the translated graph and extract the meshes informations. */
	TObjectPtr<UInterchangePipelineMeshesUtilities> PipelineMeshesUtilities = nullptr;

	/************************************************************************/
	/* Skeletal mesh API BEGIN                                              */

	/**
	 * This function will create any skeletalmesh we need to create according to the pipeline options
	 */
	void ExecutePreImportPipelineSkeletalMesh();

	/** Skeleton factory assets nodes */
	TArray<UInterchangeSkeletonFactoryNode*> SkeletonFactoryNodes;

	/** Create a UInterchangeSkeletonFactorynode */
	UInterchangeSkeletonFactoryNode* CreateSkeletonFactoryNode(const FString& RootJointUid);

	/** Skeletal mesh factory assets nodes */
	TArray<UInterchangeSkeletalMeshFactoryNode*> SkeletalMeshFactoryNodes;

	/**
	 * This function can create a UInterchangeSkeletalMeshFactoryNode
	 * @param MeshUidsPerLodIndex - The MeshUids can represent a SceneNode pointing on a MeshNode or directly a MeshNode
	 */
	UInterchangeSkeletalMeshFactoryNode* CreateSkeletalMeshFactoryNode(const FString& RootJointUid, const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex);

	/** This function can create a UInterchangeSkeletalMeshLodDataNode which represent the LOD data need by the factory to create a lod mesh */
	UInterchangeSkeletalMeshLodDataNode* CreateSkeletalMeshLodDataNode(const FString& NodeName, const FString& NodeUniqueID);

	/**
	 * This function add all lod data node to the skeletal mesh.
	 * @param NodeUidsPerLodIndex - The NodeUids can be a UInterchangeSceneNode or a UInterchangeMeshNode. The scene node can bake each instance of the mesh versus the mesh node will import only the modelled mesh.
	 */
	void AddLodDataToSkeletalMesh(const UInterchangeSkeletonFactoryNode* SkeletonFactoryNode, UInterchangeSkeletalMeshFactoryNode* SkeletalMeshFactoryNode, const TMap<int32, TArray<FString>>& NodeUidsPerLodIndex);

	/**
	 * This function will finish creating the skeletalmesh asset
	 */
	void PostImportSkeletalMesh(UObject* CreatedAsset, const UInterchangeFactoryBaseNode* FactoryNode);

	/**
	 * This function will finish creating the physics asset with the skeletalmesh render data
	 */
	void PostImportPhysicsAssetImport(UObject* CreatedAsset, const UInterchangeFactoryBaseNode* FactoryNode);
public:
	
	/** Specialize for skeletalmesh */
	void ImplementUseSourceNameForAssetOptionSkeletalMesh(const int32 MeshesImportedNodeCount, const bool bUseSourceNameForAsset);

private:
	/* Skeletal mesh API END                                                */
	/************************************************************************/


	/************************************************************************/
	/* Static mesh API BEGIN                                              */

	/**
	 * This function will create any skeletalmesh we need to create according to the pipeline options
	 */
	void ExecutePreImportPipelineStaticMesh();

	/** Static mesh factory assets nodes */
	TArray<UInterchangeStaticMeshFactoryNode*> StaticMeshFactoryNodes;

	/**
	 * This function can create a UInterchangeStaticMeshFactoryNode
	 * @param MeshUidsPerLodIndex - The MeshUids can represent a SceneNode pointing on a MeshNode or directly a MeshNode
	 */
	UInterchangeStaticMeshFactoryNode* CreateStaticMeshFactoryNode(const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex);

	/** This function can create a UInterchangeStaticMeshLodDataNode which represents the LOD data needed by the factory to create a lod mesh */
	UInterchangeStaticMeshLodDataNode* CreateStaticMeshLodDataNode(const FString& NodeName, const FString& NodeUniqueID);

	/**
	 * This function add all lod data nodes to the static mesh.
	 * @param NodeUidsPerLodIndex - The NodeUids can be a UInterchangeSceneNode or a UInterchangeMeshNode. The scene node can bake each instance of the mesh versus the mesh node will import only the modelled mesh.
	 */
	void AddLodDataToStaticMesh(UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode, const TMap<int32, TArray<FString>>& NodeUidsPerLodIndex);

	/**
	 * Return a reasonable UID and display label for a new mesh factory node.
	 */
	bool MakeMeshFactoryNodeUidAndDisplayLabel(const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex, int32 LodIndex, FString& NewMeshUid, FString& DisplayLabel);

	/* Static mesh API END                                                */
	/************************************************************************/

private:

	UInterchangeBaseNodeContainer* BaseNodeContainer = nullptr;
	TArray<const UInterchangeSourceData*> SourceDatas;

};


