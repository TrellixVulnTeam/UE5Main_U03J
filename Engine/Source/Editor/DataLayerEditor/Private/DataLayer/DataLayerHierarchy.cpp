// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerHierarchy.h"
#include "DataLayerMode.h"
#include "DataLayerActorTreeItem.h"
#include "DataLayersActorDescTreeItem.h"
#include "DataLayerTreeItem.h"
#include "DataLayerMode.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "WorldPartition/IWorldPartitionEditorModule.h"

TUniquePtr<FDataLayerHierarchy> FDataLayerHierarchy::Create(FDataLayerMode* Mode, const TWeakObjectPtr<UWorld>& World)
{
	return TUniquePtr<FDataLayerHierarchy>(new FDataLayerHierarchy(Mode, World));
}

FDataLayerHierarchy::FDataLayerHierarchy(FDataLayerMode* Mode, const TWeakObjectPtr<UWorld>& World)
	: ISceneOutlinerHierarchy(Mode)
	, RepresentingWorld(World)
	, bShowEditorDataLayers(true)
	, bShowRuntimeDataLayers(true)
	, bShowDataLayerActors(true)
	, bShowUnloadedActors(true)
	, bShowOnlySelectedActors(false)
	, bHighlightSelectedDataLayers(false)
{
	if (GEngine)
	{
		GEngine->OnLevelActorAdded().AddRaw(this, &FDataLayerHierarchy::OnLevelActorAdded);
		GEngine->OnLevelActorDeleted().AddRaw(this, &FDataLayerHierarchy::OnLevelActorDeleted);
		GEngine->OnLevelActorListChanged().AddRaw(this, &FDataLayerHierarchy::OnLevelActorListChanged);
	}

	IWorldPartitionEditorModule& WorldPartitionEditorModule = FModuleManager::LoadModuleChecked<IWorldPartitionEditorModule>("WorldPartitionEditor");
	WorldPartitionEditorModule.OnWorldPartitionCreated().AddRaw(this, &FDataLayerHierarchy::OnWorldPartitionCreated);

	if (World.IsValid())
	{
		if (World->PersistentLevel)
		{
			World->PersistentLevel->OnLoadedActorAddedToLevelEvent.AddRaw(this, &FDataLayerHierarchy::OnLoadedActorAdded);
			World->PersistentLevel->OnLoadedActorRemovedFromLevelEvent.AddRaw(this, &FDataLayerHierarchy::OnLoadedActorRemoved);
		}
		
		if (UWorldPartition* WorldPartition = World->GetWorldPartition())
		{
			WorldPartition->OnActorDescAddedEvent.AddRaw(this, &FDataLayerHierarchy::OnActorDescAdded);
			WorldPartition->OnActorDescRemovedEvent.AddRaw(this, &FDataLayerHierarchy::OnActorDescRemoved);
		}
	}

	UDataLayerEditorSubsystem::Get()->OnDataLayerChanged().AddRaw(this, &FDataLayerHierarchy::OnDataLayerChanged);
	UDataLayerEditorSubsystem::Get()->OnActorDataLayersChanged().AddRaw(this, &FDataLayerHierarchy::OnActorDataLayersChanged);
	FWorldDelegates::LevelAddedToWorld.AddRaw(this, &FDataLayerHierarchy::OnLevelAdded);
	FWorldDelegates::LevelRemovedFromWorld.AddRaw(this, &FDataLayerHierarchy::OnLevelRemoved);
}

FDataLayerHierarchy::~FDataLayerHierarchy()
{
	if (GEngine)
	{
		GEngine->OnLevelActorAdded().RemoveAll(this);
		GEngine->OnLevelActorDeleted().RemoveAll(this);
		GEngine->OnLevelActorListChanged().RemoveAll(this);
	}

	IWorldPartitionEditorModule& WorldPartitionEditorModule = FModuleManager::LoadModuleChecked<IWorldPartitionEditorModule>("WorldPartitionEditor");
	WorldPartitionEditorModule.OnWorldPartitionCreated().RemoveAll(this);

	if (RepresentingWorld.IsValid())
	{
		if (RepresentingWorld->PersistentLevel)
		{
			RepresentingWorld->PersistentLevel->OnLoadedActorAddedToLevelEvent.RemoveAll(this);
			RepresentingWorld->PersistentLevel->OnLoadedActorRemovedFromLevelEvent.RemoveAll(this);
		}
		
		if (UWorldPartition* WorldPartition = RepresentingWorld->GetWorldPartition())
		{
			WorldPartition->OnActorDescAddedEvent.RemoveAll(this);
			WorldPartition->OnActorDescRemovedEvent.RemoveAll(this);
		}
	}

	UDataLayerEditorSubsystem::Get()->OnDataLayerChanged().RemoveAll(this);
	UDataLayerEditorSubsystem::Get()->OnActorDataLayersChanged().RemoveAll(this);
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
	FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
}


bool FDataLayerHierarchy::IsDataLayerPartOfSelection(const UDataLayerInstance* DataLayer) const
{
	if (!bShowOnlySelectedActors)
	{
		return true;
	}

	if (UDataLayerEditorSubsystem::Get()->DoesDataLayerContainSelectedActors(DataLayer))
	{
		return true;
	}

	bool bFoundSelected = false;
	DataLayer->ForEachChild([this, &bFoundSelected](const UDataLayerInstance* Child)
	{
		bFoundSelected = IsDataLayerPartOfSelection(Child);
		return !bFoundSelected; // Continue iterating if not found
	});
	return bFoundSelected;
};

FSceneOutlinerTreeItemPtr FDataLayerHierarchy::CreateDataLayerTreeItem(UDataLayerInstance* InDataLayer, bool bInForce) const
{
	FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FDataLayerTreeItem>(InDataLayer, bInForce);
	if (FDataLayerTreeItem* DataLayerTreeItem = Item ? Item->CastTo<FDataLayerTreeItem>() : nullptr)
	{
		DataLayerTreeItem->SetIsHighlightedIfSelected(bHighlightSelectedDataLayers);
	}
	return Item;
}

void FDataLayerHierarchy::CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
{
	const AWorldDataLayers* WorldDataLayers = RepresentingWorld.Get()->GetWorldDataLayers();
	if (!WorldDataLayers)
	{
		return;
	}

	auto IsDataLayerShown = [this](const UDataLayerInstance* DataLayer)
	{
		const bool bIsRuntimeDataLayer = DataLayer->IsRuntime();
		return ((bIsRuntimeDataLayer && bShowRuntimeDataLayers) || (!bIsRuntimeDataLayer && bShowEditorDataLayers)) && IsDataLayerPartOfSelection(DataLayer);
	};

	WorldDataLayers->ForEachDataLayer([this, &OutItems, IsDataLayerShown](UDataLayerInstance* DataLayer)
	{
		if (IsDataLayerShown(DataLayer))
		{
			if (FSceneOutlinerTreeItemPtr DataLayerItem = CreateDataLayerTreeItem(DataLayer))
			{
				OutItems.Add(DataLayerItem);
			}
		}
		return true;
	});

	if (bShowDataLayerActors)
	{
		for (AActor* Actor : FActorRange(RepresentingWorld.Get()))
		{
			if (Actor->HasDataLayers())
			{
				for (const UDataLayerInstance* DataLayerInstance : Actor->GetDataLayerInstances())
				{
					if (IsDataLayerShown(DataLayerInstance))
					{
						if (FSceneOutlinerTreeItemPtr DataLayerActorItem = Mode->CreateItemFor<FDataLayerActorTreeItem>(FDataLayerActorTreeItemData(Actor, const_cast<UDataLayerInstance*>(DataLayerInstance))))
						{
							OutItems.Add(DataLayerActorItem);
						}
					}
				}
			}
		}

		if (bShowUnloadedActors)
		{
			if (UWorldPartition* const WorldPartition = RepresentingWorld.Get()->GetWorldPartition())
			{
				FWorldPartitionHelpers::ForEachActorDesc(WorldPartition, [this, WorldPartition, WorldDataLayers, IsDataLayerShown, &OutItems](const FWorldPartitionActorDesc* ActorDesc)
				{
					if (ActorDesc != nullptr && !ActorDesc->IsLoaded())
					{
						for (const FName& DataLayerInstanceName : ActorDesc->GetDataLayerInstanceNames())
						{
							if (const UDataLayerInstance* const DataLayerInstance = WorldDataLayers->GetDataLayerInstance(DataLayerInstanceName))
							{
								if (IsDataLayerShown(DataLayerInstance))
								{
									if (const FSceneOutlinerTreeItemPtr ActorDescItem = Mode->CreateItemFor<FDataLayerActorDescTreeItem>(FDataLayerActorDescTreeItemData(ActorDesc->GetGuid(), WorldPartition, const_cast<UDataLayerInstance*>(DataLayerInstance))))
									{
										OutItems.Add(ActorDescItem);
									}
								}
							}
						}
					}
					return true;
				});
			}
		}
	}
}

FSceneOutlinerTreeItemPtr FDataLayerHierarchy::FindOrCreateParentItem(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items, bool bCreate)
{
	if (const FDataLayerTreeItem* DataLayerTreeItem = Item.CastTo<FDataLayerTreeItem>())
	{
		if (UDataLayerInstance* DataLayer = DataLayerTreeItem->GetDataLayer())
		{
			if (UDataLayerInstance* ParentDataLayer = DataLayer->GetParent())
			{
				if (const FSceneOutlinerTreeItemPtr* ParentItem = Items.Find(ParentDataLayer))
				{
					return *ParentItem;
				}
				else if (bCreate)
				{
					return CreateDataLayerTreeItem(ParentDataLayer, true);
				}
			}
		}
	}
	else if (const FDataLayerActorTreeItem* DataLayerActorTreeItem = Item.CastTo<FDataLayerActorTreeItem>())
	{
		if (UDataLayerInstance* DataLayer = DataLayerActorTreeItem->GetDataLayer())
		{
			if (const FSceneOutlinerTreeItemPtr* ParentItem = Items.Find(DataLayer))
			{
				return *ParentItem;
			}
			else if (bCreate)
			{
				return CreateDataLayerTreeItem(DataLayer, true);
			}
		}
	}
	else if (const FDataLayerActorDescTreeItem* DataLayerActorDescTreeItem = Item.CastTo<FDataLayerActorDescTreeItem>())
	{
		if (UDataLayerInstance* DataLayer = DataLayerActorDescTreeItem->GetDataLayer())
		{
			if (const FSceneOutlinerTreeItemPtr* ParentItem = Items.Find(DataLayer))
			{
				return *ParentItem;
			}
			else if (bCreate)
			{
				return CreateDataLayerTreeItem(DataLayer, true);
			}
		}
	}
	return nullptr;
}

void FDataLayerHierarchy::OnWorldPartitionCreated(UWorld* InWorld)
{
	if (RepresentingWorld.Get() == InWorld)
	{
		FullRefreshEvent();
	}
}

void FDataLayerHierarchy::OnLevelActorsAdded(const TArray<AActor*>& InActors)
{
	if (!bShowDataLayerActors)
	{
		return;
	}

	if (UWorld* CurrentWorld = RepresentingWorld.Get())
	{
		FSceneOutlinerHierarchyChangedData EventData;
		EventData.Type = FSceneOutlinerHierarchyChangedData::Added;

		for (AActor* Actor : InActors)
		{
			if (Actor != nullptr && Actor->HasDataLayers() && Actor->GetWorld() == CurrentWorld)
			{
				TArray<const UDataLayerInstance*> DataLayerInstances = Actor->GetDataLayerInstances();
				EventData.Items.Reserve(DataLayerInstances.Num());
				for (const UDataLayerInstance* DataLayerInstance : DataLayerInstances)
				{
					EventData.Items.Add(Mode->CreateItemFor<FDataLayerActorTreeItem>(FDataLayerActorTreeItemData(Actor, const_cast<UDataLayerInstance*>(DataLayerInstance))));
				}
			}
		}

		if (!EventData.Items.IsEmpty())
		{
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}
}

void FDataLayerHierarchy::OnLevelActorsRemoved(const TArray<AActor*>& InActors)
{
	if(UWorld* CurrentWorld = RepresentingWorld.Get())
	{
		FSceneOutlinerHierarchyChangedData EventData;
		EventData.Type = FSceneOutlinerHierarchyChangedData::Removed;

		if (AWorldDataLayers* WorldDataLayers = CurrentWorld->GetWorldDataLayers())
		{
			for (AActor* Actor : InActors)
			{
				if (Actor != nullptr && Actor->HasDataLayers())
				{
					// It is possible here that Actor doesn't have world anymore
					const TArray<const UDataLayerInstance*> DataLayerInstances = Actor->GetDataLayerInstances(WorldDataLayers);
					EventData.ItemIDs.Reserve(DataLayerInstances.Num());
					for (const UDataLayerInstance* DataLayerInstance : DataLayerInstances)
					{
						EventData.ItemIDs.Add(FDataLayerActorTreeItem::ComputeTreeItemID(Actor, DataLayerInstance));
					}
				}
			}
		}

		if (!EventData.ItemIDs.IsEmpty())
		{
			HierarchyChangedEvent.Broadcast(EventData);
		}
	}
}

void FDataLayerHierarchy::OnLevelActorAdded(AActor* InActor)
{
	OnLevelActorsAdded({ InActor });
}

void FDataLayerHierarchy::OnActorDataLayersChanged(const TWeakObjectPtr<AActor>& InActor)
{
	AActor* Actor = InActor.Get();
	if (Actor && RepresentingWorld.Get() == Actor->GetWorld())
	{
		FullRefreshEvent();
	}
}

void FDataLayerHierarchy::OnDataLayerChanged(const EDataLayerAction Action, const TWeakObjectPtr<const UDataLayerInstance>& ChangedDataLayer, const FName& ChangedProperty)
{
	const UDataLayerInstance* DataLayer = ChangedDataLayer.Get();
	if ((DataLayer && (RepresentingWorld.Get() == DataLayer->GetWorld())) || (Action == EDataLayerAction::Delete) || (Action == EDataLayerAction::Reset))
	{
		FullRefreshEvent();
	}
}

void FDataLayerHierarchy::OnLevelActorDeleted(AActor* InActor)
{
	OnLevelActorsRemoved({ InActor });
}

void FDataLayerHierarchy::OnLevelActorListChanged()
{
	FullRefreshEvent();
}

void FDataLayerHierarchy::OnLevelAdded(ULevel* InLevel, UWorld* InWorld)
{
	if (InLevel != nullptr && RepresentingWorld.Get() == InWorld)
	{
		OnLevelActorsAdded(InLevel->Actors);
	}
}

void FDataLayerHierarchy::OnLevelRemoved(ULevel* InLevel, UWorld* InWorld)
{
	if (InLevel != nullptr && RepresentingWorld.Get() == InWorld)
	{
		OnLevelActorsRemoved(InLevel->Actors);
	}
}

void FDataLayerHierarchy::OnLoadedActorAdded(AActor& InActor)
{
	if (!bShowDataLayerActors)
	{
		return;
	}

	// Handle the actor being added to the level
	OnLevelActorAdded(&InActor);

	// Remove corresponding actor desc items
	if (RepresentingWorld.Get() == InActor.GetWorld() && InActor.HasDataLayers())
	{
		FSceneOutlinerHierarchyChangedData EventData;
		EventData.Type = FSceneOutlinerHierarchyChangedData::Removed;
		
		const TArray<const UDataLayerInstance*> DataLayerInstances = InActor.GetDataLayerInstances();
		EventData.ItemIDs.Reserve(DataLayerInstances.Num());
		for (const UDataLayerInstance* DataLayerInstance : DataLayerInstances)
		{
			EventData.ItemIDs.Add(FDataLayerActorDescTreeItem::ComputeTreeItemID(InActor.GetActorGuid(), DataLayerInstance));
		}
		HierarchyChangedEvent.Broadcast(EventData);
	}
}

void FDataLayerHierarchy::OnLoadedActorRemoved(AActor& InActor)
{
	// Handle the actor being removed from the level
	OnLevelActorDeleted(&InActor);

	// Add any corresponding actor desc items for this actor
	UWorldPartition* const WorldPartition = RepresentingWorld.Get()->GetWorldPartition();
	if (RepresentingWorld.Get() == InActor.GetWorld() && InActor.HasDataLayers() && WorldPartition != nullptr)
	{
		FSceneOutlinerHierarchyChangedData EventData;
		EventData.Type = FSceneOutlinerHierarchyChangedData::Added;

		const TArray<const UDataLayerInstance*> DataLayerInstances = InActor.GetDataLayerInstances();
		EventData.Items.Reserve(DataLayerInstances.Num());
		for (const UDataLayerInstance* DataLayerInstance : DataLayerInstances)
		{
			EventData.Items.Add(Mode->CreateItemFor<FDataLayerActorDescTreeItem>(FDataLayerActorDescTreeItemData(InActor.GetActorGuid(), WorldPartition, const_cast<UDataLayerInstance*>(DataLayerInstance))));
		}
		HierarchyChangedEvent.Broadcast(EventData);
    }
}

void FDataLayerHierarchy::OnActorDescAdded(FWorldPartitionActorDesc* InActorDesc)
{
	if (!bShowUnloadedActors || !InActorDesc || InActorDesc->IsLoaded(true))
	{
		return;
	}

	UWorldPartition* const WorldPartition = RepresentingWorld.Get()->GetWorldPartition();
	const AWorldDataLayers* const WorldDataLayers = RepresentingWorld.Get()->GetWorldDataLayers();
	const TArray<FName>& DataLayerInstanceNames = InActorDesc->GetDataLayerInstanceNames();

	if (WorldDataLayers != nullptr && RepresentingWorld->GetWorldPartition() == WorldPartition && DataLayerInstanceNames.Num() > 0)
	{
		FSceneOutlinerHierarchyChangedData EventData;
		EventData.Type = FSceneOutlinerHierarchyChangedData::Added;

		EventData.Items.Reserve(DataLayerInstanceNames.Num());
		for (const FName& DataLayerInstanceName : DataLayerInstanceNames)
		{
			const UDataLayerInstance* const DataLayerInstance = WorldDataLayers->GetDataLayerInstance(DataLayerInstanceName);
			EventData.Items.Add(Mode->CreateItemFor<FDataLayerActorDescTreeItem>(FDataLayerActorDescTreeItemData(InActorDesc->GetGuid(), WorldPartition, const_cast<UDataLayerInstance*>(DataLayerInstance))));
		}
		HierarchyChangedEvent.Broadcast(EventData);
	}
}

void FDataLayerHierarchy::OnActorDescRemoved(FWorldPartitionActorDesc* InActorDesc)
{
	if (!bShowUnloadedActors || (InActorDesc == nullptr))
	{
		return;
	}

	const AWorldDataLayers* const WorldDataLayers = RepresentingWorld.Get()->GetWorldDataLayers();
	const TArray<FName>& DataLayerInstanceNames = InActorDesc->GetDataLayerInstanceNames();
	
	if (WorldDataLayers != nullptr && DataLayerInstanceNames.Num() > 0)
	{
		FSceneOutlinerHierarchyChangedData EventData;
		EventData.Type = FSceneOutlinerHierarchyChangedData::Removed;
		EventData.ItemIDs.Reserve(DataLayerInstanceNames.Num());

		for (const FName& DataLayerInstanceName : DataLayerInstanceNames)
		{
			const UDataLayerInstance* const DataLayer = WorldDataLayers->GetDataLayerInstance(DataLayerInstanceName);
			EventData.ItemIDs.Add(FDataLayerActorDescTreeItem::ComputeTreeItemID(InActorDesc->GetGuid(), DataLayer));
		}
		HierarchyChangedEvent.Broadcast(EventData);
	}
}

void FDataLayerHierarchy::FullRefreshEvent()
{
	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::FullRefresh;
	HierarchyChangedEvent.Broadcast(EventData);
}
