// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstancedStruct.h"
#include "StateTreeTypes.generated.h"

STATETREEMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogStateTree, Warning, All);

#ifndef WITH_STATETREE_DEBUG
#define WITH_STATETREE_DEBUG (!(UE_BUILD_SHIPPING || UE_BUILD_SHIPPING_WITH_EDITOR || UE_BUILD_TEST) && 1)
#endif // WITH_STATETREE_DEBUG

/** Status describing current ticking state. */
UENUM()
enum class EStateTreeRunStatus : uint8
{
	Running,			/// Tree is still running.
	Failed,				/// Tree execution has stopped on failure.
	Succeeded,			/// Tree execution has stopped on success.
	Unset,				/// Status not set.
};

/**  Evaluator evaluation type. */
UENUM()
enum class EStateTreeEvaluationType : uint8
{
	PreSelect,			/// Called during selection process on states that have not been visited yet.
    Tick,				/// Called during tick on active states.
};

/**  State change type. Passed to EnterState() and ExitState() to indicate how the state change affects the state and Evaluator or Task is on. */
UENUM()
enum class EStateTreeStateChangeType : uint8
{
	None,				/// Not an activation
	Changed,			/// The state became activated or deactivated.
    Sustained,			/// The state is parent of new active state and sustained previous active state.
};

/** Transitions behavior. */
UENUM()
enum class EStateTreeTransitionType : uint8
{
	Succeeded,			/// Signal StateTree execution succeeded.
	Failed,				/// Signal StateTree execution failed.
	GotoState,			/// Transition to specified state.
	NotSet,				/// No transition.
	NextState,			/// Goto next sibling state.
};

/** Operand between conditions */
UENUM()
enum class EStateTreeConditionOperand : uint8
{
	Copy UMETA(Hidden),	/// Copy result
	And,				/// Combine results with AND.
	Or,					/// Combine results with OR.
};

namespace UE::StateTree
{
	constexpr int32 MaxConditionIndent = 4; 
}; // UE::StateTree



/** Transitions event. */
UENUM()
enum class EStateTreeTransitionEvent : uint8
{
	None = 0 UMETA(Hidden),
	OnCompleted = 0x1 | 0x2,
    OnSucceeded = 0x1,
    OnFailed = 0x2,
    OnCondition = 0x4,
};
ENUM_CLASS_FLAGS(EStateTreeTransitionEvent)

/**
 * Handle that is used to refer compact state tree data.
 */
USTRUCT(BlueprintType)
struct STATETREEMODULE_API FStateTreeHandle
{
	GENERATED_BODY()

	static const uint16 InvalidIndex = uint16(-1);		// Index value indicating invalid item.
	static const uint16 SucceededIndex = uint16(-2);	// Index value indicating a Succeeded item.
	static const uint16 FailedIndex = uint16(-3);		// Index value indicating a Failed item.
	
	static const FStateTreeHandle Invalid;
	static const FStateTreeHandle Succeeded;
	static const FStateTreeHandle Failed;

	FStateTreeHandle() = default;
	explicit FStateTreeHandle(uint16 InIndex) : Index(InIndex) {}

	bool IsValid() const { return Index != InvalidIndex; }

	bool operator==(const FStateTreeHandle& RHS) const { return Index == RHS.Index; }
	bool operator!=(const FStateTreeHandle& RHS) const { return Index != RHS.Index; }

	FString Describe() const
	{
		switch (Index)
		{
		case InvalidIndex:		return TEXT("Invalid Item");
		case SucceededIndex: 	return TEXT("Succeeded Item");
		case FailedIndex: 		return TEXT("Failed Item");
		default: 				return FString::Printf(TEXT("%d"), Index);
		}
	}

	UPROPERTY()
	uint16 Index = InvalidIndex;
};


/**
 * Describes an array of active states in a State Tree.
 */
USTRUCT(BlueprintType)
struct STATETREEMODULE_API FStateTreeActiveStates
{
	GENERATED_BODY()

	static constexpr uint8 MaxStates = 8;	// Max number of active states

	FStateTreeActiveStates() = default;
	
	explicit FStateTreeActiveStates(const FStateTreeHandle StateHandle)
	{
		Push(StateHandle);
	}
	
	/** Resets the active state array to empty. */
	void Reset()
	{
		NumStates = 0;
	}

	/** Pushes new state at the back of the array and returns true if there was enough space. */
	bool Push(const FStateTreeHandle StateHandle)
	{
		if ((NumStates + 1) > MaxStates)
		{
			return false;
		}
		
		States[NumStates++] = StateHandle;
		
		return true;
	}

	/** Pushes new state at the front of the array and returns true if there was enough space. */
	bool PushFront(const FStateTreeHandle StateHandle)
	{
		if ((NumStates + 1) > MaxStates)
		{
			return false;
		}

		NumStates++;
		for (int32 Index = (int32)NumStates - 1; Index > 0; Index--)
		{
			States[Index] = States[Index - 1];
		}
		States[0] = StateHandle;
		
		return true;
	}

	/** Pops a state from the back of the array and returns the popped value, or invalid handle if the array was empty. */
	FStateTreeHandle Pop()
	{
		if (NumStates == 0)
		{
			return FStateTreeHandle::Invalid;			
		}

		const FStateTreeHandle Ret = States[NumStates - 1];
		NumStates--;
		return Ret;
	}

	/** Sets the number of states, new states are set to invalid state. */
	void SetNum(const int32 NewNum)
	{
		check(NewNum >= 0 && NewNum <= MaxStates);
		if (NewNum > (int32)NumStates)
		{
			for (int32 Index = NumStates; Index < NewNum; Index++)
			{
				States[Index] = FStateTreeHandle::Invalid;
			}
		}
		NumStates = NewNum;
	}

	/** Returns true of the array contains specified state. */
	bool Contains(const FStateTreeHandle StateHandle) const
	{
		for (const FStateTreeHandle& Handle : *this)
		{
			if (Handle == StateHandle)
			{
				return true;
			}
		}
		return false;
	}

	/** Returns index of a state, searching in reverse order. */
	int32 IndexOfReverse(const FStateTreeHandle StateHandle) const
	{
		for (int32 Index = (int32)NumStates - 1; Index >= 0; Index--)
		{
			if (States[Index] == StateHandle)
				return Index;
		}
		return INDEX_NONE;
	}
	
	/** Returns last state in the array, or invalid state if the array is empty. */
	FStateTreeHandle Last() const { return NumStates > 0 ? States[NumStates - 1] : FStateTreeHandle::Invalid; }
	
	/** Returns number of states in the array. */
	int32 Num() const { return NumStates; }

	/** Returns true if the index is within array bounds. */
	bool IsValidIndex(const int32 Index) const { return Index >= 0 && Index < (int32)NumStates; }
	
	/** Returns true if the array is empty. */
	bool IsEmpty() const { return NumStates == 0; } 

	/** Returns a specified state in the array. */
	FORCEINLINE FStateTreeHandle operator[](const int32 Index) const
	{
		check(Index >= 0 && Index < (int32)NumStates);
		return States[Index];
	}

	/** Returns mutable reference to a specified state in the array. */
	FORCEINLINE FStateTreeHandle& operator[](const int32 Index)
	{
		check(Index >= 0 && Index < (int32)NumStates);
		return States[Index];
	}

	/** Returns a specified state in the array, or FStateTreeHandle::Invalid if Index is out of array bounds. */
	FStateTreeHandle GetStateSafe(const int32 Index) const
	{
		return (Index >= 0 && Index < (int32)NumStates) ? States[Index] : FStateTreeHandle::Invalid;
	}

	/**
	 * DO NOT USE DIRECTLY
	 * STL-like iterators to enable range-based for loop support.
	 */
	FORCEINLINE FStateTreeHandle* begin() { return &States[0]; }
	FORCEINLINE FStateTreeHandle* end  () { return &States[0] + Num(); }
	FORCEINLINE const FStateTreeHandle* begin() const { return &States[0]; }
	FORCEINLINE const FStateTreeHandle* end  () const { return &States[0] + Num(); }


	UPROPERTY(EditDefaultsOnly, Category = Default)
	FStateTreeHandle States[MaxStates];

	UPROPERTY(EditDefaultsOnly, Category = Default)
	uint8 NumStates = 0;
};

/**
 * Describes a state tree transition. Source is the state where the transition started, Target describes the state where the transition pointed at,
 * and Next describes the selected state. The reason Transition and Next are different is that Transition state can be a selector state,
 * in which case the children will be visited until a leaf state is found, which will be the next state.
 */
USTRUCT(BlueprintType)
struct STATETREEMODULE_API FStateTreeTransitionResult
{
	GENERATED_BODY()

	FStateTreeTransitionResult() = default;

	/** Current active states, where the transition started. */
	UPROPERTY(EditDefaultsOnly, Category = Default, BlueprintReadOnly)
	FStateTreeActiveStates CurrentActiveStates;

	/** Current Run status. */
	UPROPERTY(EditDefaultsOnly, Category = Default, BlueprintReadOnly)
	EStateTreeRunStatus CurrentRunStatus = EStateTreeRunStatus::Unset;

	/** Transition target state */
	UPROPERTY(EditDefaultsOnly, Category = Default, BlueprintReadOnly)
	FStateTreeHandle TargetState = FStateTreeHandle::Invalid;

	/** States selected as result of the transition. */
	UPROPERTY(EditDefaultsOnly, Category = Default, BlueprintReadOnly)
	FStateTreeActiveStates NextActiveStates;

	/** The current state being executed. On enter/exit callbacks this is the state of the task or evaluator. */
	UPROPERTY(EditDefaultsOnly, Category = Default, BlueprintReadOnly)
	FStateTreeHandle CurrentState = FStateTreeHandle::Invalid;
};


/**
 *  Runtime representation of a StateTree transition.
 */
USTRUCT()
struct STATETREEMODULE_API FCompactStateTransition
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 ConditionsBegin = 0;							// Index to first condition to test
	UPROPERTY()
	FStateTreeHandle State = FStateTreeHandle::Invalid;	// Target state of the transition
	UPROPERTY()
	EStateTreeTransitionType Type = EStateTreeTransitionType::NotSet;	// Type of the transition.
	UPROPERTY()
	EStateTreeTransitionEvent Event = EStateTreeTransitionEvent::None;	// Type of the transition event.
	UPROPERTY()
	uint8 GateDelay = 0;								// The time the conditions need to hold true for the transition to become active, in tenths of a seconds.
	UPROPERTY()
	uint8 ConditionsNum = 0;							// Number of conditions to test.
};

/**
 *  Runtime representation of a StateTree state.
 */
USTRUCT()
struct STATETREEMODULE_API FCompactStateTreeState
{
	GENERATED_BODY()

	/** @return Index to the next sibling state. */
	uint16 GetNextSibling() const { return ChildrenEnd; }

	/** @return True if the state has any child states */
	bool HasChildren() const { return ChildrenEnd > ChildrenBegin; }

	UPROPERTY()
	FName Name;											// Name of the State

	UPROPERTY()
	FStateTreeHandle LinkedState = FStateTreeHandle::Invalid;	// Linked state 

	UPROPERTY()
	FStateTreeHandle Parent = FStateTreeHandle::Invalid;		// Parent state
	UPROPERTY()
	uint16 ChildrenBegin = 0;							// Index to first child state
	UPROPERTY()
	uint16 ChildrenEnd = 0;								// Index one past the last child state

	UPROPERTY()
	uint16 EnterConditionsBegin = 0;					// Index to first state enter condition
	UPROPERTY()
	uint16 TransitionsBegin = 0;						// Index to first transition
	UPROPERTY()
	uint16 TasksBegin = 0;								// Index to first task
	UPROPERTY()
	uint16 EvaluatorsBegin = 0;							// Index to first evaluator

	UPROPERTY()
	uint8 EnterConditionsNum = 0;						// Number of enter conditions
	UPROPERTY()
	uint8 TransitionsNum = 0;							// Number of transitions
	UPROPERTY()
	uint8 TasksNum = 0;									// Number of tasks
	UPROPERTY()
	uint8 EvaluatorsNum = 0;							// Number of evaluators
};

/** An offset into the StateTree runtime storage type to get a struct view to a specific Task, Evaluator, or Condition. */
struct FStateTreeInstanceStorageOffset
{
	FStateTreeInstanceStorageOffset() = default;
	FStateTreeInstanceStorageOffset(const UScriptStruct* InStruct, const int32 InOffset) : Struct(InStruct), Offset(InOffset) {}

	/** Struct of the data the offset points at */
	const UScriptStruct* Struct = nullptr;
	/** Offset within the storage struct */
	int32 Offset = 0;
};

UENUM()
enum class EStateTreeExternalDataRequirement : uint8
{
	Required,	// StateTree cannot be executed if the data is not present.
	Optional,	// Data is optional for StateTree execution.
};

/**
 * Handle to access an external struct or object.
 * Note: Use the templated version below. 
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeExternalDataHandle
{
	GENERATED_BODY()

	static const FStateTreeExternalDataHandle Invalid;
	static constexpr uint8 IndexNone = MAX_uint8;

	static bool IsValidIndex(const int32 Index) { return Index >= 0 && Index < (int32)IndexNone; }

	bool IsValid() const { return DataViewIndex != IndexNone; }

	UPROPERTY()
	uint8 DataViewIndex = IndexNone;
};

/**
 * Handle to access an external struct or object.
 * This reference handle can be used in StateTree tasks and evaluators to have quick access to external data.
 * The type provided to the template is used by the linker and context to pass along the type.
 *
 * USTRUCT()
 * struct FExampleTask : public FStateTreeTaskBase
 * {
 *    ...
 *
 *    bool Link(FStateTreeLinker& Linker)
 *    {
 *      Linker.LinkExternalData(ExampleSubsystemHandle);
 *      return true;
 *    }
 * 
 *    EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
 *    {
 *      const UExampleSubsystem& ExampleSubsystem = Context.GetExternalData(ExampleSubsystemHandle);
 *      ...
 *    }
 *
 *    TStateTreeExternalDataHandle<UExampleSubsystem> ExampleSubsystemHandle;
 * }
 */
template<typename T, EStateTreeExternalDataRequirement Req = EStateTreeExternalDataRequirement::Required>
struct TStateTreeExternalDataHandle : FStateTreeExternalDataHandle
{
	typedef T DataType;
	static constexpr EStateTreeExternalDataRequirement DataRequirement = Req;
};

UENUM()
enum class EStateTreePropertyIndirection : uint8
{
	Offset,
	Indirect,
};

UENUM()
enum class EStateTreePropertyUsage : uint8
{
	Invalid,
	Input,
	Parameter,
	Output,
	Internal,
};


USTRUCT()
struct STATETREEMODULE_API FStateTreeInstanceDataPropertyHandle
{
	GENERATED_BODY()

	static constexpr uint8 IndexNone = MAX_uint8;

	static bool IsValidIndex(const int32 Index) { return Index >= 0 && Index < (int32)IndexNone; }

	bool IsValid() const { return DataViewIndex != IndexNone; }

	uint16 PropertyOffset = 0;
	uint8 DataViewIndex = IndexNone;
	EStateTreePropertyIndirection Type = EStateTreePropertyIndirection::Offset;
};

template<typename T>
struct TStateTreeInstanceDataPropertyHandle : FStateTreeInstanceDataPropertyHandle
{
	typedef T DataType;
};

/**
 * Describes a parameter of the state tree that could be used for bindings.
 * Can also be provided externally by a StateTreeReference to parameterized the tree.
 * @see FStateTreeReference
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeParameterDesc
{
	GENERATED_BODY()

	/** Indicates that parameters hold the same data type */
	bool IsSameType(const FStateTreeParameterDesc& RHS) const { return Parameter.GetScriptStruct() == RHS.Parameter.GetScriptStruct(); }

	/** Indicates that parameters hold the same data type and have the same identifier. They might have different values. */
	bool IsMatching(const FStateTreeParameterDesc& RHS) const
	{				
#if WITH_EDITORONLY_DATA
		return Name == RHS.Name && ID == RHS.ID && IsSameType(RHS);
#else
		return Name == RHS.Name && IsSameType(RHS);
#endif			
	}
	
	friend FString LexToString(const FStateTreeParameterDesc& Desc)
	{
		return FString::Printf(TEXT("{%s} %s"),
			Desc.Parameter.GetScriptStruct() != nullptr ? *Desc.Parameter.GetScriptStruct()->GetName() : TEXT("null type"),
			*Desc.Name.ToString());
	}

	bool IsValid() const
	{
		return DataViewIndex != InvalidIndex;
	}

	/** The type of the parameter. */
	UPROPERTY(EditDefaultsOnly, Category = Common)
	FInstancedStruct Parameter;

	/** Name of the parameter. */
	UPROPERTY(EditDefaultsOnly, Category = Common)
	FName Name;

	/** The runtime data's data view index in the StateTreeExecutionContext, and source struct index in property binding. */
	UPROPERTY()
	uint16 DataViewIndex = InvalidIndex;

	static constexpr uint16 InvalidIndex = MAX_uint16;

#if WITH_EDITORONLY_DATA
	/** Unique identifier. */
	UPROPERTY()
	FGuid ID;
#endif
};


/**
 * Container for StateTree parameters.
 * Could be used for parameter definitions (within the StateTreeAsset) and parameterization (StateTreeReference).
 */
USTRUCT()
struct FStateTreeParameters
{
	GENERATED_BODY()

	void Reset() { Parameters.Reset(); }
	
	UPROPERTY(EditDefaultsOnly, Category= Common)
	TArray<FStateTreeParameterDesc> Parameters;
};


/**
 * Describes an external data. The data can point to a struct or object.
 * The code that handles StateTree ticking is responsible for passing in the actually data, see FStateTreeExecutionContext.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeExternalDataDesc
{
	GENERATED_BODY()

	FStateTreeExternalDataDesc() = default;
	FStateTreeExternalDataDesc(const UStruct* InStruct, const EStateTreeExternalDataRequirement InRequirement) : Struct(InStruct), Requirement(InRequirement) {}

	FStateTreeExternalDataDesc(const FName InName, const UStruct* InStruct, const FGuid InGuid)
		: Struct(InStruct)
		, Name(InName)
#if WITH_EDITORONLY_DATA
		, ID(InGuid)
#endif
	{}

	bool operator==(const FStateTreeExternalDataDesc& Other) const
	{
		return Struct == Other.Struct && Requirement == Other.Requirement;
	}
	
	/** Class or struct of the external data. */
	UPROPERTY();
	const UStruct* Struct = nullptr;

	/**
	 * Name of the external data. Used only for bindable external data (enforced by the schema).
	 * External data linked explicitly by the nodes (i.e. LinkExternalData) are identified only
	 * by their type since they are used for unique instance of a given type.  
	 */
	UPROPERTY(VisibleAnywhere, Category = Common)
	FName Name;
	
	/** Handle/Index to the StateTreeExecutionContext data views array */
	UPROPERTY();
	FStateTreeExternalDataHandle Handle;

	/** Describes if the data is required or not. */
	UPROPERTY();
	EStateTreeExternalDataRequirement Requirement = EStateTreeExternalDataRequirement::Required;

#if WITH_EDITORONLY_DATA
	/** Unique identifier. Used only for bindable external data. */
	UPROPERTY()
	FGuid ID;
#endif
};


#define STATETREE_INSTANCEDATA_PROPERTY(Struct, Member) \
		decltype(Struct::Member){}, Struct::StaticStruct(), TEXT(#Member)
