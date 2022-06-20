// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMUnitNode.h"

#include "Animation/Rig.h"
#include "RigVMCore/RigVMStruct.h"
#include "RigVMModel/RigVMControllerActions.h"
#include "RigVMUserWorkflowRegistry.h"

void URigVMUnitNode::PostLoad()
{
	Super::PostLoad();

	// if we have a script struct but no notation let's figure out the template
	if(GetScriptStruct() != nullptr && GetTemplate() == nullptr)
	{
		if(const FRigVMFunction* Function = FRigVMRegistry::Get().FindFunction(GetScriptStruct(), *GetMethodName().ToString()))
		{
			if(Function->TemplateIndex != INDEX_NONE)
			{
				const FRigVMTemplate& Template = FRigVMRegistry::Get().GetTemplates()[Function->TemplateIndex];
				TemplateNotation = Template.GetNotation();
				ResolvedFunctionName = Function->GetName();
			}	
		}
	}
}

FString URigVMUnitNode::GetNodeTitle() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->GetDisplayNameText().ToString();
	}
	return Super::GetNodeTitle();
}

FText URigVMUnitNode::GetToolTipText() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->GetToolTipText();
	}
	return URigVMNode::GetToolTipText();
}

bool URigVMUnitNode::IsDefinedAsConstant() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->HasMetaData(FRigVMStruct::ConstantMetaName);
	}
	return false;
}

bool URigVMUnitNode::IsDefinedAsVarying() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->HasMetaData(FRigVMStruct::VaryingMetaName);
	}
	return false;
}

FName URigVMUnitNode::GetEventName() const
{
	TSharedPtr<FStructOnScope> StructOnScope = ConstructStructInstance(true);
	if (StructOnScope.IsValid())
	{
		const FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope->GetStructMemory();
		return StructMemory->GetEventName();
	}
	return NAME_None;
}

FText URigVMUnitNode::GetToolTipTextForPin(const URigVMPin* InPin) const
{
	if(UScriptStruct* Struct = GetScriptStruct())
	{
		TArray<FString> Parts;
		URigVMPin::SplitPinPath(InPin->GetPinPath(), Parts);

		for (int32 PartIndex = 1; PartIndex < Parts.Num(); PartIndex++)
		{
			FProperty* Property = Struct->FindPropertyByName(*Parts[PartIndex]);
			if (!Property)
			{
				break;
			}

			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				if (PartIndex < Parts.Num() - 1)
				{
					Property = ArrayProperty->Inner;
					PartIndex++;
				}
			}

			if (PartIndex == Parts.Num() - 1)
			{
				return Property->GetToolTipText();
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				Struct = StructProperty->Struct;
			}
		}

	}
	return URigVMNode::GetToolTipTextForPin(InPin);
}

bool URigVMUnitNode::IsDeprecated() const
{
	return !GetDeprecatedMetadata().IsEmpty();
}

FString URigVMUnitNode::GetDeprecatedMetadata() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		FString DeprecatedMetadata;
		if(Struct->GetStringMetaDataHierarchical(FRigVMStruct::DeprecatedMetaName, &DeprecatedMetadata))
		{
			return DeprecatedMetadata;
		}
	}
	return FString();
}

TArray<FRigVMUserWorkflow> URigVMUnitNode::GetSupportedWorkflows(ERigVMUserWorkflowType InType, const UObject* InSubject) const
{
	TArray<FRigVMUserWorkflow> Workflows = Super::GetSupportedWorkflows(InType, InSubject);

	if(InSubject == nullptr)
	{
		InSubject = this;
	}

	if(UScriptStruct* Struct = GetScriptStruct())
	{
		check(Struct->IsChildOf(FRigVMStruct::StaticStruct()));

		const TSharedPtr<FStructOnScope> StructOnScope = ConstructStructInstance();
		const FRigVMStruct* StructMemory = (const FRigVMStruct*)StructOnScope->GetStructMemory();
		TArray<FRigVMUserWorkflow> StructWorkflows = StructMemory->GetWorkflows(InType, InSubject);
		StructWorkflows.Append(URigVMUserWorkflowRegistry::Get()->GetWorkflows(InType, Struct, InSubject));
		Swap(Workflows, StructWorkflows);
		Workflows.Append(StructWorkflows);
	}

	return Workflows;
}

bool URigVMUnitNode::IsAggregate() const
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	TArray<URigVMPin*> AggregateInputs = GetAggregateInputs();
	TArray<URigVMPin*> AggregateOutputs = GetAggregateOutputs();

	if ((AggregateInputs.Num() == 2 && AggregateOutputs.Num() == 1) ||
		(AggregateInputs.Num() == 1 && AggregateOutputs.Num() == 2))
	{
		TArray<URigVMPin*> AggregateAll = AggregateInputs;
		AggregateAll.Append(AggregateOutputs);
		for (int32 i = 1; i < 3; ++i)
		{
			if (AggregateAll[0]->GetCPPType() != AggregateAll[i]->GetCPPType() ||
				AggregateAll[0]->GetCPPTypeObject() != AggregateAll[i]->GetCPPTypeObject())
			{
				return false;
			}
		}
		
		return true;
	}
#endif
	
	return false;
}

URigVMPin* URigVMUnitNode::GetFirstAggregatePin() const
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	TArray<URigVMPin*> Inputs = GetAggregateInputs();
	TArray<URigVMPin*> Outputs = GetAggregateOutputs();
	if (Inputs.Num() == 2 && Outputs.Num() == 1)
	{
		return Inputs[0];
	}
	if (Inputs.Num() == 1 && Outputs.Num() == 2)
	{
		return Outputs[0];
	}
#endif
	return nullptr;
}

URigVMPin* URigVMUnitNode::GetSecondAggregatePin() const
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	TArray<URigVMPin*> Inputs = GetAggregateInputs();
	TArray<URigVMPin*> Outputs = GetAggregateOutputs();
	if (Inputs.Num() == 2 && Outputs.Num() == 1)
	{
		return Inputs[1];
	}
	if (Inputs.Num() == 1 && Outputs.Num() == 2)
	{
		return Outputs[1];
	}
#endif
	return nullptr;
}

URigVMPin* URigVMUnitNode::GetOppositeAggregatePin() const
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	TArray<URigVMPin*> Inputs = GetAggregateInputs();
	TArray<URigVMPin*> Outputs = GetAggregateOutputs();
	if (Inputs.Num() == 2 && Outputs.Num() == 1)
	{
		return Outputs[0];
	}
	if (Inputs.Num() == 1 && Outputs.Num() == 2)
	{
		return Inputs[0];
	}
#endif
	return nullptr;
}

bool URigVMUnitNode::IsInputAggregate() const
{
	return GetAggregateInputs().Num() == 2;
}

TArray<URigVMPin*> URigVMUnitNode::GetAggregateInputs() const
{
	TArray<URigVMPin*> AggregateInputs;
#if UE_RIGVM_AGGREGATE_NODES_ENABLED
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		for (URigVMPin* Pin : GetPins())
		{
			if (Pin->GetDirection() == ERigVMPinDirection::Input)
			{
				if (FProperty* Property = Struct->FindPropertyByName(Pin->GetFName()))
				{
					if (Property->HasMetaData(FRigVMStruct::AggregateMetaName))
					{
						AggregateInputs.Add(Pin);
					}
				}			
			}
		}
	}
#endif
	return AggregateInputs;
}

TArray<URigVMPin*> URigVMUnitNode::GetAggregateOutputs() const
{
	TArray<URigVMPin*> AggregateOutputs;
#if UE_RIGVM_AGGREGATE_NODES_ENABLED	
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		for (URigVMPin* Pin : GetPins())
		{
			if (Pin->GetDirection() == ERigVMPinDirection::Output)
			{
				if (FProperty* Property = Struct->FindPropertyByName(Pin->GetFName()))
				{
					if (Property->HasMetaData(FRigVMStruct::AggregateMetaName))
					{
						AggregateOutputs.Add(Pin);
					}
				}			
			}
		}
	}
#endif
	return AggregateOutputs;
}

FName URigVMUnitNode::GetNextAggregateName(const FName& InLastAggregatePinName) const
{
#if UE_RIGVM_AGGREGATE_NODES_ENABLED	
	if(UScriptStruct* Struct = GetScriptStruct())
	{
		check(Struct->IsChildOf(FRigVMStruct::StaticStruct()));

		const TSharedPtr<FStructOnScope> StructOnScope = ConstructStructInstance();
		const FRigVMStruct* StructMemory = (const FRigVMStruct*)StructOnScope->GetStructMemory();
		return StructMemory->GetNextAggregateName(InLastAggregatePinName);
	}
#endif
	return FName();
}

UScriptStruct* URigVMUnitNode::GetScriptStruct() const
{
	if(UScriptStruct* ResolvedStruct = Super::GetScriptStruct())
	{
		return ResolvedStruct;
	}
	return ScriptStruct_DEPRECATED;
}

bool URigVMUnitNode::IsLoopNode() const
{
	TSharedPtr<FStructOnScope> StructOnScope = ConstructStructInstance(true);
	if (StructOnScope.IsValid())
	{
		const FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope->GetStructMemory();
		return StructMemory->IsForLoop();
	}
	return false;
}

FName URigVMUnitNode::GetMethodName() const
{
	const FName ResolvedMethodName = Super::GetMethodName();
	if(!ResolvedMethodName.IsNone())
	{
		return ResolvedMethodName;
	}
	return MethodName_DEPRECATED;
}

FString URigVMUnitNode::GetStructDefaultValue() const
{
	TArray<FString> PinDefaultValues;
	for (URigVMPin* Pin : GetPins())
	{
		if (Pin->GetDirection() == ERigVMPinDirection::Hidden)
		{
			continue;
		}
		FString PinDefaultValue = Pin->GetDefaultValue();
		if (Pin->IsStringType())
		{
			PinDefaultValue = TEXT("\"") + PinDefaultValue + TEXT("\"");
		}
		else if (PinDefaultValue.IsEmpty() || PinDefaultValue == TEXT("()"))
		{
			continue;
		}
		PinDefaultValues.Add(FString::Printf(TEXT("%s=%s"), *Pin->GetName(), *PinDefaultValue));
	}
	if (PinDefaultValues.Num() == 0)
	{
		return TEXT("()");
	}
	return FString::Printf(TEXT("(%s)"), *FString::Join(PinDefaultValues, TEXT(",")));

}

TSharedPtr<FStructOnScope> URigVMUnitNode::ConstructStructInstance(bool bUseDefault) const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		TSharedPtr<FStructOnScope> StructOnScope = MakeShareable(new FStructOnScope(Struct));
		FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope->GetStructMemory();
		if (bUseDefault)
		{
			Struct->InitializeDefaultValue((uint8*)StructMemory);
		}
		else
		{
			FString StructDefaultValue = GetStructDefaultValue();
			Struct->ImportText(*StructDefaultValue, StructMemory, nullptr, PPF_IncludeTransient, GLog, Struct->GetName());
		}
		return StructOnScope;
	}
	return nullptr;
}

FRigVMStructUpgradeInfo URigVMUnitNode::GetUpgradeInfo() const
{
	if(UScriptStruct* Struct = GetScriptStruct())
	{
		check(Struct->IsChildOf(FRigVMStruct::StaticStruct()));

		const TSharedPtr<FStructOnScope> StructOnScope = ConstructStructInstance();
		const FRigVMStruct* StructMemory = (const FRigVMStruct*)StructOnScope->GetStructMemory();
		return StructMemory->GetUpgradeInfo();
	}
	return FRigVMStructUpgradeInfo();
}
