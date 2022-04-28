// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelInstance/LevelInstanceEditorLevelStreaming.h"

#if WITH_EDITOR
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "LevelInstance/LevelInstanceEditorPivotActor.h"
#include "EditorLevelUtils.h"
#include "Editor.h"
#include "Engine/LevelBounds.h"
#include "GameFramework/WorldSettings.h"
#endif

#if WITH_EDITOR
static FLevelInstanceID EditLevelInstanceID;
#endif

ULevelStreamingLevelInstanceEditor::ULevelStreamingLevelInstanceEditor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, LevelInstanceID(EditLevelInstanceID)
#endif
{
#if WITH_EDITOR
	SetShouldBeVisibleInEditor(true);

	if (!IsTemplate() && !GetWorld()->IsGameWorld())
	{
		GEngine->OnLevelActorAdded().AddUObject(this, &ULevelStreamingLevelInstanceEditor::OnLevelActorAdded);
	}
#endif
}

#if WITH_EDITOR
TOptional<FFolder::FRootObject> ULevelStreamingLevelInstanceEditor::GetFolderRootObject() const
{
	if (ILevelInstanceInterface* LevelInstance = GetLevelInstance())
	{
		if (AActor* Actor = CastChecked<AActor>(LevelInstance))
		{
			return FFolder::FRootObject(Actor);
		}
	}

	return TOptional<FFolder::FRootObject>();
}

ILevelInstanceInterface* ULevelStreamingLevelInstanceEditor::GetLevelInstance() const
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
	{
		return LevelInstanceSubsystem->GetLevelInstance(LevelInstanceID);
	}

	return nullptr;
}

ULevelStreamingLevelInstanceEditor* ULevelStreamingLevelInstanceEditor::Load(ILevelInstanceInterface* LevelInstance)
{
	AActor* LevelInstanceActor = CastChecked<AActor>(LevelInstance);
	UWorld* CurrentWorld = LevelInstanceActor->GetWorld();

	TGuardValue<FLevelInstanceID> GuardEditLevelInstanceID(EditLevelInstanceID, LevelInstance->GetLevelInstanceID());
	if (ULevelStreamingLevelInstanceEditor* LevelStreaming = Cast<ULevelStreamingLevelInstanceEditor>(EditorLevelUtils::AddLevelToWorld(CurrentWorld, *LevelInstance->GetWorldAssetPackage(), ULevelStreamingLevelInstanceEditor::StaticClass(), LevelInstanceActor->GetTransform())))
	{
		check(LevelStreaming);
		check(LevelStreaming->LevelInstanceID == LevelInstance->GetLevelInstanceID());

		GEngine->BlockTillLevelStreamingCompleted(LevelInstanceActor->GetWorld());

		// Create special actor that will handle changing the pivot of this level
		ALevelInstancePivot::Create(LevelInstance, LevelStreaming);

		return LevelStreaming;
	}

	return nullptr;
}

void ULevelStreamingLevelInstanceEditor::Unload(ULevelStreamingLevelInstanceEditor* LevelStreaming)
{
	if (ULevelInstanceSubsystem* LevelInstanceSubsystem = LevelStreaming->GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
	{
		LevelInstanceSubsystem->RemoveLevelsFromWorld({ LevelStreaming->GetLoadedLevel() });
	}
}

void ULevelStreamingLevelInstanceEditor::OnLevelActorAdded(AActor* InActor)
{
	if (InActor && InActor->GetLevel() == LoadedLevel)
	{
		InActor->PushLevelInstanceEditingStateToProxies(true);
	}
}

void ULevelStreamingLevelInstanceEditor::OnLevelLoadedChanged(ULevel* InLevel)
{
	Super::OnLevelLoadedChanged(InLevel);

	if (ULevel* NewLoadedLevel = GetLoadedLevel())
	{
		check(InLevel == NewLoadedLevel);

		// Avoid prompts for Level Instance editing
		NewLoadedLevel->bPromptWhenAddingToLevelBeforeCheckout = false;
		NewLoadedLevel->bPromptWhenAddingToLevelOutsideBounds = false;

		check(!NewLoadedLevel->bAlreadyMovedActors);
		if (AWorldSettings* WorldSettings = NewLoadedLevel->GetWorldSettings())
		{
			LevelTransform = FTransform(WorldSettings->LevelInstancePivotOffset) * LevelTransform;
		}

		if (ULevelInstanceSubsystem* LevelInstanceSubsystem = GetWorld()->GetSubsystem<ULevelInstanceSubsystem>())
		{
			LevelInstanceSubsystem->RegisterLoadedLevelStreamingLevelInstanceEditor(this);
		}
	}
}

FBox ULevelStreamingLevelInstanceEditor::GetBounds() const
{
	check(GetLoadedLevel());
	return ALevelBounds::CalculateLevelBounds(GetLoadedLevel());
}

#endif