// Fill out your copyright notice in the Description page of Project Settings.


#include "OctreeManager.h"

void UOctreeManager::Initialize(const FVector& Center, const FVector& Dimensions)
{
    FVector Extents = Dimensions;
    FBox WorldBounds = FBox::BuildAABB(Center, Extents);
    // Configure parameters as needed.
    int32 MaxObjectsPerNode = 10;
    int32 MaxDepth = 3;
    OctreeRoot = MakeUnique<FOctreeNode<AActor>>(WorldBounds, MaxObjectsPerNode, 0, MaxDepth);
}

void UOctreeManager::AddObjectToOctree(AActor* Object, UClass* NativeCppClass)
{
    if (Object && OctreeRoot.IsValid())
    {
        FVector Location = Object->GetActorLocation();
        if (OctreeRoot->Bounds.IsInsideXY(Location))
        {
            OctreeRoot->Insert(Object, Location, NativeCppClass);
        }
    }
}

void UOctreeManager::OnObjectMoved(AActor* Object, const FVector& OldLocation, const FVector& NewLocation, UClass* NativeClass)
{
    if (!Object) return;

    // check whether updated object is in the same node; if not, it will be removed inside the update
    // then reinserted
    if (!OctreeRoot->UpdateObject(Object, OldLocation, NewLocation, NativeClass))
    {
        // Insert at new location.
        OctreeRoot->Insert(Object, Object->GetActorLocation(), NativeClass);
    }
}

void UOctreeManager::RemoveObject(AActor* Object, UClass* NativeClass)
{
    if (!Object) return;

    OctreeRoot->Remove(Object, Object->GetActorLocation(), NativeClass);
}

void UOctreeManager::FindObjectsInRange(const FVector& QueryCenter, float QueryRadius, TArray<AActor*>& OutResults, UClass* FilteredClass)
{
    // remove Z axis
    const FVector2D QueryCentre2D(QueryCenter.X, QueryCenter.Y);
    // populate array
    QueryOctree2D(OctreeRoot.Get(), QueryCentre2D, QueryRadius, OutResults, FilteredClass);
}

void UOctreeManager::ResetOctree(const FVector& Center, const FVector& Dimensions)
{
    // recompute the new world bounds:
    FBox WorldBounds = FBox::BuildAABB(Center, Dimensions);
    
    // reset the octree:
    OctreeRoot.Reset(); // This will delete the old octree.
    
    // create a new octree root with your desired parameters.
    int32 MaxObjectsPerNode = 10;
    int32 MaxDepth = 10;
    OctreeRoot = MakeUnique<FOctreeNode<AActor>>(WorldBounds, MaxObjectsPerNode, 0, MaxDepth);
}
