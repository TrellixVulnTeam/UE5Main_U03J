// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Fundamental/Task.h"
#include "Async/Fundamental/Scheduler.h"
#include "Async/TaskTrace.h"
#include "Templates/Invoke.h"
#include "Templates/RefCounting.h"
#include "Templates/TypeCompatibleBytes.h"
#include "Misc/Timeout.h"
#include "Containers/LockFreeList.h"
#include "Containers/LockFreeFixedSizeAllocator.h"
#include "Experimental/ConcurrentLinearAllocator.h"
#include "Templates/UnrealTemplate.h"
#include "Math/NumericLimits.h"
#include "Misc/ScopeLock.h"
#include "CoreTypes.h"

#include <atomic>
#include <type_traits>

namespace UE::Tasks
{
	using LowLevelTasks::ETaskPriority;

	class FPipe;

	namespace Private
	{
		class FTaskBase;

		// returns the task (if any) that is being executed by the current thread
		CORE_API FTaskBase* GetCurrentTask();
		// sets the current task and returns the previous current task
		CORE_API FTaskBase* ExchangeCurrentTask(FTaskBase* Task);

		// special task priorities for tasks that are never scheduled
		enum class EExtendedTaskPriority
		{
			None,
			Inline, // a task priority for "inline" task execution - a task is executed "inline" by the thread that unlocked it, w/o scheduling
			TaskEvent, // a task priority used by task events, allows to shortcut task execution
		};

		// An abstract base class for task implementation. 
		// Implements internal logic of task prerequisites, nested tasks and deep task retraction.
		// Implements intrusive ref-counting and so can be used with TRefCountPtr.
		// It doesn't store task body, instead it expects a derived class to provide a task body as a parameter to `TryExecute` method. @see TExecutableTask
		class FTaskBase
		{
			UE_NONCOPYABLE(FTaskBase);

			// `ExecutionFlag` is set at the beginning of execution as the most significant bit of `NumLocks` and indicates a switch 
			// of `NumLocks` from "execution prerequisites" (a number of uncompleted prerequisites that block task execution) to 
			// "completion prerequisites" (a number of nested uncompleted tasks that block task completion)
			static constexpr uint32 ExecutionFlag = 0x80000000;

			////////////////////////////////////////////////////////////////////////////
			// ref-count
		public:
			void AddRef()
			{
				RefCount.fetch_add(1, std::memory_order_relaxed);
			}

			void Release()
			{
				uint32 LocalRefCount = RefCount.fetch_sub(1, std::memory_order_release) - 1;
				if (LocalRefCount == 0)
				{
					std::atomic_thread_fence(std::memory_order_acquire);
					delete this;
				}
			}

			uint32 GetRefCount() const
			{
				return RefCount.load(std::memory_order_relaxed);
			}

		private:
			std::atomic<uint32> RefCount;
			////////////////////////////////////////////////////////////////////////////

		protected:
			explicit FTaskBase(uint32 InitRefCount)
				: RefCount(InitRefCount)
			{
			}

			void Init(const TCHAR* InDebugName, ETaskPriority InPriority, EExtendedTaskPriority InExtendedPriority)
			{
				// store debug name, priority and an adaptor for task execution in low-level task. The task body can't be stored as this task implementation needs to do some accounting
				// before the task is executed (e.g. maintainance of TLS "current task")
				LowLevelTask.Init(InDebugName, InPriority,
					[
						this,
						// releasing scheduler's task reference can cause task's automatic destruction and so must be done after the low-level task
						// task is flagged as completed. The task is flagged as completed after the continuation is executed but before its destroyed.
						// `Deleter` is captured by value and is destroyed along with the continuation, calling the given functor on destruction
						Deleter = LowLevelTasks::TDeleter<FTaskBase, &FTaskBase::Release>{ this }
					]
					{
						TryExecuteTask();
					}
				);
				ExtendedPriority = InExtendedPriority;
			}

			virtual ~FTaskBase()
			{
				check(IsCompleted());
			}

			// will be called to execute the task, must be implemented by a derived class that should call `FTaskBase::TryExecute` and pass the task body
			// @see TExecutableTask::TryExecuteTask
			virtual bool TryExecuteTask() = 0;

		public:
			EExtendedTaskPriority GetExtendedPriority() const
			{
				return ExtendedPriority;
			}

			// The task will be executed only when all prerequisites are completed. The task type must be a task handle that holds a pointer to
			// FTaskBase as its `Pimpl` member (see Tasks::TTaskBase).
			// Must not be called concurrently
			void AddPrerequisites(FTaskBase& Prerequisite)
			{
				checkf(NumLocks.load(std::memory_order_relaxed) >= NumInitialLocks && NumLocks.load(std::memory_order_relaxed) < ExecutionFlag, TEXT("Prerequisites can be added only before the task is launched"));

				// registering the task as a subsequent of the given prerequisite can cause its immediate launch by the prerequisite
				// (if the prerequisite has been completed on another thread), so we need to keep the task locked by assuming that the 
				// prerequisite can be added successfully, and release the lock if it wasn't
				uint32 PrevNumLocks = NumLocks.fetch_add(1, std::memory_order_relaxed); // relaxed because the following
				// `AddSubsequent` provides required sync
				checkf(PrevNumLocks + 1 < ExecutionFlag, TEXT("Max number of nested tasks reached: %d"), ExecutionFlag);

				if (Prerequisite.AddSubsequent(*this)) // linearisation point, acq_rel semantic
				{
					Prerequisite.AddRef(); // keep it alive until this task's execution
					Prerequisites.Push(&Prerequisite); // release memory order
				}
				else
				{
					// failed to add the prerequisite (too late), correct the number
					NumLocks.fetch_sub(1, std::memory_order_relaxed); // relaxed because the previous `AddSubsequent` call provides required sync
				}
			}

			// The task will be executed only when all prerequisites are completed. The task type must be a task handle that holds a pointer to
			// FTaskBase as its `Pimpl` member (see Tasks::TTaskBase).
			// Must not be called concurrently
			template<typename HigherLevelTaskType, decltype(std::declval<HigherLevelTaskType>().Pimpl)* = nullptr>
			void AddPrerequisites(const HigherLevelTaskType& Prerequisite)
			{
				AddPrerequisites(*Prerequisite.Pimpl);
			}

			// The task will be executed only when all prerequisites are completed.
			// Must not be called concurrently.
			// @param InPrerequisites - an iterable collection of tasks
			template<typename PrerequisiteCollectionType, decltype(std::declval<PrerequisiteCollectionType>().begin())* = nullptr>
			void AddPrerequisites(const PrerequisiteCollectionType& InPrerequisites)
			{
				checkf(NumLocks.load(std::memory_order_relaxed) >= NumInitialLocks && NumLocks.load(std::memory_order_relaxed) < ExecutionFlag, TEXT("Prerequisites can be added only before the task is launched"));

				// registering the task as a subsequent of the given prerequisite can cause its immediate launch by the prerequisite
				// (if the prerequisite has been completed on another thread), so we need to keep the task locked by assuming that the 
				// prerequisite can be added successfully, and release the lock if it wasn't
				uint32 PrevNumLocks = NumLocks.fetch_add(GetNum(InPrerequisites), std::memory_order_relaxed); // relaxed because the following
				// `AddSubsequent` provides required sync
				checkf(PrevNumLocks + GetNum(InPrerequisites) < ExecutionFlag, TEXT("Max number of nested tasks reached: %d"), ExecutionFlag);

				uint32 NumCompletedPrerequisites = 0;
				for (auto& Prereq : InPrerequisites)
				{
					// prerequisites can be either `FTaskBase*` or its Pimpl handle
					FTaskBase* Prerequisite;
					using FPrerequisiteType = std::decay_t<decltype(*std::declval<PrerequisiteCollectionType>().begin())>;
					if constexpr (std::is_same_v<FPrerequisiteType, FTaskBase*>)
					{
						Prerequisite = Prereq;
					}
					else if constexpr (std::is_pointer_v<FPrerequisiteType>)
					{
						Prerequisite = Prereq->Pimpl;
					}
					else
					{
						Prerequisite = Prereq.Pimpl;
					}

					if (Prerequisite->AddSubsequent(*this)) // acq_rel memory order
					{
						Prerequisite->AddRef(); // keep it alive until this task's execution
						Prerequisites.Push(Prerequisite); // release memory order
					}
					else
					{
						++NumCompletedPrerequisites;
					}
				}

				// unlock for prerequisites that weren't added
				NumLocks.fetch_sub(NumCompletedPrerequisites, std::memory_order_relaxed);  // relaxed because the previous 
				// `AddSubsequent` provides required sync
			}

			// the task unlocks all its subsequents on completion.
			// returns false if the task is already completed and the subsequent wasn't added
			bool AddSubsequent(FTaskBase& Subsequent)
			{
				return Subsequents.PushIfNotClosed(&Subsequent);
			}

			// A piped task is executed after the previous task from this pipe is completed. Tasks from the same pipe are not executed
			// concurrently (so don't require synchronization), but not necessarily on the same thread.
			// @See FPipe
			void SetPipe(FPipe& InPipe)
			{
				// keep the task locked until it's pushed into the pipe
				NumLocks.fetch_add(1, std::memory_order_relaxed); // the order doesn't matter as this happens before the task is launched
				Pipe = &InPipe;
			}

			FPipe* GetPipe() const
			{
				return Pipe;
			}

			// Tries to schedule task execution. Returns false if the task has incomplete dependencies (prerequisites or is blocked by a pipe). 
			// In this case the task will be automatically scheduled when all dependencies are completed.
			bool TryLaunch()
			{
				TaskTrace::Launched(GetTraceId(), LowLevelTask.GetDebugName(), true, (ENamedThreads::Type)0xff);
				return TryUnlock();
			}

			// @return true if the task was executed and all its nested tasks are completed
			bool IsCompleted() const
			{
				return Subsequents.IsClosed();
			}

			// Tries to pull out the task from the system and execute it. If the task is locked by either prerequisites or nested tasks, tries to retract and execute them recursively. 
			// @return true if task is completed, not necessarily by retraction. If the task is being executed (or its dependency) in parallel, it doesn't wait for task completion and 
			// returns false immediately.
			bool TryRetractAndExecute(uint32 RecursionDepth = 0)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(TaskRetraction);

				if (IsCompleted())
				{
					return true;
				}

				// avoid stack overflow. is not expected in a real-life cases but happens in stress tests
				if (RecursionDepth == 200)
				{
					return false;
				}
				++RecursionDepth;

				// returns false if the task has passed "pre-scheduling" state: all (if any) prerequisites are completed
				auto IsLockedByPrerequisites = [this]
				{
					uint32 LocalNumLocks = NumLocks.load(std::memory_order_relaxed); // the order doesn't matter as this "happens before" task execution
					return LocalNumLocks != 0 && LocalNumLocks < ExecutionFlag;
				};

				if (IsLockedByPrerequisites())
				{
					// try to unlock the task. even if (some or all) prerequisites retraction fails we still proceed to try helping with other prerequisites or this task execution

					// prerequisites are "consumed" here even if their retraction fails. this means that once prerequisite retraction failed, it won't be performed again. 
					// this can be potentially improved by using a different container for prerequisites
					while (FTaskBase* Prerequisite = Prerequisites.Pop())
					{
						// ignore if retraction failed, as this thread still can try to help with other prerequisites instead of being blocked in waiting
						Prerequisite->TryRetractAndExecute(RecursionDepth);
						Prerequisite->Release();
					}
				}

				// next we try to execute the task, despite we haven't verified that the task is unlocked. trying to obtain execution permission will fail in this case

				if (ExtendedPriority == EExtendedTaskPriority::TaskEvent)
				{
					if (!TrySetExecutionFlag())
					{
						return false;
					}

					// task events have nothing to execute, and so can't have nested task, just close it
					Close();
					ReleaseInternalReference();
					return true;
				}

				if (!TryExecuteTask())
				{
					return false; // still locked by prerequisites or another thread managed to set execution flag first
					// we could try to help with nested tasks execution (the task execution could already spawned a couple of nested tasks sitting in the queue). 
					// it's unclear how important this is, but this would definitely lead to more complicated impl. we can revisit this once we see such instances in profiler captures
				}

				TRACE_CPUPROFILER_EVENT_SCOPE(SuccessfulTaskRetraction);

				// the task was launched so the scheduler will handle the internal reference held by low-level task

				if (IsCompleted()) // still can be hold back by nested tasks, this is an optional early out for better perf
				{
					return true;
				}

				// retract nested tasks, if any
				{
					// keep trying retracting all nested tasks even if some of them fail, so the current worker can contribute instead of being blocked
					bool bSucceeded = true;
					// prerequisites are "consumed" here even if their retraction fails. this means that once prerequisite retraction failed, it won't be performed again. 
					// this can be potentially improved by using a different container for prerequisites
					while (FTaskBase* Prerequisite = Prerequisites.Pop())
					{
						if (!Prerequisite->TryRetractAndExecute(RecursionDepth))
						{
							bSucceeded = false;
						}
						Prerequisite->Release();
					}

					if (!bSucceeded)
					{
						return false;
					}
				}

				// it happens that all nested tasks are completed and are in the process of completing the parent (this task) concurrently, 
				// but the flag is not set yet. wait for it to maintain postconditions
				while (!IsCompleted())
				{
					FPlatformProcess::Yield();
				}

				return true;
			}

			// releases internal reference and maintains low-level task state. must be called iff the task was never launched, otherwise 
			// the scheduler will do this in due course
			void ReleaseInternalReference()
			{
				verify(LowLevelTask.TryCancel());
			}

			// adds a nested task that must be completed before the parent (this) is completed
			void AddNested(FTaskBase& Nested)
			{
				uint32 PrevNumLocks = NumLocks.fetch_add(1, std::memory_order_relaxed); // in case we'll succeed in adding subsequent, 
				// "happens before" registering this task as a subsequent
				checkf(PrevNumLocks + 1 < TNumericLimits<uint32>::Max(), TEXT("Max number of nested tasks reached: %d"), TNumericLimits<uint32>::Max() - ExecutionFlag);
				checkf(PrevNumLocks > ExecutionFlag, TEXT("Internal error: nested tasks can be added only during parent's execution (%u)"), PrevNumLocks);

				if (Nested.AddSubsequent(*this)) // "release" memory order
				{
					Nested.AddRef(); // keep it alive as we store it in `Prerequisites` and we can need it to try to retract it. it's released on closing the task
					Prerequisites.Push(&Nested);
				}
				else
				{
					NumLocks.fetch_sub(1, std::memory_order_relaxed);
				}
			}

			// waits for task's completion, with optional timeout. Tries to retract the task and execute it in-place, if failed - blocks until the task 
			// is completed by another thread. If timeout is zero, tries to retract the task and returns immedially after that. 
			// `Wait(FTimespan::Zero())` still tries to retract and execute the task, use `IsCompleted()` to check for completeness. 
			// The version w/o timeout is slightly more efficient.
			// @return true if the task is completed
			void Wait();
			bool Wait(FTimespan Timeout);

			// waits until the task is completed while executing other tasks
			void BusyWait()
			{
				TaskTrace::FWaitingScope WaitingScope(GetTraceId());
				TRACE_CPUPROFILER_EVENT_SCOPE(Tasks::BusyWait);
				
				if (!TryRetractAndExecute())
				{
					LowLevelTasks::BusyWaitUntil([this] { return IsCompleted(); });
				}
			}

			// waits until the task is completed or waiting timed out, while executing other tasks
			bool BusyWait(FTimespan InTimeout)
			{
				TaskTrace::FWaitingScope WaitingScope(GetTraceId());
				TRACE_CPUPROFILER_EVENT_SCOPE(Tasks::BusyWait);

				FTimeout Timeout{ InTimeout };
				
				if (TryRetractAndExecute())
				{
					return true;
				}

				LowLevelTasks::BusyWaitUntil([this, Timeout] { return IsCompleted() || Timeout; });
				return IsCompleted();
			}

			// waits until the task is completed or the condition returns true, while executing other tasks
			template<typename ConditionType>
			bool BusyWait(ConditionType&& Condition)
			{
				TaskTrace::FWaitingScope WaitingScope(GetTraceId());
				TRACE_CPUPROFILER_EVENT_SCOPE(Tasks::BusyWait);

				if (TryRetractAndExecute())
				{
					return true;
				}

				LowLevelTasks::BusyWaitUntil(
					[this, Condition = Forward<ConditionType>(Condition)]{ return IsCompleted() || Condition(); }
				);
				return IsCompleted();
			}

			TaskTrace::FId GetTraceId() const
			{
#if UE_TASK_TRACE_ENABLED
				return TraceId;
#else
				return TaskTrace::InvalidId;
#endif
			}

		protected:
			using FTaskBodyType = void(*)(FTaskBase&);

			// tries to get execution permission and if successful, executes given task body and completes the task if there're no pending nested tasks. 
			// does all required accounting before/after task execution. the task can be deleted as a result of this call.
			// @returns true if the task was executed by the current thread
			FORCENOINLINE bool TryExecute(FTaskBodyType TaskBody)
			{
				if (!TrySetExecutionFlag())
				{
					return false;
				}

				AddRef(); // `LowLevelTask` will automatically release the internal reference after execution, but there can be pending nested tasks, so keep it alive
				// it's released either later here if the task is closed, or when the last nested task is completed and unlocks its parent (in `TryUnlock`)

				TaskTrace::FTaskTimingEventScope TaskEventScope(GetTraceId());

				ReleasePrerequisites();

				FTaskBase* PrevTask = ExchangeCurrentTask(this);

				if (GetPipe() != nullptr)
				{
					StartPipeExecution();
				}

				TaskBody(*this);

				if (GetPipe() != nullptr)
				{
					FinishPipeExecution();
				}

				ExchangeCurrentTask(PrevTask);

				// close the task if there are no pending nested tasks
				uint32 LocalNumLocks = NumLocks.fetch_sub(1, std::memory_order_acq_rel) - 1; // "release" to make task execution "happen before" this, and "acquire" to 
				// "sync with" another thread that completed the last nested task
				if (LocalNumLocks == ExecutionFlag) // unlocked (no pending nested tasks)
				{
					Close();
					Release(); // the internal reference that kept the task alive for nested tasks
				} // else there're non completed nested tasks, the last one will unlock, close and release the parent (this task)

				return true;
			}

			// closes task by unlocking its subsequents and flagging it as completed
			void Close()
			{
				checkSlow(!IsCompleted());

				if (GetPipe() != nullptr)
				{
					ClearPipe();
				}

				TArray<FTaskBase*> Subs;
				Subsequents.PopAllAndClose(Subs);
				for (FTaskBase* Sub : Subs)
				{
					Sub->TryUnlock();
				}

				// release nested tasks
				ReleasePrerequisites();

				TaskTrace::Completed(GetTraceId());
			}

			CORE_API void ClearPipe();

		private:
			// A task can be locked for execution (by prerequisites or if it's not launched yet) or for completion (by nested tasks).
			// This method is called to unlock the task and so can result in its scheduling (and execution) or completion
			bool TryUnlock()
			{
				FPipe* LocalPipe = GetPipe(); // cache data locally so we won't need to touch the member (read below)

				uint32 PrevNumLocks = NumLocks.fetch_sub(1, std::memory_order_acq_rel); // `acq_rel` to make it happen after task 
				// preparation and before launching it
				// the task can be dead already as the prev line can remove the lock hold for this execution path, another thread(s) can unlock
				// the task, execute, complete and delete it. thus before touching any members or calling methods we need to make sure
				// the task can't be destroyed concurrently

				uint32 LocalNumLocks = PrevNumLocks - 1;

				if (PrevNumLocks < ExecutionFlag)
				{
					// pre-execution state, try to schedule the task

					checkf(PrevNumLocks != 0, TEXT("The task is not locked"));

					bool bPrerequisitesCompleted = LocalPipe == nullptr ? LocalNumLocks == 0 : LocalNumLocks <= 1; // the only remaining lock is pipe's one (if any)
					if (!bPrerequisitesCompleted)
					{
						return false;
					}

					// this thread unlocked the task, no other thread can reach this point concurrently, we can touch the task again

					if (LocalPipe != nullptr)
					{
						bool bFirstPipingAttempt = LocalNumLocks == 1;
						if (bFirstPipingAttempt)
						{
							FTaskBase* PrevPipedTask = TryPushIntoPipe();
							if (PrevPipedTask != nullptr) // the pipe is blocked
							{
								// the prev task in pipe's chain becomes this task's prerequisite, to enabled piped task retraction.
								// no need to AddRef as it's already sorted in `FPipe::PushIntoPipe`
								Prerequisites.Push(PrevPipedTask);
								return false;
							}

							NumLocks.store(0, std::memory_order_release); // release pipe's lock
						}
					}

					if (ExtendedPriority == EExtendedTaskPriority::Inline)
					{
						// "inline" tasks are not scheduled but executed straight away
						TryExecuteTask(); // result doesn't matter, this can fail if task retraction jumped in and got execution
						// permission between this thread unlocked the task and tried to execute it
						verify(LowLevelTask.TryCancel());
					}
					else if (ExtendedPriority == EExtendedTaskPriority::TaskEvent)
					{
						// task events have nothing to execute, try to close it. task retraction can jump in and close the task event, 
						// so this thread still needs to check execution permission
						if (TrySetExecutionFlag())
						{
							// task events are used as an empty prerequisites/subsequents
							ReleasePrerequisites();
							Close();
							verify(LowLevelTask.TryCancel()); // releases the internal reference
						}
					}
					else
					{
						Schedule();
					}

					return true;
				}

				// execution already started (at least), this is nested tasks unlocking their parent
				checkf(PrevNumLocks != ExecutionFlag, TEXT("The task is not locked"));
				if (LocalNumLocks != ExecutionFlag) // still locked
				{
					return false;
				}

				// this thread unlocked the task, no other thread can reach this point concurrently, we can touch the task again
				Close();
				Release(); // the internal reference that kept the task alive for nested tasks
				return true;
			}

			void Schedule()
			{
				LowLevelTasks::FScheduler::Get().TryLaunch(LowLevelTask, LowLevelTasks::EQueuePreference::GlobalQueuePreference, /*bWakeUpWorker=*/ true);
			}

			// is called when the task has no pending prerequisites. Returns the previous piped task if any
			CORE_API FTaskBase* TryPushIntoPipe();

			// only one thread can successfully set execution flag, that grants task execution permission
			// @returns false if another thread got execution permission first
			bool TrySetExecutionFlag()
			{
				uint32 ExpectedUnlocked = 0;
				// set the execution flag and simultenously lock it (+1) so a nested task completion doesn't close it before its execution is finished
				return NumLocks.compare_exchange_strong(ExpectedUnlocked, ExecutionFlag + 1, std::memory_order_acq_rel, std::memory_order_relaxed); // on success 
				// - linearisation point for task execution, on failure - load order doesn't matter
			}

			void ReleasePrerequisites()
			{
				while (FTaskBase* Prerequisite = Prerequisites.Pop())
				{
					Prerequisite->Release();
				}
			}

			CORE_API void StartPipeExecution();
			CORE_API void FinishPipeExecution();

			// the task is already executed but it's not completed yet. This method sets completion flag if there're no pending nested tasks.
			// The task can be deleted as the result of this call.

		private:
			EExtendedTaskPriority ExtendedPriority; // internal priorities, if any

			LowLevelTasks::FTask LowLevelTask;

			// the number of times that the task should be unlocked before it can be scheduled or completed
			// initial count is 1 for launching the task (it can't be scheduled before it's launched)
			// reaches 0 the task is scheduled for execution.
			// NumLocks's the most significant bit (see `ExecutionFlag`) is set on task execution start, and indicates that now 
			// NumLocks is about how many times the task must be unlocked to be completed
			static constexpr uint32 NumInitialLocks = 1;
			std::atomic<uint32> NumLocks{ NumInitialLocks };

#if UE_TASK_TRACE_ENABLED
			TaskTrace::FId TraceId = TaskTrace::GenerateTaskId();
#endif

			// the task is completed when its subsequents list is closed
			TClosableLockFreePointerListUnorderedSingleConsumer<FTaskBase, 0> Subsequents;

			// stores backlinks to prerequsites, either execution prerequisites or nested tasks (completion prerequisites).
			// It's populated in three stages:
			// 1) by adding execution prerequisites, before the task is launched.
			// 2) by piping, when the previous piped task (if any) is added as a prerequisite. can happen concurrently with other threads accessing prerequisites for
			//		task retraction.
			// 3) by adding nested tasks. after piping. during task execution.
			TLockFreePointerListUnordered<FTaskBase, 0> Prerequisites;

			FPipe* Pipe{ nullptr };
		};

		// an extension of FTaskBase for tasks that return a result.
		// Stores task execution result and provides an access to it.
		template<typename ResultType>
		class TTaskWithResult : public FTaskBase
		{
		protected:
			explicit TTaskWithResult(const TCHAR* InDebugName, ETaskPriority InPriority, EExtendedTaskPriority InExtendedPriority, uint32 InitRefCount)
				: FTaskBase(InitRefCount)
			{
				Init(InDebugName, InPriority, InExtendedPriority);
			}

			virtual ~TTaskWithResult() override
			{
				DestructItem(ResultStorage.GetTypedPtr());
			}

		public:
			ResultType& GetResult()
			{
				checkf(IsCompleted(), TEXT("The task must be completed to obtain its result"));
				return *ResultStorage.GetTypedPtr();
			}

		protected:
			TTypeCompatibleBytes<ResultType> ResultStorage;
		};

		struct FTaskBlockAllocationTag : FDefaultBlockAllocationTag
		{
			static constexpr uint32 BlockSize = 64 * 1024;
			static constexpr bool AllowOversizedBlocks = false;
			static constexpr bool RequiresAccurateSize = false;
			static constexpr bool InlineBlockAllocation = true;
			static constexpr const char* TagName = "TaskLinearAllocator";

			using Allocator = TBlockAllocationCache<BlockSize, FAlignedAllocator>;
		};

		// Task implementation that can be executed, as it stores task body. Generic version (for tasks that return non-void results).
		// In most cases it should be allocated on the heap and used with TRefCountPtr, e.g. @see FTaskHandle. With care, can be allocated on the stack, e.g. see 
		// WaitingTask in FTaskBase::Wait().
		// Implements memory allocation from a pooled fixed-size allocator tuned for the everage UE task size
		template<typename TaskBodyType, typename ResultType = TInvokeResult_T<TaskBodyType>, typename Enable = void>
		class TExecutableTask final : public TConcurrentLinearObject<TExecutableTask<TaskBodyType>, FTaskBlockAllocationTag>, public TTaskWithResult<ResultType>
		{
			UE_NONCOPYABLE(TExecutableTask);

		public:
			static TExecutableTask* Create(const TCHAR* DebugName, TaskBodyType TaskBody, ETaskPriority Priority, EExtendedTaskPriority InExtendedPriority = EExtendedTaskPriority::None)
			{
				return new TExecutableTask(DebugName, MoveTemp(TaskBody), Priority, InExtendedPriority);
			}

			TExecutableTask(const TCHAR* InDebugName, TaskBodyType&& TaskBody, ETaskPriority InPriority, EExtendedTaskPriority InExtendedPriority):
				TTaskWithResult<ResultType>(InDebugName, InPriority, InExtendedPriority, 2)
				// 2 init refs: one for the initial reference (we don't increment it on passing to `TRefCountPtr`), and one for the internal 
				// reference that keeps the task alive while it's in the system. is released either on task completion or by the scheduler after
				// trying to execute the task
			{
				new(&TaskBodyStorage) TaskBodyType(MoveTemp(TaskBody));
			}

			virtual ~TExecutableTask() override
			{
				DestructItem(TaskBodyStorage.GetTypedPtr());
			}

			virtual bool TryExecuteTask() override
			{
				return FTaskBase::TryExecute(
					[](FTaskBase& Task) 
					{ 
						TExecutableTask& This = static_cast<TExecutableTask&>(Task);
						new(&This.ResultStorage) ResultType{ Invoke(*This.TaskBodyStorage.GetTypedPtr()) }; 
					}
				);
			}

		private:
			TTypeCompatibleBytes<TaskBodyType> TaskBodyStorage;
		};

		// a specialization for tasks that don't return results
		template<typename TaskBodyType>
		class TExecutableTask<TaskBodyType, typename TEnableIf<TIsSame<TInvokeResult_T<TaskBodyType>, void>::Value>::Type> final : public TConcurrentLinearObject<TExecutableTask<TaskBodyType>, FTaskBlockAllocationTag>, public FTaskBase
		{
			UE_NONCOPYABLE(TExecutableTask);

		public:
			static TExecutableTask* Create(const TCHAR* DebugName, TaskBodyType TaskBody, ETaskPriority Priority, EExtendedTaskPriority InExtendedPriority = EExtendedTaskPriority::None)
			{
				return new TExecutableTask(DebugName, MoveTemp(TaskBody), Priority, InExtendedPriority);
			}

			TExecutableTask(const TCHAR* InDebugName, TaskBodyType&& TaskBody, ETaskPriority InPriority, EExtendedTaskPriority InExtendedPriority) :
				FTaskBase(2)
				// 2 init refs: one for the initial reference (we don't increment it on passing to `TRefCountPtr`), and one for the internal 
				// reference that keeps the task alive while it's in the system. is released either on task completion or by the scheduler after
				// trying to execute the task
			{
				Init(InDebugName, InPriority, InExtendedPriority);
				new(&TaskBodyStorage) TaskBodyType(MoveTemp(TaskBody));
			}

			virtual ~TExecutableTask() override
			{
				DestructItem(TaskBodyStorage.GetTypedPtr());
			}

			virtual bool TryExecuteTask() override
			{
				return TryExecute(
					[](FTaskBase& Task)
					{
						TExecutableTask& This = static_cast<TExecutableTask&>(Task);
						Invoke(*This.TaskBodyStorage.GetTypedPtr()); 
					}
				);
			}

		private:
			TTypeCompatibleBytes<TaskBodyType> TaskBodyStorage;
		};

		// a special kind of task that is used for signalling or dependency management. It can have prerequisites or be used as a prerequisite for other tasks. 
		// It's optimized for the fact that it doesn't have a task body and so doesn't need to be scheduled and executed
		class FTaskEventBase : public TConcurrentLinearObject<FTaskEventBase, FTaskBlockAllocationTag>, public FTaskBase
		{
		public:
			static FTaskEventBase* Create(const TCHAR* DebugName)
			{
				return new FTaskEventBase(DebugName);
			}

		private:
			FTaskEventBase(const TCHAR* InDebugName)
				: FTaskBase(/*InitRefCount=*/ 1) // for the initial reference (we don't increment it on passing to `TRefCountPtr`)
			{
				Init(InDebugName, ETaskPriority::Normal, EExtendedTaskPriority::TaskEvent);
			}

			virtual bool TryExecuteTask() override
			{
				checkNoEntry(); // never executed because it doesn't have a task body
				return true;
			}
		};

		inline void FTaskBase::Wait()
		{
			TaskTrace::FWaitingScope WaitingScope(GetTraceId());
			TRACE_CPUPROFILER_EVENT_SCOPE(Tasks::Wait);

			if (TryRetractAndExecute())
			{
				return;
			}

			if (GetCurrentTask() == this)
			{
				UE_LOG(LogTemp, Fatal, TEXT("A task waiting for itself detected"));
				return;
			}

			FEventRef CompletionEvent;
			auto WaitingTaskBody = [&CompletionEvent] { CompletionEvent->Trigger(); };
			using FWaitingTask = TExecutableTask<decltype(WaitingTaskBody)>;

			// the task is stored on the stack as we can guarantee that it's out of the system by the end of the call
			FWaitingTask WaitingTask{ TEXT("Waiting Task"), MoveTemp(WaitingTaskBody), ETaskPriority::Default /* doesn't matter*/, EExtendedTaskPriority::Inline};
			WaitingTask.AddPrerequisites(*this);

			if (WaitingTask.TryLaunch())
			{	// was executed inline
				check(WaitingTask.IsCompleted());
			}
			else
			{
				CompletionEvent->Wait();
			}

			// the waiting task will be destroyed leaving this scope, wait for the internal reference to it to be released
			while (WaitingTask.GetRefCount() != 1)
			{
				FPlatformProcess::Yield();
			}
		}

		inline bool FTaskBase::Wait(FTimespan InTimeout)
		{
			TaskTrace::FWaitingScope WaitingScope(GetTraceId());
			TRACE_CPUPROFILER_EVENT_SCOPE(Tasks::Wait);

			FTimeout Timeout{ InTimeout };

			if (TryRetractAndExecute())
			{
				return true;
			}

			if (GetCurrentTask() == this)
			{
				UE_LOG(LogTemp, Fatal, TEXT("A task waiting for itself detected"));
				return true;
			}

			// the event must be alive for the task and this function lifetime, we don't know which one will be finished first as waiting can 
			// time out before the waiting task is completed
			FSharedEventRef CompletionEvent;
			auto WaitingTaskBody = [CompletionEvent] { CompletionEvent->Trigger(); };
			using FWaitingTask = TExecutableTask<decltype(WaitingTaskBody)>;

			TRefCountPtr<FWaitingTask> WaitingTask{ FWaitingTask::Create(TEXT("Waiting Task"), MoveTemp(WaitingTaskBody), ETaskPriority::Default /* doesn't matter*/, EExtendedTaskPriority::Inline), /*bAddRef=*/ false};
			WaitingTask->AddPrerequisites(*this);

			if (WaitingTask->TryLaunch())
			{	// was executed inline
				check(WaitingTask->IsCompleted());
				return true;
			}

			return CompletionEvent->Wait(Timeout.GetRemainingTime());
		}

		// task retraction of multiple tasks. 
		// @return true if all tasks are completed
		template<typename TaskCollectionType>
		bool TryRetractAndExecute(const TaskCollectionType& Tasks)
		{
			bool bResult = true;

			for (auto& Task : Tasks)
			{
				if (Task.IsValid() && !Task.Pimpl->TryRetractAndExecute())
				{
					bResult = false;
				}
			}

			return bResult;
		}

		// task retraction of multiple tasks, with timeout. The timeout is rounded up to any successful task execution, which means that it can time out only in-between individual task
		// retractions.
		// @return true if all tasks are completed
		template<typename TaskCollectionType>
		bool TryRetractAndExecute(const TaskCollectionType& Tasks, FTimespan InTimeout)
		{
			FTimeout Timeout{ InTimeout };
			bool bResult = true;

			for (auto& Task : Tasks)
			{
				if (Task.IsValid() && !Task.Pimpl->TryRetractAndExecute())
				{
					bResult = false;
				}

				if (Timeout)
				{
					return false;
				}
			}

			return bResult;
		}
	}
}