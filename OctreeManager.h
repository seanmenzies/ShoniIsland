// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "OctreeManager.generated.h"

DEFINE_LOG_CATEGORY_STATIC(LogOctree, Log, All);

template<typename T>
class FOctreeNode
{
public:
    FBox Bounds;

    // The objects stored in this node. They are stored only in leaf nodes.
    TMap<UClass*, TArray<T*>> ClassBuckets;

    // Child nodes (8 octants) stored as unique pointers.
    TUniquePtr<FOctreeNode<T>> Children[8];

    // Controls when this node subdivides.
    int32 MaxObjectsPerNode;        // Maximum number of objects this node holds before subdividing.
    int32 MaxDepth;                 // Maximum allowed subdivision depth.
    int32 Depth;                    // Current depth of this node.
    int32 TotalObjectCount = 0;     // Total objects in this node

    FOctreeNode(const FBox& InBounds, int32 InMaxObjects, int32 InDepth, int32 InMaxDepth)
        : Bounds(InBounds)
        , MaxObjectsPerNode(InMaxObjects)
        , MaxDepth(InMaxDepth)
        , Depth(InDepth)
    {
        // children[] default to null.
    }

    // insert an object given its location.
    void Insert(T* Object, const FVector& ObjectLocation, UClass* ClassKey)
    {
        // check if the object is inside this node's bounds.
        if (!Bounds.IsInside(ObjectLocation))
        {
            return;
        }

        // if this node is a leaf (no subdivisions yet)
        if (Children[0] == nullptr)
        {
            // if we have room or we're at maximum depth, we add the object here.
            if (TotalObjectCount < MaxObjectsPerNode || Depth == MaxDepth)
            {
                ClassBuckets.FindOrAdd(ClassKey).Add(Object);
                ++TotalObjectCount;
                
                if (Depth == MaxDepth && TotalObjectCount > MaxObjectsPerNode)
                {
                    UE_LOG(LogOctree, Warning, TEXT("Octree node at max depth (%d) is storing %d objects, exceeding MaxObjectsPerNode (%d)."),
                        Depth, TotalObjectCount, MaxObjectsPerNode);
                }
                return;
            }
        }

        // otherwise, if we have exceeded the max objects and are not at max depth,
        // we need to subdivide (if not already subdivided) and then insert the object into a child.

        // if not subdivided yet, subdivide the node.
        if (Children[0] == nullptr)
        {
            Subdivide();

            // reinsert any existing objects into the children.
            for (auto& Pair : ClassBuckets)
            {
                for (T* ExistingObject : Pair.Value)
                {
                    FVector Loc = ExistingObject->GetActorLocation();
                    for (int32 i = 0; i < 8; ++i)
                    {
                        if (Children[i]->Bounds.IsInside(Loc))
                        {
                            Children[i]->Insert(ExistingObject, Loc, ClassKey);
                            break;
                        }
                    }
                }
            }
            ClassBuckets.Empty();        
        }

        // insert the new object into the appropriate child.
        for (int32 i = 0; i < 8; ++i)
        {
            if (Children[i]->Bounds.IsInside(ObjectLocation))
            {
                Children[i]->Insert(Object, ObjectLocation, ClassKey);
                return;
            }
        }
    }

    bool UpdateObject(T* Object, const FVector& OldLocation, const FVector& NewLocation, UClass* NativeClass)
    {
        // Check if NewLocation is still inside the current node.
        if (Bounds.IsInside(NewLocation))
        {
            // If still inside, we are done.
            return true;
        }
        else
        {
            // Otherwise, remove the object and indicate that it needs to be reinserted.
            Remove(Object, OldLocation, NativeClass);
            return false;
        }
    }

    void Remove(T* Object, const FVector& ObjectLocation, UClass* ClassKey)
    {
        // Check if the object is inside this node
        if (!Bounds.IsInside(ObjectLocation))
        {
            return;
        }
    
        // If this node is a leaf, remove the object if it exists in Objects.
        if (Children[0] == nullptr)
        {
            if (ClassBuckets.Contains(ClassKey))
            {
                ClassBuckets[ClassKey].RemoveSingle(Object);
                --TotalObjectCount;
                // Prune bucket
                if (ClassBuckets[ClassKey].Num() == 0)
                {
                    ClassBuckets.Remove(ClassKey);
                }
            }
            return;
        }
    
        // Otherwise, propagate removal to the appropriate child node.
        for (int32 i = 0; i < 8; i++)
        {
            if (Children[i]->Bounds.IsInside(ObjectLocation))
            {
                Children[i]->Remove(Object, ObjectLocation, ClassKey);
                break;
            }
        }
    }

    static bool Intersects2D(const FBox& Box, const FVector2D& CircleCenter, float CircleRadius)
    {
        // Extract the box's X-Y bounds:
        FVector2D BoxMin(Box.Min.X, Box.Min.Y);
        FVector2D BoxMax(Box.Max.X, Box.Max.Y);

        // Find the closest point in the box to the circle center:
        float ClampedX = FMath::Clamp(CircleCenter.X, BoxMin.X, BoxMax.X);
        float ClampedY = FMath::Clamp(CircleCenter.Y, BoxMin.Y, BoxMax.Y);
        FVector2D ClosestPoint(ClampedX, ClampedY);

        // Check if the distance from this point to the circle center is less than or equal to the circle's radius.
        float DistSquared = FVector2D::DistSquared(CircleCenter, ClosestPoint);
        return DistSquared <= FMath::Square(CircleRadius);
    }

private:
    // Subdivide this node into 8 children (one per octant)
    void Subdivide()
    {
        FVector Center = Bounds.GetCenter();
        // Child extents are half the parent's extent.
        FVector ChildExtent = Bounds.GetExtent() * .5;

        // Create each of the 8 child nodes.
        for (int32 i = 0; i < 8; i++)
        {
            // Determine offset for the child center for each dimension:
            // Each bit of i determines whether to offset positively or negatively along an axis.
            // NB. This is mental. Boolean operation on single bits. ChatGPT figured this out. Super 
            // elegant but I'd never come up with it myself
            FVector Offset;
            Offset.X = (i & 1) ? ChildExtent.X : -ChildExtent.X;
            Offset.Y = (i & 2) ? ChildExtent.Y : -ChildExtent.Y;
            Offset.Z = (i & 4) ? ChildExtent.Z : -ChildExtent.Z;

            FVector ChildCenter = Center + Offset;
            // build a bounding box for this child.
            FBox ChildBounds = FBox::BuildAABB(ChildCenter, ChildExtent);

            Children[i] = MakeUnique<FOctreeNode<T>>(ChildBounds, MaxObjectsPerNode, Depth + 1, MaxDepth);
        }
    }
};

UCLASS()
class SHONIISLAND_API UOctreeManager : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(const FVector& Center, const FVector& Dimensions);
    void AddObjectToOctree(AActor* Object, UClass* NativeCppClass);
    void OnObjectMoved(AActor* Object, const FVector& OldLocation, const FVector& NewLocation, UClass* NativeClass);
    void RemoveObject(AActor* Object, UClass* NativeClass);
    void FindObjectsInRange(const FVector& QueryCenter, float QueryRadius, TArray<AActor*>& OutResults, UClass* FilteredClass);
    void ResetOctree(const FVector& Center, const FVector& Dimensions);

private:
    TUniquePtr<FOctreeNode<AActor>> OctreeRoot;
    // Templated function that queries the octree in 2D (ignoring Z)
    template<typename T>
    void QueryOctree2D(FOctreeNode<T>* Node, const FVector2D& QueryCenter, float QueryRadius, TArray<T*>& OutResults, UClass* FilterClass = nullptr)
    {
        if (!Node)
        {
            return;
        }
    
        // Use our helper to check if the node's bounding box (projected to X-Y) intersects with the query circle.
        if (!FOctreeNode<T>::Intersects2D(Node->Bounds, QueryCenter, QueryRadius))
        {
            return; // No intersection means we can skip this node.
        }
    
        // If this node is a leaf (i.e. it has no children), check each object.
        if (Node->Children[0] == nullptr)
        {
            if (Node->ClassBuckets.Contains(FilterClass))
            {
                for (T* Object : Node->ClassBuckets[FilterClass])
                {
                    // Get the object's location and project it onto X-Y:
                    FVector ObjLocation = Object->GetActorLocation();
                    FVector2D ObjXY(ObjLocation.X, ObjLocation.Y);

                    // Compare the object's distance in 2D to the query center.
                    if (FVector2D::Distance(ObjXY, QueryCenter) <= QueryRadius)
                    {
                        OutResults.Add(Object);
                    }
                }
            }
            else
            {
                for (const auto& Pair : Node->ClassBuckets)
                {
                    for (T* Object : Pair.Value)
                    {
                        // Get the object's location and project it onto X-Y:
                        FVector ObjLocation = Object->GetActorLocation();
                        FVector2D ObjXY(ObjLocation.X, ObjLocation.Y);

                        // Compare the object's distance in 2D to the query center.
                        if (FVector2D::Distance(ObjXY, QueryCenter) <= QueryRadius)
                        {
                            OutResults.Add(Object);
                        }
                    }
                }
            }
        }
        else
        {
            // If not a leaf, recursively query each child.
            for (int32 i = 0; i < 8; ++i)
            {
                QueryOctree2D(Node->Children[i].Get(), QueryCenter, QueryRadius, OutResults, FilterClass);
            }
        }
    }
};
