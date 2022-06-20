// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimSceneAsset.generated.h"

class UContextualAnimScenePivotProvider;
class UContextualAnimSceneInstance;

USTRUCT(BlueprintType)
struct FContextualAnimAlignmentSectionData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FName WarpTargetName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (GetOptions = "GetRoles"))
	FName Origin = NAME_None;
		
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	bool bAlongClosestDistance = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (GetOptions = "GetRoles", EditCondition = "bAlongClosestDistance"))
	FName OtherRole = NAME_None;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bAlongClosestDistance"))
	float Weight = 0.f;
};

USTRUCT(BlueprintType)
struct FContextualAnimRoleDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TSubclassOf<AActor> PreviewActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FTransform MeshToComponent = FTransform(FRotator(0.f, -90.f, 0.f));
};

UCLASS(Blueprintable)
class CONTEXTUALANIMATION_API UContextualAnimRolesAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TArray<FContextualAnimRoleDefinition> Roles;

	UContextualAnimRolesAsset(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {};

	const FContextualAnimRoleDefinition* FindRoleDefinitionByName(const FName& Name) const
	{
		return Roles.FindByPredicate([Name](const FContextualAnimRoleDefinition& RoleDef) { return RoleDef.Name == Name; });
	}
};

USTRUCT(BlueprintType)
struct FContextualAnimTracksContainer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TArray<FContextualAnimTrack> Tracks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TArray<FTransform> ScenePivots;
};

UCLASS(Blueprintable)
class CONTEXTUALANIMATION_API UContextualAnimSceneAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	typedef TFunctionRef<UE::ContextualAnim::EForEachResult(const FContextualAnimTrack& AnimTrack)> FForEachAnimTrackFunction;

	UContextualAnimSceneAsset(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	void PrecomputeData();
	
	void ForEachAnimTrack(FForEachAnimTrackFunction Function) const;

	void ForEachAnimTrack(int32 VariantIdx, FForEachAnimTrackFunction Function) const;

	FORCEINLINE const FName& GetPrimaryRole() const { return PrimaryRole; }
	FORCEINLINE float GetRadius() const { return Radius; }
	FORCEINLINE bool GetDisableCollisionBetweenActors() const { return bDisableCollisionBetweenActors; }
	FORCEINLINE const TSubclassOf<UContextualAnimSceneInstance>& GetSceneInstanceClass() const { return SceneInstanceClass; }
	FORCEINLINE const TArray<FContextualAnimAlignmentSectionData>& GetAlignmentSections() const { return  AlignmentSections; }

	UFUNCTION()
	TArray<FName> GetRoles() const;

	const FContextualAnimTrack* GetAnimTrack(const FName& Role, int32 VariantIdx) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	int32 FindVariantIdx(FName Role, UAnimMontage* Animation) const;

	FName FindRoleByAnimation(const UAnimMontage* Animation) const;

	const FContextualAnimTrack* FindFirstAnimTrackForRoleThatPassesSelectionCriteria(const FName& Role, const FContextualAnimPrimaryActorData& PrimaryActorData, const FContextualAnimQuerierData& QuerierData) const;
	
	const FContextualAnimTrack* FindAnimTrackForRoleWithClosestEntryLocation(const FName& Role, const FContextualAnimPrimaryActorData& PrimaryActorData, const FVector& TestLocation) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	FTransform GetAlignmentTransformForRoleRelativeToScenePivot(FName Role, int32 VariantIdx, float Time) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	FTransform GetAlignmentTransformForRoleRelativeToOtherRole(FName FromRole, FName ToRole, int32 VariantIdx, float Time) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	FTransform GetIKTargetTransformForRoleAtTime(FName Role, int32 VariantIdx, FName TrackName, float Time) const;

	const FContextualAnimIKTargetDefContainer& GetIKTargetDefsForRole(const FName& Role) const;

	const FTransform& GetMeshToComponentForRole(const FName& Role) const;

	int32 GetTotalVariants() const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset", meta = (DisplayName = "Get Anim Track"))
	const FContextualAnimTrack& BP_GetAnimTrack(FName Role, int32 VariantIdx) const
	{
		const FContextualAnimTrack* AnimTrack = GetAnimTrack(Role, VariantIdx);
		return AnimTrack ? *AnimTrack : FContextualAnimTrack::EmptyTrack;
	}

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	bool Query(FName Role, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const;

protected:

	UPROPERTY(EditAnywhere, Category = "Defaults")
	TObjectPtr<UContextualAnimRolesAsset> RolesAsset;

	UPROPERTY(EditAnywhere, Category = "Defaults", meta = (GetOptions = "GetRoles"))
	FName PrimaryRole = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Defaults")
	TArray<FContextualAnimTracksContainer> Variants;

	UPROPERTY(EditAnywhere, Category = "Defaults")
	TMap<FName, FContextualAnimIKTargetDefContainer> RoleToIKTargetDefsMap;

	UPROPERTY(EditAnywhere, Category = "Defaults")
	TArray<FContextualAnimAlignmentSectionData> AlignmentSections;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TSubclassOf<UContextualAnimSceneInstance> SceneInstanceClass;

	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bDisableCollisionBetweenActors = true;

	/** Sample rate (frames per second) used when sampling the animations to generate alignment and IK tracks */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "1", ClampMax = "60"), AdvancedDisplay)
	int32 SampleRate = 15;

	UPROPERTY(VisibleAnywhere, Category = "Settings")
	float Radius = 0.f;

	void GenerateAlignmentTracks();

	void GenerateIKTargetTracks();

	void UpdateRadius();

	friend class UContextualAnimUtilities;
	friend class FContextualAnimViewModel;
	friend class FContextualAnimEdMode;
	friend class FContextualAnimMovieSceneNotifyTrackEditor;
};
