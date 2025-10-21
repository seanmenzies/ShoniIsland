## SELECTED EXAMPLES OF LOW-LEVEL SYSTEMS FROM SHONI ISLAND

# AITask_AsyncMoveTo
Mostly kept Unreal's implementation but includes a pseudo co-routine that uses FindPathAsync and adds a callback when pathfinding result returned (see PerformMove())
NB. This implementation requires a workaround if using in conjunction with RVO as Unreal's async find path is a bit hacky (I overrode the CrowdManager to avoid hitting race conditions)

# OctreeManager
Simple octree that self-organises into 2D squares containing max n objects to permit low-cost querying in a large map. Recent changes also sort the objects into class buckets to be able to filter queries by class.

# SignificanceManager
Async significance manager currently based exclusively on distance but is extendible to other factors. Throttles object adds to avoid costly initialisation and updates every n seconds. Containers are all recycled and size maintained to avoid excessive memory re-allocation.
