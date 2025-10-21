// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "AITypes.h"
#include "Navigation/PathFollowingComponent.h"
#include "Tasks/AITask.h"
#include "Tasks/AITask_MoveTo.h"
#include "AITask_AsyncMoveTo.generated.h"

class AAIController;

DECLARE_LOG_CATEGORY_EXTERN(LogMoveToError, Log, All);

UCLASS(MinimalAPI)
class UAITask_AsyncMoveTo : public UAITask
{
	GENERATED_BODY()

public:
	SHONIISLAND_API UAITask_AsyncMoveTo(const FObjectInitializer& ObjectInitializer);

	/** tries to start move request and handles retry timer */
	SHONIISLAND_API void ConditionalPerformMove();

	/** prepare move task for activation */
	SHONIISLAND_API void SetUp(AAIController* Controller, const FAIMoveRequest& InMoveRequest);

	EPathFollowingResult::Type GetMoveResult() const { return MoveResult; }
	bool WasMoveSuccessful() const { return MoveResult == EPathFollowingResult::Success; }
	bool WasMovePartial() const { return Path.IsValid() && Path->IsPartial(); }

	UFUNCTION(BlueprintCallable, Category = "AI|Tasks", meta = (AdvancedDisplay = "AcceptanceRadius,StopOnOverlap,AcceptPartialPath,bUsePathfinding,bUseContinuousGoalTracking,ProjectGoalOnNavigation", DefaultToSelf = "Controller", BlueprintInternalUseOnly = "TRUE", DisplayName = "Async Move To"))
	static SHONIISLAND_API UAITask_AsyncMoveTo* AIMoveTo(AAIController* Controller, FVector GoalLocation, AActor* GoalActor = nullptr,
		float AcceptanceRadius = -1.f, EAIOptionFlag::Type StopOnOverlap = EAIOptionFlag::Default, EAIOptionFlag::Type AcceptPartialPath = EAIOptionFlag::Default,
		bool bUsePathfinding = true, bool bLockAILogic = true, bool bUseContinuousGoalTracking = false, EAIOptionFlag::Type ProjectGoalOnNavigation = EAIOptionFlag::Default);

	/** Allows custom move request tweaking. Note that all MoveRequest need to 
	 *	be performed before PerformMove is called. */
	FAIMoveRequest& GetMoveRequestRef() { return MoveRequest; }

	UPROPERTY(BlueprintAssignable)
	FMoveTaskCompletedSignature OnMoveFinished;

	UPROPERTY(BlueprintAssignable)
	FGenericGameplayTaskDelegate OnRequestFailed;

	/** Switch task into continuous tracking mode: keep restarting move toward goal actor. Only pathfinding failure or external cancel will be able to stop this task. */
	SHONIISLAND_API void SetContinuousGoalTracking(bool bEnable);

protected:

	/** parameters of move request */
	UPROPERTY()
	FAIMoveRequest MoveRequest;

	/** handle of path following's OnMoveFinished delegate */
	FDelegateHandle PathFinishDelegateHandle;

	/** handle of path's update event delegate */
	FDelegateHandle PathUpdateDelegateHandle;

	/** handle of active ConditionalPerformMove timer  */
	FTimerHandle MoveRetryTimerHandle;

	/** handle of active ConditionalUpdatePath timer */
	FTimerHandle PathRetryTimerHandle;

	/** request ID of path following's request */
	FAIRequestID MoveRequestID;

	/** currently followed path */
	FNavPathSharedPtr Path;

	TEnumAsByte<EPathFollowingResult::Type> MoveResult;
	uint8 bUseContinuousTracking : 1;

	SHONIISLAND_API virtual void Activate() override;
	SHONIISLAND_API virtual void OnDestroy(bool bOwnerFinished) override;

	SHONIISLAND_API virtual void Pause() override;
	SHONIISLAND_API virtual void Resume() override;

	/** finish task */
	SHONIISLAND_API void FinishMoveTask(EPathFollowingResult::Type InResult);

	/** stores path and starts observing its events */
	SHONIISLAND_API void SetObservedPath(FNavPathSharedPtr InPath);

	/** remove all delegates */
	SHONIISLAND_API virtual void ResetObservers();

	/** remove all timers */
	SHONIISLAND_API virtual void ResetTimers();

	/** tries to update invalidated path and handles retry timer */
	SHONIISLAND_API void ConditionalUpdatePath();

	/** start move request */
	SHONIISLAND_API virtual void PerformMove();

	void OnAsynPathResult(uint32 QueryID, ENavigationQueryResult::Type Result, FNavPathSharedPtr Path);

	/** event from followed path */
	SHONIISLAND_API virtual void OnPathEvent(FNavigationPath* InPath, ENavPathEvent::Type Event);

	/** event from path following */
	SHONIISLAND_API virtual void OnRequestFinished(FAIRequestID RequestID, const FPathFollowingResult& Result);

	FVector LastGoalLocation = FAISystem::InvalidLocation;
};
