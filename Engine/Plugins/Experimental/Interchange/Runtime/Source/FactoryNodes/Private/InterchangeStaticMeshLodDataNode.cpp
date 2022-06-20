// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeStaticMeshLodDataNode.h"

namespace UE
{
	namespace Interchange
	{
		const FString& FStaticMeshNodeLodDataStaticData::GetMeshUidsBaseKey()
		{
			static FString MeshUids_BaseKey = TEXT("__MeshUids__Key");
			return MeshUids_BaseKey;
		}

		const FString& FStaticMeshNodeLodDataStaticData::GetBoxCollisionMeshUidsBaseKey()
		{
			static FString CollisionMeshUids_BaseKey = TEXT("__BoxCollisionMeshUids__Key");
			return CollisionMeshUids_BaseKey;
		}

		const FString& FStaticMeshNodeLodDataStaticData::GetCapsuleCollisionMeshUidsBaseKey()
		{
			static FString CollisionMeshUids_BaseKey = TEXT("__CapsuleCollisionMeshUids__Key");
			return CollisionMeshUids_BaseKey;
		}

		const FString& FStaticMeshNodeLodDataStaticData::GetSphereCollisionMeshUidsBaseKey()
		{
			static FString CollisionMeshUids_BaseKey = TEXT("__SphereCollisionMeshUids__Key");
			return CollisionMeshUids_BaseKey;
		}

		const FString& FStaticMeshNodeLodDataStaticData::GetConvexCollisionMeshUidsBaseKey()
		{
			static FString CollisionMeshUids_BaseKey = TEXT("__ConvexCollisionMeshUids__Key");
			return CollisionMeshUids_BaseKey;
		}

	} // namespace Interchange
} // namespace UE


UInterchangeStaticMeshLodDataNode::UInterchangeStaticMeshLodDataNode()
{
	MeshUids.Initialize(Attributes, UE::Interchange::FStaticMeshNodeLodDataStaticData::GetMeshUidsBaseKey());
	BoxCollisionMeshUids.Initialize(Attributes, UE::Interchange::FStaticMeshNodeLodDataStaticData::GetBoxCollisionMeshUidsBaseKey());
	CapsuleCollisionMeshUids.Initialize(Attributes, UE::Interchange::FStaticMeshNodeLodDataStaticData::GetCapsuleCollisionMeshUidsBaseKey());
	SphereCollisionMeshUids.Initialize(Attributes, UE::Interchange::FStaticMeshNodeLodDataStaticData::GetSphereCollisionMeshUidsBaseKey());
	ConvexCollisionMeshUids.Initialize(Attributes, UE::Interchange::FStaticMeshNodeLodDataStaticData::GetConvexCollisionMeshUidsBaseKey());
}

/**
 * Return the node type name of the class, we use this when reporting error
 */
FString UInterchangeStaticMeshLodDataNode::GetTypeName() const
{
	const FString TypeName = TEXT("StaticMeshLodDataNode");
	return TypeName;
}

FString UInterchangeStaticMeshLodDataNode::GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	FString KeyDisplayName = NodeAttributeKey.Key;
	if (NodeAttributeKey.Key.Equals(UE::Interchange::FStaticMeshNodeLodDataStaticData::GetMeshUidsBaseKey()))
	{
		KeyDisplayName = TEXT("Mesh count");
		return KeyDisplayName;
	}
	else if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FStaticMeshNodeLodDataStaticData::GetMeshUidsBaseKey()))
	{
		KeyDisplayName = TEXT("Mesh index ");
		const FString IndexKey = UE::Interchange::TArrayAttributeHelper<FString>::IndexKey();
		int32 IndexPosition = NodeAttributeKey.Key.Find(IndexKey) + IndexKey.Len();
		if (IndexPosition < NodeAttributeKey.Key.Len())
		{
			KeyDisplayName += NodeAttributeKey.Key.RightChop(IndexPosition);
		}
		return KeyDisplayName;
	}

	return Super::GetKeyDisplayName(NodeAttributeKey);
}

FString UInterchangeStaticMeshLodDataNode::GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
{
	if (NodeAttributeKey.Key.StartsWith(UE::Interchange::FStaticMeshNodeLodDataStaticData::GetMeshUidsBaseKey()))
	{
		return FString(TEXT("Meshes"));
	}
	return Super::GetAttributeCategory(NodeAttributeKey);
}

int32 UInterchangeStaticMeshLodDataNode::GetMeshUidsCount() const
{
	return MeshUids.GetCount();
}

void UInterchangeStaticMeshLodDataNode::GetMeshUids(TArray<FString>& OutMeshNames) const
{
	MeshUids.GetItems(OutMeshNames);
}

bool UInterchangeStaticMeshLodDataNode::AddMeshUid(const FString& MeshName)
{
	return MeshUids.AddItem(MeshName);
}

bool UInterchangeStaticMeshLodDataNode::RemoveMeshUid(const FString& MeshName)
{
	return MeshUids.RemoveItem(MeshName);
}

bool UInterchangeStaticMeshLodDataNode::RemoveAllMeshes()
{
	return MeshUids.RemoveAllItems();
}

int32 UInterchangeStaticMeshLodDataNode::GetBoxCollisionMeshUidsCount() const
{
	return BoxCollisionMeshUids.GetCount();
}

void UInterchangeStaticMeshLodDataNode::GetBoxCollisionMeshUids(TArray<FString>& OutMeshNames) const
{
	BoxCollisionMeshUids.GetItems(OutMeshNames);
}

bool UInterchangeStaticMeshLodDataNode::AddBoxCollisionMeshUid(const FString& MeshName)
{
	return BoxCollisionMeshUids.AddItem(MeshName);
}

bool UInterchangeStaticMeshLodDataNode::RemoveBoxCollisionMeshUid(const FString& MeshName)
{
	return BoxCollisionMeshUids.RemoveItem(MeshName);
}

bool UInterchangeStaticMeshLodDataNode::RemoveAllBoxCollisionMeshes()
{
	return BoxCollisionMeshUids.RemoveAllItems();
}

int32 UInterchangeStaticMeshLodDataNode::GetCapsuleCollisionMeshUidsCount() const
{
	return CapsuleCollisionMeshUids.GetCount();
}

void UInterchangeStaticMeshLodDataNode::GetCapsuleCollisionMeshUids(TArray<FString>& OutMeshNames) const
{
	CapsuleCollisionMeshUids.GetItems(OutMeshNames);
}

bool UInterchangeStaticMeshLodDataNode::AddCapsuleCollisionMeshUid(const FString& MeshName)
{
	return CapsuleCollisionMeshUids.AddItem(MeshName);
}

bool UInterchangeStaticMeshLodDataNode::RemoveCapsuleCollisionMeshUid(const FString& MeshName)
{
	return CapsuleCollisionMeshUids.RemoveItem(MeshName);
}

bool UInterchangeStaticMeshLodDataNode::RemoveAllCapsuleCollisionMeshes()
{
	return CapsuleCollisionMeshUids.RemoveAllItems();
}

int32 UInterchangeStaticMeshLodDataNode::GetSphereCollisionMeshUidsCount() const
{
	return SphereCollisionMeshUids.GetCount();
}

void UInterchangeStaticMeshLodDataNode::GetSphereCollisionMeshUids(TArray<FString>& OutMeshNames) const
{
	SphereCollisionMeshUids.GetItems(OutMeshNames);
}

bool UInterchangeStaticMeshLodDataNode::AddSphereCollisionMeshUid(const FString& MeshName)
{
	return SphereCollisionMeshUids.AddItem(MeshName);
}

bool UInterchangeStaticMeshLodDataNode::RemoveSphereCollisionMeshUid(const FString& MeshName)
{
	return SphereCollisionMeshUids.RemoveItem(MeshName);
}

bool UInterchangeStaticMeshLodDataNode::RemoveAllSphereCollisionMeshes()
{
	return SphereCollisionMeshUids.RemoveAllItems();
}

int32 UInterchangeStaticMeshLodDataNode::GetConvexCollisionMeshUidsCount() const
{
	return ConvexCollisionMeshUids.GetCount();
}

void UInterchangeStaticMeshLodDataNode::GetConvexCollisionMeshUids(TArray<FString>& OutMeshNames) const
{
	ConvexCollisionMeshUids.GetItems(OutMeshNames);
}

bool UInterchangeStaticMeshLodDataNode::AddConvexCollisionMeshUid(const FString& MeshName)
{
	return ConvexCollisionMeshUids.AddItem(MeshName);
}

bool UInterchangeStaticMeshLodDataNode::RemoveConvexCollisionMeshUid(const FString& MeshName)
{
	return ConvexCollisionMeshUids.RemoveItem(MeshName);
}

bool UInterchangeStaticMeshLodDataNode::RemoveAllConvexCollisionMeshes()
{
	return ConvexCollisionMeshUids.RemoveAllItems();
}

bool UInterchangeStaticMeshLodDataNode::GetOneConvexHullPerUCX(bool& AttributeValue) const
{
	IMPLEMENT_NODE_ATTRIBUTE_GETTER(OneConvexHullPerUCX, bool);
}

bool UInterchangeStaticMeshLodDataNode::SetOneConvexHullPerUCX(bool AttributeValue)
{
	IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(OneConvexHullPerUCX, bool)
}

bool UInterchangeStaticMeshLodDataNode::IsEditorOnlyDataDefined()
{
#if WITH_EDITORONLY_DATA
	return true;
#else
	return false;
#endif
}
