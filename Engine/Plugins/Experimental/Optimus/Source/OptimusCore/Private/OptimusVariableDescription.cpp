// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusVariableDescription.h"

#include "OptimusDeformer.h"
#include "OptimusHelpers.h"


void UOptimusVariableDescription::ResetValueDataSize()
{
	if (DataType->CanCreateProperty())
	{
		// Create a temporary property from the type so that we can get the size of the
		// type for properly resizing the storage.
		TUniquePtr<FProperty> TempProperty(DataType->CreateProperty(nullptr, NAME_None));
		
		if (ValueData.Num() != TempProperty->GetSize())
		{
			ValueData.SetNumZeroed(TempProperty->GetSize());
		}
	}
}


UOptimusDeformer* UOptimusVariableDescription::GetOwningDeformer() const
{
	const UOptimusVariableContainer* Container = CastChecked<UOptimusVariableContainer>(GetOuter());
	return Container ? CastChecked<UOptimusDeformer>(Container->GetOuter()) : nullptr;
}


#if WITH_EDITOR
void UOptimusVariableDescription::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UOptimusVariableDescription, VariableName))
	{
		UOptimusDeformer* Deformer = GetOwningDeformer();
		if (ensure(Deformer))
		{
			VariableName = Optimus::GetUniqueNameForScope(GetOuter(), VariableName);
			Rename(*VariableName.ToString(), nullptr);
			Deformer->UpdateVariableNodesPinNames(this, VariableName);
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(FOptimusDataType, TypeName))
	{
		UOptimusDeformer* Deformer = GetOwningDeformer();
		if (ensure(Deformer))
		{
			// Set the variable type again, so that we can remove any links that are now
			// type-incompatible.
			Deformer->SetVariableDataType(this, DataType);
		}

		// Make sure the value data container is still large enough to hold the property value.
		ValueData.Reset();
		ResetValueDataSize();
	}
}


void UOptimusVariableDescription::PreEditUndo()
{
	UObject::PreEditUndo();

	VariableNameForUndo = VariableName;
}


void UOptimusVariableDescription::PostEditUndo()
{
	UObject::PostEditUndo();

	if (VariableNameForUndo != VariableName)
	{
		const UOptimusDeformer *Deformer = GetOwningDeformer();
		Deformer->Notify(EOptimusGlobalNotifyType::VariableRenamed, this);
	}
}
#endif
