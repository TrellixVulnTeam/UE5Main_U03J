// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FPCGPoint;
struct FPCGContext;

namespace FPCGAsync
{
	/** 
	* Helper to do simple point processing loops
	* @param Context - The context containing the information about how many tasks to launch
	* @param NumIterations - The number of calls that will be done to the provided function, also an upper bound on the number of points generated
	* @param OutPoints - The array in which the results will be written to. Note that the array will be cleared before execution
	* @param PointFunc - A function that has the index [0; NumIterations] and has to write to the point & return true when the current call generates a point
	*/
	void AsyncPointProcessing(FPCGContext* Context, int32 NumIterations, TArray<FPCGPoint>& OutPoints, const TFunction<bool(int32, FPCGPoint&)>& PointFunc);

	/** 
	* Helper to do more general 1:1 point processing loops
	* @param NumAvailableTasks - The upper bound on the number of async tasks we'll start
	* @param MinIterationsPerTask - The lower bound on the number of iterations per task we'll dispatch
	* @param NumIterations - The number of calls that will be done to the provided function, also an upper bound on the number of points generated
 	* @param OutPoints - The array in which the results will be written to. Note that the array will be cleared before execution
	* @param PointFunc - A function that has the index [0; NumIterations] and has to write to the point & return true when the current call generates a point
	*/
	void AsyncPointProcessing(int32 NumAvailableTasks, int32 MinIterationsPerTask, int32 NumIterations, TArray<FPCGPoint>& OutPoints, const TFunction<bool(int32, FPCGPoint&)>& PointFunc);

	/**
	* Helper to do simple point filtering loops
	* @param Context - The context containing the information about how many tasks to launch
	* @param NumIterations - The number of calls that will be done to the provided function, also an upper bound on the number of points generated
	* @param InFilterPoints - The array in which the in-filter results will be written to. Note that the array will be cleared before execution
	* @param OutFilterPoints - The array in which the out-filter results will be written to. Note that the array will be cleared before execution
	* @param PointFunc - A function that has the index [0; NumIterations] and has to write to the point & return true when the current call generates a point
	*/
	void AsyncPointFilterProcessing(FPCGContext* Context, int32 NumIterations, TArray<FPCGPoint>& InFilterPoints, TArray<FPCGPoint>& OutFilterPoints, const TFunction<bool(int32, FPCGPoint&, FPCGPoint&)>& PointFunc);

	/**
	* Helper to do more general 1:1 point filtering loops
	* @param NumAvailableTasks - The upper bound on the number of async tasks we'll start
	* @param MinIterationsPerTask - The lower bound on the number of iterations per task we'll dispatch
	* @param NumIterations - The number of calls that will be done to the provided function, also an upper bound on the number of points generated
	* @param InFilterPoints - The array in which the in-filter results will be written to. Note that the array will be cleared before execution
	* @param OutFilterPoints - The array in which the out-filter results will be written to. Note that the array will be cleared before execution
	* @param PointFunc - A function that has the index [0; NumIterations] and has to write to the point & return true when the current call generates a point
	*/
	void AsyncPointFilterProcessing(int32 NumAvailableTasks, int32 MinIterationsPerTask, int32 NumIterations, TArray<FPCGPoint>& InFilterPoints, TArray<FPCGPoint>& OutFilterPoints, const TFunction<bool(int32, FPCGPoint&, FPCGPoint&)>& PointFunc);
}