// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * WorldPartitionLevelHelper implementation
 */

#include "WorldPartition/WorldPartitionLevelHelper.h"
#include "WorldPartition/WorldPartitionPackageHelper.h"

#if WITH_EDITOR

#include "FileHelpers.h"
#include "Model.h"
#include "UnrealEngine.h"
#include "UObject/UObjectHash.h"
#include "WorldPartition/ActorDescContainer.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionPackageHelper.h"
#include "StaticMeshCompiler.h"
#include "LevelUtils.h"
#include "Templates/SharedPointer.h"
#include "ActorFolder.h"

TMap<FName, FWorldPartitionLevelHelper::FPackageReference> FWorldPartitionLevelHelper::PackageReferences;

void FWorldPartitionLevelHelper::FPackageReferencer::AddReference(UPackage* InPackage)
{
	check(InPackage);
	FPackageReference& RefInfo = FWorldPartitionLevelHelper::PackageReferences.FindOrAdd(InPackage->GetFName());
	check(RefInfo.Package == nullptr || RefInfo.Package == InPackage);
	RefInfo.Package = InPackage;
	RefInfo.Referencers.Add(this);
}

void FWorldPartitionLevelHelper::FPackageReferencer::RemoveReferences()
{
	for (auto It = FWorldPartitionLevelHelper::PackageReferences.CreateIterator(); It; ++It)
	{
		FPackageReference& RefInfo = It->Value;
		RefInfo.Referencers.Remove(this);
		if (RefInfo.Referencers.Num() == 0)
		{
			if (UPackage* Package = RefInfo.Package.Get())
			{
				FWorldPartitionPackageHelper::UnloadPackage(Package);
			}
			It.RemoveCurrent();
		}
	}
}


 /**
  * Defaults World's initialization values for World Partition StreamingLevels
  */
UWorld::InitializationValues FWorldPartitionLevelHelper::GetWorldInitializationValues()
{
	return UWorld::InitializationValues()
		.InitializeScenes(false)
		.AllowAudioPlayback(false)
		.RequiresHitProxies(false)
		.CreatePhysicsScene(false)
		.CreateNavigation(false)
		.CreateAISystem(false)
		.ShouldSimulatePhysics(false)
		.EnableTraceCollision(false)
		.SetTransactional(false)
		.CreateFXSystem(false);
}

/**
 * Moves external actors into the given level
 */
void FWorldPartitionLevelHelper::MoveExternalActorsToLevel(const TArray<FWorldPartitionRuntimeCellObjectMapping>& InChildPackages, ULevel* InLevel)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FWorldPartitionLevelHelper::MoveExternalActorsToLevel);

	// We can't have async compilation still going on while we move actors as this is going to ResetLoaders which will move bulkdata around that
	// might still be used by async compilation. 
	// #TODO_DC Revisit once virtualbulkdata are enabled
	FStaticMeshCompilingManager::Get().FinishAllCompilation();

	check(InLevel);
	UPackage* LevelPackage = InLevel->GetPackage();
	
	// Move all actors to Cell level
	for (const FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping : InChildPackages)
	{
		// We assume actor failed to duplicate if LoadedPath equals NAME_None (warning already logged we can skip this mapping)
		if (PackageObjectMapping.LoadedPath == NAME_None && !PackageObjectMapping.ContainerID.IsMainContainer())
		{
			continue;
		}

		AActor* Actor = FindObject<AActor>(nullptr, *PackageObjectMapping.LoadedPath.ToString());
		if (Actor)
		{
			UPackage* ActorExternalPackage = Actor->GetPackage();

			const bool bSameOuter = (InLevel == Actor->GetOuter());
			Actor->SetPackageExternal(false, false);
						
			// Avoid calling Rename on the actor if it's already outered to InLevel as this will cause it's name to be changed. 
			// (UObject::Rename doesn't check if Rename is being called with existing outer and assigns new name)
			if (!bSameOuter)
			{
				Actor->Rename(nullptr, InLevel, REN_ForceNoResetLoaders);
			}
			
			check(Actor->GetPackage() == LevelPackage);
			if (bSameOuter && !InLevel->Actors.Contains(Actor))
			{
				InLevel->AddLoadedActor(Actor);
			}

			// Include objects found in the source actor package in the destination level package
			if (ensure(ActorExternalPackage))
			{
				TArray<UObject*> Objects;
				const bool bIncludeNestedSubobjects = false;
				GetObjectsWithOuter(ActorExternalPackage, Objects, bIncludeNestedSubobjects);
				for (UObject* Object : Objects)
				{
					if (Object->GetFName() != NAME_PackageMetaData)
					{
						Object->Rename(nullptr, LevelPackage, REN_ForceNoResetLoaders);
					}
				}
			}
		}
		else
		{
			UE_LOG(LogEngine, Warning, TEXT("Can't find actor %s."), *PackageObjectMapping.Path.ToString());
		}
	}
}

void FWorldPartitionLevelHelper::RemapLevelSoftObjectPaths(ULevel* InLevel, UWorldPartition* InWorldPartition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FWorldPartitionLevelHelper::RemapLevelSoftObjectPaths);

	check(InLevel);
	check(InWorldPartition);

	FSoftObjectPathFixupArchive FixupSerializer([InWorldPartition](FSoftObjectPath& Value)
	{
		if(!Value.IsNull())
		{
			InWorldPartition->RemapSoftObjectPath(Value);
		}
	});
	FixupSerializer.Fixup(InLevel);
}

FString FWorldPartitionLevelHelper::AddActorContainerIDToSubPathString(const FActorContainerID& InContainerID, const FString& InSubPathString)
{
	if (!InContainerID.IsMainContainer())
	{
		constexpr const TCHAR PersistenLevelName[] = TEXT("PersistentLevel.");
		constexpr const int32 DotPos = UE_ARRAY_COUNT(PersistenLevelName);
		if (InSubPathString.StartsWith(PersistenLevelName))
		{
			const int32 SubObjectPos = InSubPathString.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromStart, DotPos);
			if (SubObjectPos == INDEX_NONE)
			{
				return InSubPathString + TEXT("_") + InContainerID.ToString();
			}
			else
			{
				return InSubPathString.Mid(0, SubObjectPos) + TEXT("_") + InContainerID.ToString() + InSubPathString.Mid(SubObjectPos);
			}
		}
	}

	return InSubPathString;
}

FString FWorldPartitionLevelHelper::AddActorContainerIDToActorPath(const FActorContainerID& InContainerID, const FString& InActorPath)
{
	if (!InContainerID.IsMainContainer())
	{
		const FSoftObjectPath SoftObjectPath(InActorPath);
		const FString NewSubPathString = FWorldPartitionLevelHelper::AddActorContainerIDToSubPathString(InContainerID, SoftObjectPath.GetSubPathString());
		return FSoftObjectPath(SoftObjectPath.GetAssetPathName(), NewSubPathString).ToString();
	}

	return InActorPath;
}

/**
 * Creates an empty Level used in World Partition
 */
ULevel* FWorldPartitionLevelHelper::CreateEmptyLevelForRuntimeCell(const UWorld* InWorld, const FString& InWorldAssetName, UPackage* InPackage)
{
	// Create or use given package
	UPackage* CellPackage = nullptr;
	if (InPackage)
	{
		check(FindObject<UPackage>(nullptr, *InPackage->GetName()));
		CellPackage = InPackage;
	}
	else
	{
		FString PackageName = FPackageName::ObjectPathToPackageName(InWorldAssetName);
		check(!FindObject<UPackage>(nullptr, *PackageName));
		CellPackage = CreatePackage(*PackageName);
		CellPackage->SetPackageFlags(PKG_NewlyCreated);
	}

	if (InWorld->IsPlayInEditor())
	{
		check(!InPackage);
		CellPackage->SetPackageFlags(PKG_PlayInEditor);
		CellPackage->SetPIEInstanceID(InWorld->GetPackage()->GetPIEInstanceID());
	}

	// Create World & Persistent Level
	UWorld::InitializationValues IVS = FWorldPartitionLevelHelper::GetWorldInitializationValues();
	const FName WorldName = FName(FPackageName::ObjectPathToObjectName(InWorldAssetName));
	UWorld* NewWorld = UWorld::CreateWorld(InWorld->WorldType, /*bInformEngineOfWorld*/false, WorldName, CellPackage, /*bAddToRoot*/false, InWorld->FeatureLevel, &IVS, /*bInSkipInitWorld*/true);
	check(NewWorld);
	NewWorld->SetFlags(RF_Public | RF_Standalone);
	check(NewWorld->GetWorldSettings());
	check(UWorld::FindWorldInPackage(CellPackage) == NewWorld);
	check(InPackage || (NewWorld->GetPathName() == InWorldAssetName));
	
	// Setup of streaming cell Runtime Level
	ULevel* NewLevel = NewWorld->PersistentLevel;
	check(NewLevel);
	check(NewLevel->GetFName() == InWorld->PersistentLevel->GetFName());
	check(NewLevel->OwningWorld == NewWorld);
	check(NewLevel->Model);
	check(!NewLevel->bIsVisible);

	// Mark the level as a runtime cell
	NewLevel->bIsWorldPartitionRuntimeCell = true;
	
	// Mark the level package as fully loaded
	CellPackage->MarkAsFullyLoaded();

	// Mark the level package as containing a map
	CellPackage->ThisContainsMap();

	// Set the guids on the constructed level to something based on the generator rather than allowing indeterminism by
	// constructing new Guids on every cook
	// @todo_ow: revisit for static lighting support. We need to base the LevelBuildDataId on the relevant information from the
	// actor's package.
	NewLevel->LevelBuildDataId = InWorld->PersistentLevel->LevelBuildDataId;
	check(InWorld->PersistentLevel->Model && NewLevel->Model);
	NewLevel->Model->LightingGuid = InWorld->PersistentLevel->Model->LightingGuid;

	return NewLevel;
}

void FWorldPartitionLevelHelper::DuplicateActorFolderToRuntimeCell(ULevel* CellLevel, ULevel* SrcLevel, const FGuid& ActorFolderGuid)
{
	check(SrcLevel->IsUsingActorFolders() && CellLevel->IsUsingActorFolders());
		
	FGuid CurrentGuid = ActorFolderGuid;
	while (CurrentGuid.IsValid() && !CellLevel->GetActorFolder(CurrentGuid))
	{
		const bool bSkipDeleted = false;
		UActorFolder* ActorFolder = SrcLevel->GetActorFolder(CurrentGuid, bSkipDeleted);
		CurrentGuid.Invalidate();
		if (ActorFolder)
		{
			CurrentGuid.Invalidate();
			FObjectDuplicationParameters DupParams = InitStaticDuplicateObjectParams(ActorFolder, CellLevel);
			DupParams.ApplyFlags |= RF_Transient;
			DupParams.FlagMask &= ~RF_Transactional;
			DupParams.bAssignExternalPackages = false;
			UActorFolder* DuplicatedFolder = Cast<UActorFolder>(StaticDuplicateObjectEx(DupParams));

			const bool bShouldDirtyLevel = false;
			const bool bShouldBroadcast = false;
			FLevelActorFoldersHelper::AddActorFolder(CellLevel, DuplicatedFolder, bShouldDirtyLevel, bShouldBroadcast);

			// Continue to parent
			if (UActorFolder* ParentFolder = ActorFolder->GetParent(bSkipDeleted))
			{
				CurrentGuid = ParentFolder->GetGuid();
			}
		}
	}
}

bool FWorldPartitionLevelHelper::LoadActors(UWorld* InOwningWorld, ULevel* InDestLevel, TArrayView<FWorldPartitionRuntimeCellObjectMapping> InActorPackages, FWorldPartitionLevelHelper::FPackageReferencer& InPackageReferencer, TFunction<void(bool)> InCompletionCallback, bool bInLoadAsync, FLinkerInstancingContext InstancingContext)
{
	UPackage* DestPackage = InDestLevel ? InDestLevel->GetPackage() : nullptr;
	FString ShortLevelPackageName = DestPackage? FPackageName::GetShortName(DestPackage->GetFName()) : FString();

	struct FLoadProgress
	{
		int32 NumPendingLoadRequests = 0;
		int32 NumFailedLoadedRequests = 0;
	};
	TSharedPtr<FLoadProgress> LoadProgress = MakeShared<FLoadProgress>();

	// Actors to load
	TArray<FWorldPartitionRuntimeCellObjectMapping*> ActorPackages;
	ActorPackages.Reserve(InActorPackages.Num());

	TMap<FActorContainerID, FLinkerInstancingContext> LinkerInstancingContexts;
	// Add Main container context
	LinkerInstancingContexts.Add(FActorContainerID::GetMainContainerID(), MoveTemp(InstancingContext));
			
	for (FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping : InActorPackages)
	{
		FLinkerInstancingContext* Context = LinkerInstancingContexts.Find(PackageObjectMapping.ContainerID);
		if (!Context)
		{
			check(!PackageObjectMapping.ContainerID.IsMainContainer());
		
			FString ContainerPackageName = PackageObjectMapping.ContainerPackage.ToString();
			if (InDestLevel && InDestLevel->GetPackage()->GetPIEInstanceID() != INDEX_NONE)
			{
				ContainerPackageName = UWorld::ConvertToPIEPackageName(ContainerPackageName, InDestLevel->GetPackage()->GetPIEInstanceID());
			}
			
			const FName ContainerPackageInstanceName(*FString::Printf(TEXT("/Temp%s_%s"), *ContainerPackageName, *PackageObjectMapping.ContainerID.ToString()));

			FLinkerInstancingContext& NewContext = LinkerInstancingContexts.Add(PackageObjectMapping.ContainerID);
			NewContext.AddTag(ULevel::DontLoadExternalObjectsTag);
			NewContext.AddMapping(PackageObjectMapping.ContainerPackage, ContainerPackageInstanceName);
			Context = &NewContext;
		}
		
		const FName ContainerPackageInstanceName = Context->Remap(PackageObjectMapping.ContainerPackage);
		if (PackageObjectMapping.ContainerPackage != ContainerPackageInstanceName)
		{
			const FString ActorPackageName = FPackageName::ObjectPathToPackageName(PackageObjectMapping.Package.ToString());
			const FString ActorPackageInstanceName = ULevel::GetExternalActorPackageInstanceName(ContainerPackageInstanceName.ToString(), ActorPackageName);

			Context->AddMapping(FName(*ActorPackageName), FName(*ActorPackageInstanceName));
		}
		ActorPackages.Add(&PackageObjectMapping);
	}

	LoadProgress->NumPendingLoadRequests = ActorPackages.Num();

	for (FWorldPartitionRuntimeCellObjectMapping* PackageObjectMapping : ActorPackages)
	{
		FLoadPackageAsyncDelegate CompletionCallback = FLoadPackageAsyncDelegate::CreateLambda([LoadProgress, PackageObjectMapping, &InPackageReferencer, InOwningWorld, InDestLevel, InCompletionCallback](const FName& LoadedPackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
		{
			const FName ActorName = *FPaths::GetExtension(PackageObjectMapping->Path.ToString());
			check(LoadProgress->NumPendingLoadRequests);
			LoadProgress->NumPendingLoadRequests--;

			AActor* Actor = LoadedPackage ? FindObject<AActor>(LoadedPackage, *ActorName.ToString()) : nullptr;

			if (Actor)
			{
				const UWorld* ContainerWorld = PackageObjectMapping->ContainerID.IsMainContainer() ? InOwningWorld : Actor->GetTypedOuter<UWorld>();
				
				// Duplicate Folder if needed (this will recreate a transient folder structure in InDestLevel if it doesn't exist (only used in PIE)
				if (FGuid ActorFolderGuid = Actor->GetFolderGuid(); InDestLevel && ActorFolderGuid.IsValid())
				{
					// Make sure Dest level is properly setup
					if (!InDestLevel->IsUsingActorFolders())
					{
						FLevelActorFoldersHelper::SetUseActorFolders(InDestLevel, true);
					}
					InDestLevel->bFixupActorFoldersAtLoad = false;

					// Make sure Source level actor folder fixup was called
					if (!ContainerWorld->PersistentLevel->LoadedExternalActorFolders.IsEmpty())
					{
						ContainerWorld->PersistentLevel->bFixupActorFoldersAtLoad = false;
						ContainerWorld->PersistentLevel->FixupActorFolders();
					}
					
					FWorldPartitionLevelHelper::DuplicateActorFolderToRuntimeCell(InDestLevel, ContainerWorld->PersistentLevel, ActorFolderGuid);
				}

				if (!PackageObjectMapping->ContainerID.IsMainContainer())
				{					
					// Add Cache handle on world so it gets unloaded properly
					InPackageReferencer.AddReference(ContainerWorld->GetPackage());
										
					FString SourceWorldPath, RemappedWorldPath;
					ContainerWorld->GetSoftObjectPathMapping(SourceWorldPath, RemappedWorldPath);

					// Rename through UObject to avoid changing Actor's external packaging and folder properties
					Actor->UObject::Rename(*FString::Printf(TEXT("%s_%s"), *Actor->GetName(), *PackageObjectMapping->ContainerID.ToString()), InDestLevel, REN_NonTransactional | REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors);
					
					FLevelUtils::FApplyLevelTransformParams TransformParams(nullptr, PackageObjectMapping->ContainerTransform);
					TransformParams.Actor = Actor;
					TransformParams.bDoPostEditMove = false;
					FLevelUtils::ApplyLevelTransform(TransformParams);
						
					// Path to use when searching for this actor in MoveExternalActorsToLevel
					PackageObjectMapping->LoadedPath = *Actor->GetPathName();

					// Fixup any FSoftObjectPath from this Actor (and its SubObjects) in this container to another object in the same container with a ContainerID suffix that can be remapped to
					// to a Cell in the StreamingPolicy (this relies on the fact that the _DUP package doesn't get fixed up)
					FSoftObjectPathFixupArchive FixupArchive([&](FSoftObjectPath& Value)
					{
						if (!Value.IsNull() && Value.GetAssetPathString().Equals(SourceWorldPath, ESearchCase::IgnoreCase))
						{
							Value.SetSubPathString(AddActorContainerIDToSubPathString(PackageObjectMapping->ContainerID, Value.GetSubPathString()));
						}
					});
					FixupArchive.Fixup(Actor);
				}

				if (InDestLevel)
				{
					check(Actor->IsPackageExternal());
					InDestLevel->Actors.Add(Actor);
					checkf(Actor->GetLevel() == InDestLevel, TEXT("Levels mismatch, got : %s, expected: %s\nActor: %s\nActorFullName: %s\nActorPackage: %s"), *InDestLevel->GetFullName(), *Actor->GetLevel()->GetFullName(), *Actor->GetActorNameOrLabel(), *Actor->GetFullName(), *Actor->GetPackage()->GetFullName());

					// Handle child actors
					Actor->ForEachComponent<UChildActorComponent>(true, [InDestLevel](UChildActorComponent* ChildActorComponent)
					{
						if (AActor* ChildActor = ChildActorComponent->GetChildActor())
						{
							InDestLevel->Actors.Add(ChildActor);
							check(ChildActor->GetLevel() == InDestLevel);
						}
					});
				}

				UE_LOG(LogEngine, Verbose, TEXT(" ==> Loaded %s (remaining: %d)"), *Actor->GetFullName(), LoadProgress->NumPendingLoadRequests);
			}
			else
			{
				UE_LOG(LogEngine, Warning, TEXT("Failed to load %s"), *LoadedPackageName.ToString());
				//@todo_ow: cumulate and process when NumPendingActorRequests == 0
				LoadProgress->NumFailedLoadedRequests++;
			}

			if (!LoadProgress->NumPendingLoadRequests)
			{
				InCompletionCallback(!LoadProgress->NumFailedLoadedRequests);
			}
		});

		FName PackageToLoad(*FPackageName::ObjectPathToPackageName(PackageObjectMapping->Package.ToString()));
		const FLinkerInstancingContext& ContainerInstancingContext = LinkerInstancingContexts.FindChecked(PackageObjectMapping->ContainerID);
		FName PackageName = ContainerInstancingContext.Remap(PackageToLoad);

		if (bInLoadAsync)
		{
			FPackagePath PackagePath = FPackagePath::FromPackageNameChecked(PackageToLoad);

			check(DestPackage);
			const EPackageFlags PackageFlags = DestPackage->HasAnyPackageFlags(PKG_PlayInEditor) ? PKG_PlayInEditor : PKG_None;
			::LoadPackageAsync(PackagePath, PackageName, CompletionCallback, PackageFlags, DestPackage->GetPIEInstanceID(), 0, &ContainerInstancingContext);
		}
		else
		{
			UPackage* InstancingPackage = nullptr;
			if (PackageName != PackageToLoad)
			{
				InstancingPackage = CreatePackage(*PackageName.ToString());
			}

			UPackage* Package = LoadPackage(InstancingPackage, *PackageToLoad.ToString(), LOAD_None, nullptr, &ContainerInstancingContext);
			CompletionCallback.Execute(PackageToLoad, Package, Package ? EAsyncLoadingResult::Succeeded : EAsyncLoadingResult::Failed);
		}
	}

	return (LoadProgress->NumPendingLoadRequests == 0);
}

#endif
