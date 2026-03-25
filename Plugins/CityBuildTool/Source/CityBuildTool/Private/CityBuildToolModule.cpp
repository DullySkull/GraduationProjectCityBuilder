// Copyright Epic Games, Inc. All Rights Reserved.

#include "CityBuildToolModule.h"
#include "CityBuildToolEditorModeCommands.h"

#define LOCTEXT_NAMESPACE "CityBuildToolModule"

void FCityBuildToolModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	FCityBuildToolEditorModeCommands::Register();
}

void FCityBuildToolModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	FCityBuildToolEditorModeCommands::Unregister();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCityBuildToolModule, CityBuildTool)
