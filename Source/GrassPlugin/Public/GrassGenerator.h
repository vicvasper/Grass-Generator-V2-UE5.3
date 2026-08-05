// Copyright (c) Victor Rivas Perez. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "GrassGenerator.generated.h"

class ALandscape;
class ULandscapeLayerInfoObject;
class UMaterialInterface;
class UPhysicalMaterial;
class URuntimeVirtualTexture;

/**
 * Editor utility actor that prepares a landscape for the stylized grass setup: it assigns the
 * landscape material, creates the layer infos the material expects, registers a runtime
 * virtual texture volume aligned to the landscape, and fills the grass weightmap.
 *
 * Drop one into a level containing a landscape and press "Generate Grass" in the details
 * panel. The work is editor-only - it creates and saves assets - and does nothing at runtime.
 *
 * Every asset it depends on is a property rather than a baked-in path, so the actor works in
 * projects that do not reproduce the sample content's folder layout.
 */
UCLASS()
class GRASSPLUGIN_API AGrassGenerator : public AActor
{
	GENERATED_BODY()

public:
	AGrassGenerator();

	/** Runs the full setup pass against every landscape in the current level. */
	UFUNCTION(CallInEditor, Category = "Grass Generation")
	void GenerateGrass();

	//~ Begin AActor interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
	//~ End AActor interface

protected:
	// -- Assets ------------------------------------------------------------------------

	/** Material applied to every landscape found. Must expose the grass and other layers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Generation|Assets")
	TSoftObjectPtr<UMaterialInterface> LandscapeMaterial;

	/** Physical material assigned to the generated grass layer info. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Generation|Assets")
	TSoftObjectPtr<UPhysicalMaterial> GrassPhysicalMaterial;

	/** Physical material assigned to the generated non-grass layer info. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Generation|Assets")
	TSoftObjectPtr<UPhysicalMaterial> OtherPhysicalMaterial;

	/** Runtime virtual texture the generated volume writes into. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Generation|Assets")
	TSoftObjectPtr<URuntimeVirtualTexture> LandscapeVirtualTexture;

	// -- Layers ------------------------------------------------------------------------

	/** Name of the landscape layer treated as grass, matching the landscape material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Generation|Layers")
	FName GrassLayerName;

	/** Name of the landscape layer treated as everything else. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Generation|Layers")
	FName OtherLayerName;

	/** Content path the generated layer info assets are created under. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Generation|Layers")
	FString LayerInfoPackageRoot;

	// -- Behaviour ---------------------------------------------------------------------

	/**
	 * Runs generation from OnConstruction.
	 *
	 * Off by default, and worth leaving off. OnConstruction fires on every move, rotate and
	 * property edit of this actor, and generation creates assets, saves packages to disk and
	 * rewrites landscape weightmaps - so enabling this makes nudging the actor in the viewport
	 * a destructive operation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Generation")
	bool bAutoGenerateOnConstruction;

	/**
	 * Delay before the deferred setup passes run.
	 *
	 * Assigning the landscape material and adding layer infos kicks off asynchronous work in
	 * the landscape system; the passes that follow read the results. This delay is what gives
	 * that work time to land. It is a heuristic, not a synchronisation primitive - see the
	 * README's limitations section.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Generation",
		meta = (ClampMin = "0.0", Units = "s"))
	float LandscapeSettleDelay;

	//~ Begin AActor interface
	virtual void OnConstruction(const FTransform& Transform) override;
	//~ End AActor interface

private:
#if WITH_EDITOR
	/** Resolves the soft asset references into the transient hard references. */
	bool ResolveAssets();

	/** Assigns LandscapeMaterial to every landscape and schedules the deferred passes. */
	void ApplyLandscapeMaterials();

	/** Creates and assigns the layer infos, then schedules the weightmap fill. */
	void SetupLayerInfos();

	/** Creates a runtime virtual texture volume aligned and snapped to the landscape. */
	void SetupVirtualTextureVolume();

	/** Writes full weight into the grass layer's weightmap channel. */
	void FillGrassLayer(ALandscape* Landscape, ULandscapeLayerInfoObject* GrassLayerInfo);

	/** Loads the layer info asset for LayerName, creating and saving it if absent. */
	ULandscapeLayerInfoObject* GetOrCreateLayerInfo(FName LayerName, UPhysicalMaterial* PhysMaterial);

	/** Registers LayerInfo with the landscape's editor and info layer lists, if not present. */
	void AddLayerInfoToLandscape(ALandscape* Landscape, ULandscapeLayerInfoObject* LayerInfo);
#endif

	/** Cancels any deferred pass still pending, so it cannot fire against a destroyed actor. */
	void CancelPendingWork();

	// Hard references to the resolved assets. Held as UPROPERTYs so they are visible to the
	// garbage collector: the previous version stored the landscape material in a bare pointer
	// loaded from the constructor, leaving it collectable while still in use.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ResolvedLandscapeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> ResolvedGrassPhysicalMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> ResolvedOtherPhysicalMaterial;

	UPROPERTY(Transient)
	TObjectPtr<URuntimeVirtualTexture> ResolvedLandscapeVirtualTexture;

	// Kept as members rather than locals so the deferred passes can be cancelled. Local
	// handles cannot be, which left timers firing into a destroyed actor.
	FTimerHandle VirtualTextureVolumeTimer;
	FTimerHandle LayerInfoTimer;
	FTimerHandle FillGrassTimer;
};
