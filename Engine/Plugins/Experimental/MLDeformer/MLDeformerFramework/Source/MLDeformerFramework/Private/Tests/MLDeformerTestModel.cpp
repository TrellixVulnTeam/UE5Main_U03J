// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerTestModel.h"
#include "MLDeformerComponent.h"
#include "UObject/UObjectGlobals.h"

UMLDeformerModelInstance* UMLDeformerTestModel::CreateModelInstance(UMLDeformerComponent* Component)
{
	return NewObject<UTestModelInstance>(Component);
}

FString UMLDeformerTestModel::GetDisplayName() const 
{ 
	return FString("Test Model");
}

void UMLDeformerTestModel::UpdateNumTargetMeshVertices()
{
	NumTargetMeshVerts = NumBaseMeshVerts;
}
