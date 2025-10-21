// Copyright Epic Games, Inc. All Rights Reserved.

#include "AITask_AsyncMoveTo.h"
#include "UObject/Package.h"
#include "TimerManager.h"
#include "AISystem.h"
#include "AIController.h"
#include "VisualLogger/VisualLogger.h"
#include "AIResources.h"
#include "NavigationSystem.h"
#include "NavigationSystemTypes.h"
#include "../Actors/VillagerController.h"
#include "ShoniCrowdManager.h"
#include "GameplayTasksComponent.h"

DEFINE_LOG_CATEGORY(LogMoveToError);

UAITask_AsyncMoveTo::UAITask_AsyncMoveTo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsPausable = true;
	MoveRequestID = FAIRequestID::InvalidRequest;

	MoveRequest.SetAcceptanceRadius(GET_AI_CONFIG_VAR(AcceptanceRadius));
	MoveRequest.SetReachTestIncludesAgentRadius(GET_AI_CONFIG_VAR(bFinishMoveOnGoalOverlap));
	MoveRequest.SetAllowPartialPath(GET_AI_CONFIG_VAR(bAcceptPartialPaths));
	MoveRequest.SetUsePathfinding(true);

	AddRequiredResource(UAIResource_Movement::StaticClass());
	AddClaimedResource(UAIResource_Movement::StaticClass());
	
	MoveResult = EPathFollowingResult::Invalid;
	bUseContinuousTracking = false;
}

UAITask_AsyncMoveTo* UAITask_AsyncMoveTo::AIMoveTo(AAIController* Controller, FVector InGoalLocation, AActor* InGoalActor,
	float AcceptanceRadius, EAIOptionFlag::Type StopOnOverlap, EAIOptionFlag::Type AcceptPartialPath,
	bool bUsePathfinding, bool bLockAILogic, bool bUseContinuousGoalTracking, EAIOptionFlag::Type ProjectGoalOnNavigation)
{
	UAITask_AsyncMoveTo* MyTask = Controller ? UAITask::NewAITask<UAITask_AsyncMoveTo>(*Controller, EAITaskPriority::High) : nullptr;
	if (MyTask)
	{
		FAIMoveRequest MoveReq;
		if (InGoalActor)
		{
			MoveReq.SetGoalActor(InGoalActor);
		}
		else
		{
			MoveReq.SetGoalLocation(InGoalLocation);
		}

		MoveReq.SetAcceptanceRadius(AcceptanceRadius);
		MoveReq.SetReachTestIncludesAgentRadius(FAISystem::PickAIOption(StopOnOverlap, MoveReq.IsReachTestIncludingAgentRadius()));
		MoveReq.SetAllowPartialPath(FAISystem::PickAIOption(AcceptPartialPath, MoveReq.IsUsingPartialPaths()));
		MoveReq.SetUsePathfinding(bUsePathfinding);
		MoveReq.SetProjectGoalLocation(FAISystem::PickAIOption(ProjectGoalOnNavigation, MoveReq.IsProjectingGoal()));
		if (Controller)
		{
			MoveReq.SetNavigationFilter(Controller->GetDefaultNavigationFilterClass());
		}

		MyTask->SetUp(Controller, MoveReq);
		MyTask->SetContinuousGoalTracking(bUseContinuousGoalTracking);

		if (bLockAILogic)
		{
			MyTask->RequestAILogicLocking();
		}
	}

	return MyTask;
}

void UAITask_AsyncMoveTo::SetUp(AAIController* Controller, const FAIMoveRequest& InMoveRequest)
{
	OwnerController = Controller;
	MoveRequest = InMoveRequest;
}

void UAITask_AsyncMoveTo::SetContinuousGoalTracking(bool bEnable)
{
	bUseContinuousTracking = bEnable;
}

void UAITask_AsyncMoveTo::FinishMoveTask(EPathFollowingResult::Type InResult)
{
	if (MoveRequestID.IsValid())
	{
		UPathFollowingComponent* PFComp = OwnerController ? OwnerController->GetPathFollowingComponent() : nullptr;
		if (PFComp && PFComp->GetStatus() != EPathFollowingStatus::Idle)
		{
			ResetObservers();
			PFComp->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished, MoveRequestID);
		}
	}

	MoveResult = InResult;
	EndTask();

	if (InResult == EPathFollowingResult::Invalid)
	{
		OnRequestFailed.Broadcast();
	}
	else
	{
		OnMoveFinished.Broadcast(InResult, OwnerController);
	}
}

void UAITask_AsyncMoveTo::Activate()
{
	Super::Activate();

	if (MoveRequest.IsMoveToActorRequest())
	{
	  LastGoalLocation = MoveRequest.GetGoalActor()->GetActorLocation();
	}

	UE_CVLOG(bUseContinuousTracking, GetGameplayTasksComponent(), LogGameplayTasks, Log, TEXT("Continuous goal tracking requested, moving to: %s"),
		MoveRequest.IsMoveToActorRequest() ? TEXT("actor => looping successful moves!") : TEXT("location => will NOT loop"));

	MoveRequestID = FAIRequestID::InvalidRequest;
	ConditionalPerformMove();
}

void UAITask_AsyncMoveTo::ConditionalPerformMove()
{
	if (MoveRequest.IsUsingPathfinding() && OwnerController && OwnerController->ShouldPostponePathUpdates())
	{
		UE_VLOG(GetGameplayTasksComponent(), LogGameplayTasks, Log, TEXT("%s> can't path right now, waiting..."), *GetName());
		OwnerController->GetWorldTimerManager().SetTimer(MoveRetryTimerHandle, this, &UAITask_AsyncMoveTo::ConditionalPerformMove, 0.2f, false);
	}
	else
	{
		MoveRetryTimerHandle.Invalidate();
		PerformMove();
	}
}

void UAITask_AsyncMoveTo::PerformMove()
{
	UPathFollowingComponent* PFComp = OwnerController ? OwnerController->GetPathFollowingComponent() : nullptr;
	if (PFComp == nullptr)
	{
		FinishMoveTask(EPathFollowingResult::Invalid);
		return;
	}

	ResetObservers();
	ResetTimers();

    FPathFindingQuery NavQuery;
    if (!OwnerController->BuildPathfindingQuery(MoveRequest, NavQuery))
    {
        FinishMoveTask(EPathFollowingResult::Invalid);
        return;
    }

    UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (!NavSys)
    {
        FinishMoveTask(EPathFollowingResult::Invalid);
        return;
    }

	// block RVO from interfering with pathfinding
	UShoniCrowdManager::SetAsyncInFlight(OwnerController->GetWorld(), true);
    // 3) Kick off async
    const uint32 QueryID = NavSys->FindPathAsync(
        NavQuery.NavAgentProperties,
        NavQuery,
        FNavPathQueryDelegate::CreateUObject(this, &UAITask_AsyncMoveTo::OnAsynPathResult),
        EPathFindingMode::Regular
    );
}

void UAITask_AsyncMoveTo::OnAsynPathResult(uint32 QueryID, ENavigationQueryResult::Type Result, FNavPathSharedPtr _Path)
{
	// switch RVO back on
	UShoniCrowdManager::SetAsyncInFlight(OwnerController->GetWorld(), false);
	// 1) log errors and abort if any found
    switch (Result)
    {
    case ENavigationQueryResult::Error:
        UE_LOG(LogMoveToError, Warning, TEXT("AsyncMoveTo: pathfinding error for request %u"), QueryID);
        FinishMoveTask(EPathFollowingResult::Invalid);
        return;
    case ENavigationQueryResult::Fail:
        UE_LOG(LogMoveToError, Warning, TEXT("AsyncMoveTo: no path found to %s"), *MoveRequest.ToString());
        FinishMoveTask(EPathFollowingResult::Aborted);
        return;
    case ENavigationQueryResult::Success:
		// just break out of the switch and proceed
        break;
    default:
        UE_LOG(LogMoveToError, Warning, TEXT("AsyncMoveTo: unexpected result %d"), (int)Result);
        FinishMoveTask(EPathFollowingResult::Invalid);
        return;
    }

    // 2) Success -> hand off to PathFollowingComponent
    UPathFollowingComponent* PFComp = OwnerController->GetPathFollowingComponent();
    MoveRequestID = PFComp->RequestMove(MoveRequest, _Path);

    // 3) Hook up the same delegates you had before
    PathFinishDelegateHandle = PFComp->OnRequestFinished.AddUObject(this, &UAITask_AsyncMoveTo::OnRequestFinished);
    SetObservedPath(_Path);
}

void UAITask_AsyncMoveTo::Pause()
{
	if (OwnerController && MoveRequestID.IsValid())
	{
		OwnerController->PauseMove(MoveRequestID);
	}

	ResetTimers();
	Super::Pause();
}

void UAITask_AsyncMoveTo::Resume()
{
	Super::Resume();

	if (!MoveRequestID.IsValid() || (OwnerController && !OwnerController->ResumeMove(MoveRequestID)))
	{
		UE_CVLOG(MoveRequestID.IsValid(), GetGameplayTasksComponent(), LogGameplayTasks, Log, TEXT("%s> Resume move failed, starting new one."), *GetName());
		ConditionalPerformMove();
	}
}

void UAITask_AsyncMoveTo::SetObservedPath(FNavPathSharedPtr InPath)
{
	if (PathUpdateDelegateHandle.IsValid() && Path.IsValid())
	{
		Path->RemoveObserver(PathUpdateDelegateHandle);
	}

	PathUpdateDelegateHandle.Reset();
	
	Path = InPath;
	if (Path.IsValid())
	{
		// disable auto repaths, it will be handled by move task to include ShouldPostponePathUpdates condition
		Path->EnableRecalculationOnInvalidation(false);
		PathUpdateDelegateHandle = Path->AddObserver(FNavigationPath::FPathObserverDelegate::FDelegate::CreateUObject(this, &UAITask_AsyncMoveTo::OnPathEvent));
	}
}

void UAITask_AsyncMoveTo::ResetObservers()
{
	if (Path.IsValid())
	{
		Path->DisableGoalActorObservation();
	}

	if (PathFinishDelegateHandle.IsValid())
	{
		UPathFollowingComponent* PFComp = OwnerController ? OwnerController->GetPathFollowingComponent() : nullptr;
		if (PFComp)
		{
			PFComp->OnRequestFinished.Remove(PathFinishDelegateHandle);
		}

		PathFinishDelegateHandle.Reset();
	}

	if (PathUpdateDelegateHandle.IsValid())
	{
		if (Path.IsValid())
		{
			Path->RemoveObserver(PathUpdateDelegateHandle);
		}

		PathUpdateDelegateHandle.Reset();
	}
}

void UAITask_AsyncMoveTo::ResetTimers()
{
	if (OwnerController)
	{
		// Remove all timers including the ones that might have been set with SetTimerForNextTick 
		OwnerController->GetWorldTimerManager().ClearAllTimersForObject(this);
	}
	MoveRetryTimerHandle.Invalidate();
	PathRetryTimerHandle.Invalidate();
}

void UAITask_AsyncMoveTo::OnDestroy(bool bInOwnerFinished)
{
	Super::OnDestroy(bInOwnerFinished);
	
	ResetObservers();
	ResetTimers();

	if (MoveRequestID.IsValid())
	{
		UPathFollowingComponent* PFComp = OwnerController ? OwnerController->GetPathFollowingComponent() : nullptr;
		if (PFComp && PFComp->GetStatus() != EPathFollowingStatus::Idle)
		{
			PFComp->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished, MoveRequestID);
		}
	}

	// clear the shared pointer now to make sure other systems
	// don't think this path is still being used
	Path = nullptr;
}

void UAITask_AsyncMoveTo::OnRequestFinished(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	if (RequestID == MoveRequestID)
	{
		if (Result.HasFlag(FPathFollowingResultFlags::UserAbort) && Result.HasFlag(FPathFollowingResultFlags::NewRequest) && !Result.HasFlag(FPathFollowingResultFlags::ForcedScript))
		{
			UE_VLOG(GetGameplayTasksComponent(), LogGameplayTasks, Log, TEXT("%s> ignoring OnRequestFinished, move was aborted by new request"), *GetName());
		}
		else
		{
			// reset request Id, FinishMoveTask doesn't need to update path following's state
			MoveRequestID = FAIRequestID::InvalidRequest;

			if (bUseContinuousTracking && MoveRequest.IsMoveToActorRequest() && Result.IsSuccess())
			{
				FVector CurrGoal = MoveRequest.GetGoalActor()->GetActorLocation();
				const float Tolerance = MoveRequest.GetAcceptanceRadius();
				if (!CurrGoal.Equals(LastGoalLocation, Tolerance))
				{
					LastGoalLocation = CurrGoal;
					UE_VLOG(GetGameplayTasksComponent(), LogGameplayTasks, Log, TEXT("%s> received OnRequestFinished and goal tracking is active! Moving again in next tick"), *GetName());
					GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::PerformMove);
				}
				else
				{
					FinishMoveTask(Result.Code);
				}
			}
			else
			{
				FinishMoveTask(Result.Code);
			}
		}
	}
	else if (IsActive())
	{
		UE_VLOG(GetGameplayTasksComponent(), LogGameplayTasks, Warning, TEXT("%s> received OnRequestFinished with not matching RequestID!"), *GetName());
	}
}

void UAITask_AsyncMoveTo::OnPathEvent(FNavigationPath* InPath, ENavPathEvent::Type Event)
{
	const static UEnum* NavPathEventEnum = StaticEnum<ENavPathEvent::Type>();
	UE_VLOG(GetGameplayTasksComponent(), LogGameplayTasks, Log, TEXT("%s> Path event: %s"), *GetName(), *NavPathEventEnum->GetNameStringByValue(Event));

	switch (Event)
	{
	case ENavPathEvent::NewPath:
	case ENavPathEvent::UpdatedDueToGoalMoved:
		if (bUseContinuousTracking && MoveRequest.IsMoveToActorRequest())
		{
			GetWorld()->GetTimerManager().ClearTimer(PathRetryTimerHandle);
			PerformMove();
		}
	case ENavPathEvent::UpdatedDueToNavigationChanged:
		if (InPath && InPath->IsPartial() && !MoveRequest.IsUsingPartialPaths())
		{
			UE_VLOG(GetGameplayTasksComponent(), LogGameplayTasks, Log, TEXT(">> partial path is not allowed, aborting"));
			UPathFollowingComponent::LogPathHelper(OwnerController, InPath, MoveRequest.GetGoalActor());
			FinishMoveTask(EPathFollowingResult::Aborted);
		}
#if ENABLE_VISUAL_LOG
		else if (!IsActive())
		{
			UPathFollowingComponent::LogPathHelper(OwnerController, InPath, MoveRequest.GetGoalActor());
		}
#endif // ENABLE_VISUAL_LOG
		break;

	case ENavPathEvent::Invalidated:
		ConditionalUpdatePath();
		break;

	case ENavPathEvent::Cleared:
	case ENavPathEvent::RePathFailed:
		UE_VLOG(GetGameplayTasksComponent(), LogGameplayTasks, Log, TEXT(">> no path, aborting!"));
		FinishMoveTask(EPathFollowingResult::Aborted);
		break;

	case ENavPathEvent::MetaPathUpdate:
	default:
		break;
	}
}

void UAITask_AsyncMoveTo::ConditionalUpdatePath()
{
	// mark this path as waiting for repath so that PathFollowingComponent doesn't abort the move while we 
	// micro manage repathing moment
	// note that this flag fill get cleared upon repathing end
	if (Path.IsValid())
	{
		Path->SetManualRepathWaiting(true);
	}

	if (MoveRequest.IsUsingPathfinding() && OwnerController && OwnerController->ShouldPostponePathUpdates())
	{
		UE_VLOG(GetGameplayTasksComponent(), LogGameplayTasks, Log, TEXT("%s> can't path right now, waiting..."), *GetName());
		OwnerController->GetWorldTimerManager().SetTimer(PathRetryTimerHandle, this, &UAITask_AsyncMoveTo::ConditionalUpdatePath, 0.2f, false);
	}
	else
	{
		PathRetryTimerHandle.Invalidate();
		PerformMove();
	}
}

