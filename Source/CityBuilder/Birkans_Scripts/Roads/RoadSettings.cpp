#include "RoadSettings.h"

#include "PCGContext.h"
#include "PCGComponent.h"
#include "PCGPin.h"
#include "Data/PCGPointData.h"
#include "RoadWFCGenerator.h"

TArray<FPCGPinProperties> URoadSettings::InputPinProperties() const
{
	return {};
}

TArray<FPCGPinProperties> URoadSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Props;
	Props.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Point);
	return Props;
}

FPCGElementPtr URoadSettings::CreateElement() const
{
	return MakeShared<FPCGRoadElement>();
}

bool FPCGRoadElement::ExecuteInternal(FPCGContext* Context) const
{
	if (!Context)
	{
		return true;
	}

	const URoadSettings* Settings = Context->GetInputSettings<URoadSettings>();
	if (!Settings)
	{
		return true;
	}

	UPCGComponent* SourceComp = Context->SourceComponent.Get();
	if (!SourceComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoadSettings: No source PCG component."));
		return true;
	}

	ARoadWFCGenerator* RoadGenerator = nullptr;

	if (AActor* Owner = SourceComp->GetOwner())
	{
		RoadGenerator = Cast<ARoadWFCGenerator>(Owner);
	}

	if (!RoadGenerator && !Settings->RoadGeneratorActor.IsNull())
	{
		RoadGenerator = Settings->RoadGeneratorActor.LoadSynchronous();
	}

	if (!RoadGenerator)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoadSettings: Could not find ARoadWFCGenerator."));
		return true;
	}

	const TArray<FRoadCellResult> AllCells = RoadGenerator->GetRoadResults();

	if (AllCells.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoadSettings: No road results found. Generate roads first."));
		return true;
	}

	UPCGPointData* PointData = NewObject<UPCGPointData>();
	TArray<FPCGPoint>& Points = PointData->GetMutablePoints();
	Points.Reserve(AllCells.Num());

	UPCGMetadata* Metadata = PointData->MutableMetadata();
	auto* MeshAttribute = Metadata->FindOrCreateAttribute<FSoftObjectPath>(
		TEXT("Mesh"), FSoftObjectPath(), false, true);

	int32 RunningSeed = 0;

	for (const FRoadCellResult& Cell : AllCells)
	{
		if (!Cell.bIsRoad)
		{
			continue;
		}

		if (Cell.RoadType != Settings->RoadType)
		{
			continue;
		}

		FPCGPoint Point;
		Point.Transform = Cell.WorldTransform;
		Point.Density = 1.0f;
		Point.Seed = RunningSeed++;

		const PCGMetadataEntryKey EntryKey = Metadata->AddEntry();
		Point.MetadataEntry = EntryKey;

		Points.Add(Point);
	}

	UE_LOG(LogTemp, Warning, TEXT("RoadSettings [%s]: Outputting %d points"),
		*UEnum::GetValueAsString(Settings->RoadType),
		Points.Num());

	FPCGTaggedData& Output = Context->OutputData.TaggedData.Emplace_GetRef();
	Output.Pin = PCGPinConstants::DefaultOutputLabel;
	Output.Data = PointData;

	return true;
}