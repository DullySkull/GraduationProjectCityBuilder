#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "WFCData.h"
#include "RoadSettings.generated.h"

class ARoadWFCGenerator;

UCLASS(BlueprintType, ClassGroup=(Procedural))
class CITYBUILDER_API URoadSettings : public UPCGSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road WFC")
	ERoadType RoadType = ERoadType::Straight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road WFC")
	TSoftObjectPtr<ARoadWFCGenerator> RoadGeneratorActor;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("RoadWFCPoints")); }
	virtual FText GetDefaultNodeTitle() const override { return FText::FromString(TEXT("Road WFC Points")); }
	virtual FText GetNodeTooltipText() const override
	{
		return FText::FromString(TEXT("Outputs PCG points from the RoadWFCGenerator for one selected ERoadType."));
	}
	virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
#endif

protected:
	virtual TArray<FPCGPinProperties> InputPinProperties() const override;
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
};

class CITYBUILDER_API FPCGRoadElement : public IPCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
};