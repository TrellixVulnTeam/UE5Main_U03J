// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataType.h"

#include "CoreMinimal.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "OptimusVariableDescription.generated.h"


class UOptimusDeformer;


USTRUCT()
struct FOptimusVariableMetaDataEntry
{
	GENERATED_BODY()

	FOptimusVariableMetaDataEntry() {}
	FOptimusVariableMetaDataEntry(FName InKey, FString&& InValue)
	    : Key(InKey), Value(MoveTemp(InValue))
	{}

	/** Name of metadata key */
	UPROPERTY(EditAnywhere, Category = VariableMetaDataEntry)
	FName Key;

	/** Name of metadata value */
	UPROPERTY(EditAnywhere, Category = VariableMetaDataEntry)
	FString Value;
};


UCLASS(BlueprintType)
class OPTIMUSCORE_API UOptimusVariableDescription : 
	public UObject
{
	GENERATED_BODY()
public:
	UOptimusVariableDescription() = default;

	/** 
	 * Set the value data storage to match the size required by the DataType. 
	 * If a reallocation is required then the value data will be zeroed.
	 */
	void ResetValueDataSize();

	/** Returns the owning deformer to operate on this variable */
	// FIXME: Move to interface-based system.
	UOptimusDeformer* GetOwningDeformer() const;

	/** An identifier that uniquely identifies this variable */
	UPROPERTY()
	FGuid Guid;

	/** The actual binary data of the value that was written (or the default value) */
	UPROPERTY()
	TArray<uint8> ValueData;
	
	/** Name of the variable */
	UPROPERTY(EditAnywhere, Category = VariableDefinition)
	FName VariableName;

	/** The data type of the variable */
	UPROPERTY(EditAnywhere, Category = VariableDefinition, meta=(UseInVariable))
	FOptimusDataTypeRef DataType;

#if WITH_EDITOR
	void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	void PreEditUndo() override;
	void PostEditUndo() override;
#endif

private:
#if WITH_EDITORONLY_DATA
	FName VariableNameForUndo;
#endif
};
