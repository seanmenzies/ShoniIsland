// Fill out your copyright notice in the Description page of Project Settings.

#include "ShoniSignificanceManager.h"
#include "../../Interfaces/SignificanceInterface.h"
#include "../../AI/Actors/Villager.h"

TArray<FSignificanceObject> UShoniSignificanceManager::RegisteredObjects = {};
bool UShoniSignificanceManager::bAsyncOperationInProgress = false;
bool UShoniSignificanceManager::bRequiresUpdate = false;
bool UShoniSignificanceManager::bDebouncePending = false;
bool UShoniSignificanceManager::bIsInited = false;
TArray<TPair<TWeakObjectPtr<UObject>, ESignificanceTag>> UShoniSignificanceManager::ElementsToAdd = {};
TArray<TWeakObjectPtr<UObject>> UShoniSignificanceManager::ElementsToRemove = {};
TMap<TObjectKey<UObject>, int32> UShoniSignificanceManager::ObjectLookupTable = {};

DEFINE_LOG_CATEGORY_STATIC(LogShoniSignificance, Log, All);

void UShoniSignificanceManager::Init(AActor* Camera)
{
	if (Camera && Camera->GetWorld())
	{
		bAsyncOperationInProgress = false;
		CameraActor = Camera;
		bIsInited = true;
		Camera->GetWorld()->GetTimerManager().SetTimer(TickTimer, this, &UShoniSignificanceManager::CalculateSignificance, INTERVAL, true, 0.f);
		UE_LOG(LogShoniSignificance, Verbose, TEXT("Significance manager successfully initialised"));
	}
}

void UShoniSignificanceManager::RegisterObject(UObject* NewObject, ESignificanceTag SignificanceTag)
{
	if (!NewObject || !(NewObject->IsA(AActor::StaticClass()) || (NewObject->IsA(USceneComponent::StaticClass())))) return;

	ElementsToAdd.Add(TPair<TWeakObjectPtr<UObject>, ESignificanceTag>(MakeWeakObjectPtr<UObject>(NewObject), SignificanceTag));
	bRequiresUpdate = true;

	if (!bDebouncePending)
	{
		if (NewObject->GetWorld()) NewObject->GetWorld()->GetTimerManager().SetTimerForNextTick(&UShoniSignificanceManager::UpdateContainers);
		bDebouncePending = true;
	}
}

void UShoniSignificanceManager::DeregisterObject(UObject* OldObject)
{
	if (!OldObject || !OldObject->GetWorld()) return;

	ElementsToRemove.AddUnique(OldObject);
	bRequiresUpdate = true;

	if (!bDebouncePending)
	{
		if (OldObject->GetWorld()) OldObject->GetWorld()->GetTimerManager().SetTimerForNextTick(&UShoniSignificanceManager::UpdateContainers);
		bDebouncePending = true;
	}
}

void UShoniSignificanceManager::UpdateContainers()
{
	if (ElementsToAdd.Num() || ElementsToRemove.Num())
	{
		// take care of memory allocation first
		RegisteredObjects.Reserve(RegisteredObjects.Num() + ElementsToAdd.Num() - ElementsToRemove.Num());
		// remove first to ensure index order integrity
		for (auto& Obj : ElementsToRemove)
		{
			if (Obj.IsValid() && ObjectLookupTable.Contains(Obj.Get()))
			{
				const int32 Idx = ObjectLookupTable[Obj.Get()];
				if (RegisteredObjects.IsValidIndex(Idx) && RegisteredObjects[Idx].Source.IsValid())
				{
					ObjectLookupTable.Remove(RegisteredObjects[Idx].Source.Get());
					RegisteredObjects.RemoveAtSwap(Idx, 1, false);
				}
			}
		}
		for (auto Obj : ElementsToAdd)
		{
			if (Obj.Key.IsValid())
			{
				auto NewSigObj = FSignificanceObject(Obj.Key.Get(), Obj.Value);
				int32 NewIdx = RegisteredObjects.Add(NewSigObj);
				ObjectLookupTable.Add(Obj.Key.Get(), NewIdx);
			}
		}
	}
	UE_LOG(LogShoniSignificance, Verbose, TEXT("Elements added: %i"), ElementsToAdd.Num());
	UE_LOG(LogShoniSignificance, Verbose, TEXT("Elements removed: %i"), ElementsToRemove.Num());
	UE_LOG(LogShoniSignificance, Verbose, TEXT("Total elements managed: %i"), RegisteredObjects.Num());

	ElementsToAdd.Empty();
	ElementsToRemove.Empty();
	bRequiresUpdate = false;
	bDebouncePending = false;
}

void UShoniSignificanceManager::CalculateSignificance()
{
	if (!bIsInited || bAsyncOperationInProgress || !CameraActor.IsValid()) return;
	// populate cache
	AsyncTransformQueue.Empty();
	for (const auto& SigOb : RegisteredObjects)
	{
		AsyncTransformQueue.Add(TPair<FTransform, float>(SigOb.GetTransform(), 0.f));
	}
	CameraLocation_Threadsafe = CameraActor.Get()->GetActorLocation();
	CameraDirection_Threadsafe = CameraActor.Get()->GetActorForwardVector();
	if (!AsyncTransformQueue.IsEmpty())
	{
		bAsyncOperationInProgress = true;
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this]()
			{
				int32 NumSignificant = 0;
				for (auto& SigObj : AsyncTransformQueue)
				{
					const FVector ObjLocation = SigObj.Key.GetLocation();
					const float Dist = FVector::Dist(ObjLocation, CameraLocation_Threadsafe);

					const FVector DirToObj = (ObjLocation - CameraLocation_Threadsafe).GetSafeNormal();

					const float FacingDot = FVector::DotProduct(CameraDirection_Threadsafe, DirToObj);

					const float DistanceScale = FMath::Lerp(0.5f, 1.0f, (FacingDot + 1.f) * 0.5f);
					const float ScaledMaxDist = CAM_DIST_MAX * DistanceScale;

					// Linear falloff significance
					SigObj.Value = (Dist <= ScaledMaxDist && ScaledMaxDist > 0.f) ? 1.f - (Dist / ScaledMaxDist) : 0.f;

					if (SigObj.Value > 0.f) ++NumSignificant;
				}
				AsyncTask(ENamedThreads::GameThread, [this, NumSignificant]()
					{
						if (!IsValid(this)) return;
						for (int32 i = 0; i < RegisteredObjects.Num(); ++i)
						{
							// call all objects on first pass to ensure correct state initialised
							if (!bFirstPassComplete)
							{
								if (AsyncTransformQueue[i].Value > 0.f)
								{
									if (auto Int_Obj = Cast<ISignificanceInterface>(RegisteredObjects[i].Source))
									{
										Int_Obj->OnSignificanceChanged(true);
									}
								}
								else
								{
									if (auto Int_Obj = Cast<ISignificanceInterface>(RegisteredObjects[i].Source))
									{
										Int_Obj->OnSignificanceChanged(false);
									}
								}
							}
							else
							{
								if (!RegisteredObjects[i].Source.IsValid()) continue;

								// particles
								if (RegisteredObjects[i].SignificanceTag == SIG_Niagara)
								{
									// spirit VFX handled by player controller
								}
								// game actors
								else
								{
									if (RegisteredObjects[i].CachedSignificance <= 0.f && AsyncTransformQueue[i].Value > 0.f)
									{
										if (auto Int_Obj = Cast<ISignificanceInterface>(RegisteredObjects[i].Source))
										{
											Int_Obj->OnSignificanceChanged(true);
										}
									}
									else if (RegisteredObjects[i].CachedSignificance > 0.f && AsyncTransformQueue[i].Value <= 0.f)
									{
										if (auto Int_Obj = Cast<ISignificanceInterface>(RegisteredObjects[i].Source))
										{
											Int_Obj->OnSignificanceChanged(false);
										}
									}
									else if (AsyncTransformQueue[i].Value > 0.f && RegisteredObjects[i].Source->IsA(AVillager::StaticClass()))
									{
										if (auto Int_Obj = Cast<ISignificanceInterface>(RegisteredObjects[i].Source))
										{
											Int_Obj->OnSignificanceValueChanged(RegisteredObjects[i].CachedSignificance, AsyncTransformQueue[i].Value);
										}
									}
								}
							}
							RegisteredObjects[i].SetCachedSignificance(AsyncTransformQueue[i].Value);
						}
						AsyncTransformQueue.Empty();
						// if we have anny elements added and a timer hasn't already been set for an update, go ahead and update
						if (bRequiresUpdate && !bDebouncePending) UpdateContainers();
						bAsyncOperationInProgress = false;
						UE_LOG(LogShoniSignificance, Verbose, TEXT("Significance updated. %i objects significant"), NumSignificant);
						bFirstPassComplete = true;
					});
			});
	}
}
