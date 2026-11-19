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

	/** Lloyd relaxation passes: more = rounder, more even (honeycomb-like) blocks; 0-1 keeps it irregular. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Layout", meta = (ClampMin = "0", ClampMax = "10"))
	int32 RelaxIterations = 1;

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

	/** Extra gap between the road edge and the block content. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Roads", meta = (ClampMin = "0"))
	float BlockMargin = 300.0f;

	// ---- Mass AI lanes ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Lanes")
	bool bGenerateZoneLanes = true;

	UPROPERTY(EditAnywhere, Category = "City|Lanes")
	FZoneLaneProfileRef VehicleLaneProfile;

	UPROPERTY(EditAnywhere, Category = "City|Lanes")
	FZoneLaneProfileRef CrowdLaneProfile;

	/** Distance from the street centre line to the sidewalk lane. */
	UPROPERTY(EditAnywhere, Category = "City|Lanes")
	float SidewalkOffset = 480.0f;

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
	int32 MinFloors = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings", meta = (ClampMin = "1"))
	int32 MaxFloors = 28;

	/** 0 = heights random everywhere, 1 = towers only downtown (city centre), low-rise at the edges. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings", meta = (ClampMin = "0", ClampMax = "1"))
	float DowntownStrength = 0.8f;

	/** Building frontage and depth, in facade modules. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings")
	FIntPoint WidthModules = FIntPoint(3, 6);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings")
	FIntPoint DepthModules = FIntPoint(3, 6);

	/** Flat roof cap, scaled to the footprint (default: engine cube). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings")
	TObjectPtr<UStaticMesh> RoofMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City|Buildings")
	TObjectPtr<UMaterialInterface> RoofMaterial;

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
	void AddInstance(UStaticMesh* Mesh, const FTransform& Transform);

	/** One instanced component per mesh, rebuilt on every generate. */
	TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> InstancedMeshes;
};
