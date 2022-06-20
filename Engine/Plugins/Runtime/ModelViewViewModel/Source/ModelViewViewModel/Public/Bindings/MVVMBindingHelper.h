// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Templates/ValueOrError.h"
#include "Types/MVVMFieldContext.h"
#include "Types/MVVMFunctionContext.h"
#include "Types/MVVMFieldVariant.h"
#include "Types/MVVMObjectVariant.h"
#include "Types/MVVMBindingName.h"

class FProperty;
class UFunction;

namespace UE::MVVM::BindingHelper
{

	/** Is the Property usable as a source (readable) by the binding system. It may required a Getter to read it. */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsValidForSourceBinding(const FProperty* InProperty);

	/**
	 * Is the Function usable as a source (readable) by the binding system.
	 * @note It may be a BlueprintGetter and binding to the Property would be better in the editor.
	 */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsValidForSourceBinding(const UFunction* InFunction);

	/**
	 * Is the Field usable as a source (readable) by the binding system.
	 * @note It may be a BlueprintGetter and binding to the Property would be better in the editor.
	 */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsValidForSourceBinding(const FMVVMConstFieldVariant InField);

	/** Is the Property usable as a destination (settable) by the binding system. It may required a Setter to write it. */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsValidForDestinationBinding(const FProperty* InProperty);

	/**
	 * Is the Function usable as a destination (settable) by the binding system.
	 * @note It may be a BlueprintSetter and binding to the Property would be better in the editor.
	 */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsValidForDestinationBinding(const UFunction* InFunction);

	/**
	 * Is the Field usable as a destination (settable) by the binding system.
	 * @note It may be a BlueprintSetter and binding to the Property would be better in the editor.
	 */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsValidForDestinationBinding(const FMVVMConstFieldVariant InFunction);

	/**
	 * Is the Function usable as a conversion function by the binding system.
	 */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsValidForRuntimeConversion(const UFunction* InFunction);

#if WITH_EDITOR
	/** Is the Property usable as a source by the binding system and can it be read directly or it requires a Getter. */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsAccessibleDirectlyForSourceBinding(const FProperty* InProperty);

	/** Is the Property usable as a destination by the binding system and can it be write directly or it requires a Setter. */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsAccessibleDirectlyForDestinationBinding(const FProperty* InProperty);

	/** Is the Property usable as a source by the binding system and a Getter exists. */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsAccessibleWithGetterForSourceBinding(const FProperty* InProperty);

	/** Is the Property usable as a destination by the binding system and a Setter exists. */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool IsAccessibleWithSetterForDestinationBinding(const FProperty* InProperty);
#endif

	/**
	 * Returns the Property or the Function that matches that BindingName.
	 * @note It doesn't check for BlueprintGetter or BlueprintSetter.
	 */
	UE_NODISCARD MODELVIEWVIEWMODEL_API FMVVMFieldVariant FindFieldByName(const UStruct* Container, FMVVMBindingName BindingName);

	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<const FProperty*, FString> TryGetPropertyTypeForSourceBinding(const FMVVMConstFieldVariant& InField);
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<const FProperty*, FString> TryGetPropertyTypeForDestinationBinding(const FMVVMConstFieldVariant& InField);
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<const FProperty*, FString> TryGetPropertyTypeForSourceBinding(const FProperty* InProperty);
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<const FProperty*, FString> TryGetPropertyTypeForDestinationBinding(const FProperty* InProperty);
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<const FProperty*, FString> TryGetPropertyTypeForSourceBinding(const UFunction* InFunction);
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<const FProperty*, FString> TryGetPropertyTypeForDestinationBinding(const UFunction* InFunction);

	struct FConversionFunctionArguments
	{
		const FProperty* ReturnProperty = nullptr;
		const FProperty* ArgumentProperty = nullptr;
	};
	/** Returns the Return and 1st argument for a conversion function. */
	UE_NODISCARD MODELVIEWVIEWMODEL_API TValueOrError<FConversionFunctionArguments, FString> TryGetPropertyTypeForConversionFunction(const UFunction* InFunction);

	/** Are type the same or could be converted at runtime. */
	UE_NODISCARD MODELVIEWVIEWMODEL_API bool ArePropertiesCompatible(const FProperty* Source, const FProperty* Destination);

	/**
	 * Returns the "return" property of the function. It can be a none-const ref/out argument.
	 * int Foo(double) -> returns int
	 * void Foo(int&, double) -> returns int
	 * void Foo(const int&, double) -> returns null
	 */
	UE_NODISCARD MODELVIEWVIEWMODEL_API const FProperty* GetReturnProperty(const UFunction* InFunction);

	/**
	 * Returns the first argument property. Ref/Out argument have to be const.
	 * int Foo(double) -> returns double
	 * void Foo(int& Out, double) -> returns double
	 * void Foo(const int& Out, double) -> returns int
	 */
	UE_NODISCARD MODELVIEWVIEWMODEL_API const FProperty* GetFirstArgumentProperty(const UFunction* InFunction);

	/**
	 * Execute a binding that can be
	 *   LocalValue = Src.Property; Dst.Property = LocalValue;
	 *   LocalValue = Src.Property; Dst.SetProperty(LocalValue);
	 *   LocalValue = Src.GetProperty(); Dst.Property = LocalValue;
	 *   LocalValue = Src.GetProperty(); Dst.SetProperty(LocalValue);
	 * with conversion from float to double between the getter and the setter.
	 * 
	 * @note No test is performed to see if the Src Property can be safely assign to the Destination Property. Use with caution.
	 * @note No test is performed to see if the Src.GetProperty can be safely executed. Use with caution.
	 * @note No test is performed to see if the Dst.SetProperty can be safely executed. Use with caution.
	 */
	MODELVIEWVIEWMODEL_API void ExecuteBinding_NoCheck(const FFieldContext& Source, const FFieldContext& Destination);

	/**
	 * Execute a binding that can be
	 *   LocalValue = Src.Property; LocalValue = ConcFuncOwner.ConversionFunction(LocalValue); Dst.Property = LocalValue;
	 *   LocalValue = Src.Property; LocalValue = ConcFuncOwner.ConversionFunction(LocalValue); Dst.SetProperty(LocalValue);
	 *   LocalValue = Src.GetProperty(); LocalValue = ConcFuncOwner.ConversionFunction(LocalValue); Dst.Property = LocalValue;
	 *   LocalValue = Src.GetProperty(); LocalValue = ConcFuncOwner.ConversionFunction(LocalValue); Dst.SetProperty(LocalValue);
	 * with conversion from float to double between the getter and the conversion function and/or the conversion function and the setter.
	 *
	 * @note No test is performed to see if the Src Property can be safely assign to the Destination Property. Use with caution.
	 * @note No test is performed to see if the Src.GetProperty can be safely executed. Use with caution.
	 * @note No test is performed to see if the Dst.SetProperty can be safely executed. Use with caution.
	 * @note No test is performed to see if the ConversionFunction can be safely executed. Use with caution.
	 */
	MODELVIEWVIEWMODEL_API void ExecuteBinding_NoCheck(const FFieldContext& Source, const FFieldContext& Destination, const FFunctionContext& ConversionFunction);
} //namespace
