// Copyright Epic Games, Inc. All Rights Reserved.

#include "CityBuildToolEditorModeToolkit.h"
#include "CityBuildToolEditorMode.h"
#include "Engine/Selection.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "CityBuildToolEditorModeToolkit"

FCityBuildToolEditorModeToolkit::FCityBuildToolEditorModeToolkit()
{
}

void FCityBuildToolEditorModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode)
{
	FModeToolkit::Init(InitToolkitHost, InOwningMode);
}

void FCityBuildToolEditorModeToolkit::GetToolPaletteNames(TArray<FName>& PaletteNames) const
{
	PaletteNames.Add(NAME_Default);
}


FName FCityBuildToolEditorModeToolkit::GetToolkitFName() const
{
	return FName("CityBuildToolEditorMode");
}

FText FCityBuildToolEditorModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("DisplayName", "CityBuildToolEditorMode Toolkit");
}

#undef LOCTEXT_NAMESPACE
