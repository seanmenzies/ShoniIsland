// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ShoniSignificanceManager.generated.h"



enum ESignificanceTag
{
	SIG_Gameplay,
	SIG_Rendering,
	SIG_Audio,
	SIG_Niagara
};

struct FSignificanceObject
{
	TWeakObjectPtr<UObject> Source;
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<USceneComponent> Component;
	const ESignificanceTag SignificanceTag;

	float CachedSignificance = 0.f;

	explicit FSignificanceObject(UObject* InObject, ESignificanceTag INTag)
		: Source(InObject), SignificanceTag(INTag)
	{
		if (AActor* AsActor = Cast<AActor>(InObject))
		{
			Actor = AsActor;
		}
		else if (USceneComponent* AsComp = Cast<USceneComponent>(InObject))
		{
			Component = AsComp;
		}
		checkf(Actor.IsValid() || Component.IsValid(), TEXT("Invalid object type passed to SignificanceManager: %s"), *GetNameSafe(InObject));
	}

	/* Only valid on game thread — safe transform access */
	FTransform GetTransform() const
	{
		if (Actor.IsValid())
		{
			return Actor->GetActorTransform();
		}
		else if (Component.IsValid())
		{
			return Component->GetComponentTransform();
		}
		return FTransform::Identity;
	}

	void SetCachedSignificance(float NewSignificance)
	{
		CachedSignificance = NewSignificance;
	}
};

UCLASS()
class SHONIISLAND_API UShoniSignificanceManager : public UObject
{
	GENERATED_BODY()

public:
	void Init(AActor* Camera);
	static void RegisterObject(UObject* NewObject, ESignificanceTag SignificanceTag);
	static void DeregisterObject(UObject* OldObject);
	static void UpdateContainers();
	static float GetSignificance(UObject* Caller)
	{
		if (ObjectLookupTable.Contains(Caller))
		{
			int32 Index = ObjectLookupTable[Caller];
			if (RegisteredObjects.IsValidIndex(Index))
			{
				return RegisteredObjects[Index].CachedSignificance;
			}
		}		
		return 0.f;
	}

private:
	static bool bIsInited;
	bool bFirstPassComplete;
	UPROPERTY()
	TWeakObjectPtr<AActor> CameraActor;
	FVector CameraLocation_Threadsafe;
	FVector CameraDirection_Threadsafe;
	FTimerHandle TickTimer;
	const float INTERVAL = .2;
	const float CAM_DIST_MAX = 20000.f;
	void CalculateSignificance();

	static TArray<FSignificanceObject> RegisteredObjects;
	static TMap<TObjectKey<UObject>, int32> ObjectLookupTable;

	TArray<TPair<FTransform, float>> AsyncTransformQueue;
	static bool bAsyncOperationInProgress;

	static TArray<TPair<TWeakObjectPtr<UObject>, ESignificanceTag>> ElementsToAdd;
	static TArray<TWeakObjectPtr<UObject>> ElementsToRemove;
	static bool bRequiresUpdate;

	static bool bDebouncePending;
};
