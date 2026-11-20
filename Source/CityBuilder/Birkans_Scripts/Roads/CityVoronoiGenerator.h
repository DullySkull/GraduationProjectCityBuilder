#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ZoneGraphTypes.h"
#include "CityVoronoiGenerator.generated.h"

class UPCGComponent;
class UStaticMesh;
class UMaterialInterface;
class UHierarchicalInstancedStaticMeshComponent;

/** One facade kit. Wall pieces are modelled along local Y, facing +X, with the pivot at the bottom. */
USTRUCT(BlueprintType)
struct FCityBuildingStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TObjectPtr<UStaticMesh> WallMesh;

	/** Optional street-level piece (shops, doors). Empty = WallMesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TObjectPtr<UStaticMesh> GroundFloorMesh;

	/** Optional roof trim placed along the top edge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TObjectPtr<UStaticMesh> TopTrimMesh;

	/** Optional pillar placed on the building corners, stretched to the building height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TObjectPtr<UStaticMesh> CornerMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "100"))
	float ModuleWidth = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "100"))
	float FloorHeight = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "100"))
	float GroundFloorHeight = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "0"))
	float Weight = 1.0f;
};

/**
 * Organic city layout for level designers.
 * Voronoi cells become city blocks, cell edges become streets (road meshes + ZoneGraph lanes for Mass traffic and crowds).
 * Change the settings and press "Generate City". Block contents are dressed by the PCG graph on this actor, which reads
 * the block outline splines tagged Block_Building / Block_Park / Block_Parking.
 */
UCLASS(hidecategories = (Rendering, Physics, HLOD, Collision, Replication, Networking, Input, Cooking, DataLayers))
class CITYBUILDER_API ACityVoronoiGenerator : public AActor
{
	GENERATED_BODY()

public:
	ACityVoronoiGenerator();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "City")
	void GenerateCity();

	UFUNCTION(CallInEditor, Category = "City")
	void NewSeed();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "City")
	void ClearCity();

	/** Rebuild the city whenever a setting changes (live preview). Turn off for very large cities. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City")
	bool bAutoRegenerate = true;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// ---- Layout ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "10000"))
	FVector2D CitySize = FVector2D(200000.0, 200000.0);

	/** Average block diameter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "3000"))
	float BlockSize = 12000.0f;

	/** 0 = square grid, 1 = fully random (organic) blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "0", ClampMax = "1"))
	float Organic = 1.0f;

	/** Lloyd relaxation passes: more = rounder, more even (honeycomb-like) blocks; 0 keeps the flow field's shape. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "0", ClampMax = "10"))
	int32 RelaxIterations = 0;

	/** Tensor-field style layout: blocks and streets follow a smooth curving flow field. 0 = plain random Voronoi. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Flow Field", meta = (ClampMin = "0", ClampMax = "1"))
	float FlowStrength = 1.0f;

	/** Main direction of the flow, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Flow Field")
	float FlowDirection = 20.0f;

	/** How strongly the flow bends (radians). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Flow Field", meta = (ClampMin = "0", ClampMax = "3"))
	float FlowCurl = 0.8f;

	/** Size of the bends (cm). Bigger = long sweeping curves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Flow Field", meta = (ClampMin = "1000"))
	float FlowScale = 60000.0f;

	/** Blocks are this many times longer along the flow than across it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Flow Field", meta = (ClampMin = "1", ClampMax = "4"))
	float FlowElongation = 1.7f;

	/** Share of outskirts streets removed by noise (blocks merge into bigger plots). Downtown stays dense; the network stays connected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Road Thinning", meta = (ClampMin = "0", ClampMax = "1"))
	float RoadThinning = 0.45f;

	/** Inside this fraction of the city radius no streets are removed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Road Thinning", meta = (ClampMin = "0", ClampMax = "1"))
	float ThinningInnerRadius = 0.3f;

	/** How much the streets bend. 0 = straight streets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "0", ClampMax = "1"))
	float StreetCurvature = 0.6f;

	/** Extra small blocks packed around the city centre (dense downtown, wide outskirts). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "0", ClampMax = "2"))
	float DowntownDensity = 0.35f;

	/** Give the city a natural blob-shaped edge instead of filling the whole rectangle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout")
	bool bOrganicOutline = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "0", ClampMax = "1", EditCondition = "bOrganicOutline"))
	float OutlineRoughness = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout")
	int32 Seed = 1337;

	// ---- Districts ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Districts", meta = (ClampMin = "0", ClampMax = "1"))
	float ParkChance = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Districts", meta = (ClampMin = "0", ClampMax = "1"))
	float ParkingChance = 0.10f;

	// ---- Roads ----
	/** Road piece, modelled along +X; it is stretched along every street. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Roads")
	TObjectPtr<UStaticMesh> RoadMesh;

	/** Junction plate, scaled to the intersection size (default: engine cylinder, 100 units wide and tall). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Roads")
	TObjectPtr<UStaticMesh> IntersectionMesh;

	/** Material for the junction plate. Empty = the road mesh's first material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Roads")
	TObjectPtr<UMaterialInterface> IntersectionMaterial;

	/** Full road width including sidewalks; sizes intersections and the gap between blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Roads", meta = (ClampMin = "200"))
	float RoadWidth = 1150.0f;

	/** Flat ground under the whole city (one mesh), so players and pedestrians have something to stand on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Roads")
	TObjectPtr<UStaticMesh> GroundMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Roads")
	TObjectPtr<UMaterialInterface> GroundMaterial;

	/** Sidewalk width between the road edge and the block content. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Roads", meta = (ClampMin = "0"))
	float BlockMargin = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Sidewalks")
	bool bGenerateSidewalks = true;

	/** Sidewalk strip mesh (default: engine cube, scaled per strip). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Sidewalks")
	TObjectPtr<UStaticMesh> SidewalkMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Sidewalks")
	TObjectPtr<UMaterialInterface> SidewalkMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Sidewalks", meta = (ClampMin = "1"))
	float SidewalkHeight = 15.0f;

	/** Walking loops through parks (ZoneGraph crowd lanes) that pedestrians use, with benches placed beside them by PCG. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Parks")
	bool bGenerateParkPaths = true;

	/** How far inside the park edge the walking loop runs (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Parks", meta = (ClampMin = "0"))
	float ParkPathInset = 500.0f;

	/** Bit 22 = "SmartObjects" lanes (MassSmartObjectSettings), so crowd agents look for benches along them. */
	UPROPERTY(EditAnywhere, Category = "City|Parks")
	FZoneGraphTagMask ParkLaneTags = FZoneGraphTagMask(1u << 22);

	// ---- Mass AI lanes ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Lanes")
	bool bGenerateZoneLanes = true;

	UPROPERTY(EditAnywhere, Category = "City|Lanes")
	FZoneLaneProfileRef VehicleLaneProfile;

	UPROPERTY(EditAnywhere, Category = "City|Lanes")
	FZoneLaneProfileRef CrowdLaneProfile;

	/** Distance from the street centre line to the sidewalk lane. */
	UPROPERTY(EditAnywhere, Category = "City|Lanes")
	float SidewalkOffset = 800.0f;

	// Tags match the lane filters in Config/DefaultPlugins.ini (MassTrafficSettings / MassCrowdSettings).
	UPROPERTY(EditAnywhere, Category = "City|Lanes")
	FZoneGraphTagMask RoadTags = FZoneGraphTagMask(1u << 3);

	UPROPERTY(EditAnywhere, Category = "City|Lanes")
	FZoneGraphTagMask IntersectionTags = FZoneGraphTagMask((1u << 3) | (1u << 6) | (1u << 15));

	UPROPERTY(EditAnywhere, Category = "City|Lanes")
	FZoneGraphTagMask CrosswalkTags = FZoneGraphTagMask(1u << 9);

	// ---- Buildings ----
	/** Street-facing buildings along every Block_Building block. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings")
	bool bGenerateBuildings = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings")
	TArray<FCityBuildingStyle> BuildingStyles;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings", meta = (ClampMin = "1"))
	int32 MinFloors = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings", meta = (ClampMin = "1"))
	int32 MaxFloors = 14;

	/** 0 = heights random everywhere, 1 = towers only downtown (city centre), low-rise at the edges. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings", meta = (ClampMin = "0", ClampMax = "1"))
	float DowntownStrength = 0.8f;

	/** Blocks are split into lots no larger than a random size in this range (cm). Smaller = more, narrower buildings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings")
	FVector2D LotSize = FVector2D(3000.0, 6000.0);

	/** Random gap between neighbouring buildings (cm). 0 = buildings touch, like a dense downtown. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings")
	FVector2D LotGap = FVector2D(0.0, 250.0);

	/** Chance that a lot stays open as a small green patch with trees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings", meta = (ClampMin = "0", ClampMax = "1"))
	float GreenLotChance = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings")
	TObjectPtr<UStaticMesh> TreeMesh;

	/** Flat roof cap, scaled to the footprint (default: engine cube). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings")
	TObjectPtr<UStaticMesh> RoofMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings")
	TObjectPtr<UMaterialInterface> RoofMaterial;

	/** Draw the ZoneGraph lane shapes in the editor viewport. Thousands of shapes, so off by default (the baked lanes still work). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Performance")
	bool bShowLanesInEditor = false;

	/**
	 * Lumen sees one invisible box per building instead of every facade piece (tens of thousands).
	 * Turn off only if you need exact per-window bounce light.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Performance")
	bool bUseLumenBuildingProxies = true;

	/** What reflections and bounce light "see" on the proxy boxes. Empty = the first building style's wall material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Performance")
	TObjectPtr<UMaterialInterface> LumenProxyMaterial;

	/** Small building details (trims, pillars) disappear beyond this distance. 0 = never. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Performance", meta = (ClampMin = "0"))
	float DetailCullDistance = 25000.0f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPCGComponent> PCGComponent;

	UPROPERTY()
	TArray<TObjectPtr<UActorComponent>> GeneratedComponents;

private:
	template <class T>
	T* AddGenerated();

	void AddZoneShape(FZoneShapeType Type, const FZoneLaneProfileRef& Profile, FZoneGraphTagMask ShapeTags, TArray<FZoneShapePoint>&& Points);
	void AddRoad(const FVector& Start, const FVector& End);
	void AddIntersection(const FVector& Center, float Radius);
	void AddBlock(const TArray<FVector2d>& Polygon, FName TypeTag);
	void AddBuildings(const TArray<FVector2d>& Block, FRandomStream& Rand);
	void AddBuildingBox(const FCityBuildingStyle& Style, const FVector2d& Origin, const FVector2d& T, const FVector2d& N, int32 WMods, int32 DMods, int32 Floors);
	void AddInstance(UStaticMesh* Mesh, const FTransform& Transform);

	/** One instanced component per mesh, rebuilt on every generate. */
	TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> InstancedMeshes;

	/** Invisible building boxes that only feed Lumen and distance fields. */
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LumenProxies;

	/** Sidewalk strips get their own component so they can share the cube mesh with roofs but use their own material. */
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SidewalkStrips;
};
