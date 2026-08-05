// Copyright (c) Victor Rivas Perez. All Rights Reserved.

#include "GrassGenerator.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GrassPlugin.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "TimerManager.h"

#if WITH_EDITOR
#include "Components/RuntimeVirtualTextureComponent.h"
#include "Engine/Texture2D.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "VT/RuntimeVirtualTexture.h"
#include "VT/RuntimeVirtualTextureVolume.h"
#endif

namespace
{
	/**
	 * Default asset paths, matching the sample content the plugin ships with.
	 *
	 * Seeds for the corresponding properties rather than baked-in constants: the previous
	 * version hardcoded them in the implementation, which meant the actor silently did nothing
	 * in any project that did not reproduce this exact folder layout.
	 */
	const TCHAR* const DefaultLandscapeMaterialPath = TEXT("/Game/StylizedGrass/Materials/MI_Landscape.MI_Landscape");
	const TCHAR* const DefaultGrassPhysicalMaterialPath = TEXT("/Game/StylizedGrass/Materials/PM_Grass.PM_Grass");
	const TCHAR* const DefaultOtherPhysicalMaterialPath = TEXT("/Game/StylizedGrass/Materials/PM_Other.PM_Other");
	const TCHAR* const DefaultLandscapeVirtualTexturePath = TEXT("/Game/StylizedGrass/Materials/RVT_Landscape.RVT_Landscape");
	const TCHAR* const DefaultLayerInfoPackageRoot = TEXT("/Game/StylizedGrass/LayerInfos");

	const FName DefaultGrassLayerName(TEXT("Grass"));
	const FName DefaultOtherLayerName(TEXT("Other"));

	/**
	 * Delay before the deferred passes run.
	 *
	 * The original scheduled these with a rate of 1.0s and a first delay of 0.1s, but a
	 * non-looping timer fires at its first delay and never uses the rate - so the effective
	 * wait has always been 0.1s. Kept at the value that was actually in effect.
	 */
	constexpr float DefaultLandscapeSettleDelay = 0.1f;

	/** Full weight for a landscape layer, in weightmap units. */
	constexpr uint8 FullLayerWeight = 255;

	/** Half a texel, the offset used to centre the virtual texture snap on the landscape grid. */
	constexpr float HalfTexel = 0.5f;

#if WITH_EDITOR
	/**
	 * Maps a weightmap channel index onto the FColor component holding it.
	 *
	 * Returned as a pointer-to-member so the branch is resolved once per texture rather than
	 * once per pixel, and so the mapping does not depend on FColor's in-memory byte order.
	 */
	uint8 FColor::* GetWeightmapChannelMember(uint8 ChannelIndex)
	{
		switch (ChannelIndex)
		{
		case 0:  return &FColor::R;
		case 1:  return &FColor::G;
		case 2:  return &FColor::B;
		case 3:  return &FColor::A;
		default: return nullptr;
		}
	}
#endif
}

AGrassGenerator::AGrassGenerator()
	: LandscapeMaterial(FSoftObjectPath(DefaultLandscapeMaterialPath))
	, GrassPhysicalMaterial(FSoftObjectPath(DefaultGrassPhysicalMaterialPath))
	, OtherPhysicalMaterial(FSoftObjectPath(DefaultOtherPhysicalMaterialPath))
	, LandscapeVirtualTexture(FSoftObjectPath(DefaultLandscapeVirtualTexturePath))
	, GrassLayerName(DefaultGrassLayerName)
	, OtherLayerName(DefaultOtherLayerName)
	, LayerInfoPackageRoot(DefaultLayerInfoPackageRoot)
	, bAutoGenerateOnConstruction(false)
	, LandscapeSettleDelay(DefaultLandscapeSettleDelay)
{
	PrimaryActorTick.bCanEverTick = false;

	// Assets are resolved in GenerateGrass, not here. LoadObject from a constructor runs during
	// CDO creation - before the asset registry is ready, and again during cook - which is why
	// UE offers ConstructorHelpers for the cases that genuinely need it. Nothing here does:
	// generation is an explicit, user-triggered action.
}

void AGrassGenerator::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

#if WITH_EDITOR
	if (bAutoGenerateOnConstruction)
	{
		GenerateGrass();
	}
#endif
}

void AGrassGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelPendingWork();
	Super::EndPlay(EndPlayReason);
}

void AGrassGenerator::Destroyed()
{
	CancelPendingWork();
	Super::Destroyed();
}

void AGrassGenerator::CancelPendingWork()
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		TimerManager.ClearTimer(VirtualTextureVolumeTimer);
		TimerManager.ClearTimer(LayerInfoTimer);
		TimerManager.ClearTimer(FillGrassTimer);
	}
}

#if WITH_EDITOR

bool AGrassGenerator::ResolveAssets()
{
	ResolvedLandscapeMaterial = LandscapeMaterial.LoadSynchronous();
	ResolvedGrassPhysicalMaterial = GrassPhysicalMaterial.LoadSynchronous();
	ResolvedOtherPhysicalMaterial = OtherPhysicalMaterial.LoadSynchronous();
	ResolvedLandscapeVirtualTexture = LandscapeVirtualTexture.LoadSynchronous();

	// Reported individually: "one of four assets failed" is not actionable.
	bool bAllResolved = true;
	const auto Require = [&bAllResolved](const UObject* Asset, const TCHAR* PropertyName, const FString& Path)
	{
		if (!Asset)
		{
			UE_LOG(LogGrassPlugin, Error, TEXT("%s could not be loaded from '%s'."), PropertyName, *Path);
			bAllResolved = false;
		}
	};

	Require(ResolvedLandscapeMaterial, TEXT("LandscapeMaterial"), LandscapeMaterial.ToString());
	Require(ResolvedGrassPhysicalMaterial, TEXT("GrassPhysicalMaterial"), GrassPhysicalMaterial.ToString());
	Require(ResolvedOtherPhysicalMaterial, TEXT("OtherPhysicalMaterial"), OtherPhysicalMaterial.ToString());
	Require(ResolvedLandscapeVirtualTexture, TEXT("LandscapeVirtualTexture"), LandscapeVirtualTexture.ToString());

	return bAllResolved;
}

void AGrassGenerator::GenerateGrass()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!ResolveAssets())
	{
		UE_LOG(LogGrassPlugin, Error, TEXT("Grass generation aborted: required assets are missing."));
		return;
	}

	ApplyLandscapeMaterials();

	// Scheduled once, not once per landscape. Both passes iterate every landscape themselves,
	// so scheduling them inside the landscape loop ran the whole job N times over for N
	// landscapes - and left N-1 uncancellable timers behind.
	TWeakObjectPtr<AGrassGenerator> WeakThis(this);
	FTimerManager& TimerManager = World->GetTimerManager();

	TimerManager.SetTimer(VirtualTextureVolumeTimer, [WeakThis]()
	{
		if (AGrassGenerator* Self = WeakThis.Get())
		{
			Self->SetupVirtualTextureVolume();
		}
	}, LandscapeSettleDelay, false);

	TimerManager.SetTimer(LayerInfoTimer, [WeakThis]()
	{
		if (AGrassGenerator* Self = WeakThis.Get())
		{
			Self->SetupLayerInfos();
		}
	}, LandscapeSettleDelay, false);
}

void AGrassGenerator::ApplyLandscapeMaterials()
{
	int32 LandscapeCount = 0;

	for (TActorIterator<ALandscape> It(GetWorld()); It; ++It)
	{
		ALandscape* Landscape = *It;
		++LandscapeCount;

		if (Landscape->GetLandscapeMaterial() == ResolvedLandscapeMaterial)
		{
			continue;
		}

		Landscape->Modify();
		Landscape->LandscapeMaterial = ResolvedLandscapeMaterial;

		FPropertyChangedEvent MaterialChangedEvent(FindFieldChecked<FProperty>(
			ALandscapeProxy::StaticClass(),
			GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LandscapeMaterial)));

		Landscape->PostEditChangeProperty(MaterialChangedEvent);
		Landscape->MarkPackageDirty();

		UE_LOG(LogGrassPlugin, Log, TEXT("Assigned landscape material to '%s'."), *Landscape->GetName());
	}

	if (LandscapeCount == 0)
	{
		UE_LOG(LogGrassPlugin, Warning, TEXT("No landscape actors found in this level."));
	}
}

void AGrassGenerator::SetupLayerInfos()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		ALandscape* Landscape = *It;

		ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
		if (!LandscapeInfo)
		{
			UE_LOG(LogGrassPlugin, Warning, TEXT("'%s' has no landscape info yet; skipping."), *Landscape->GetName());
			continue;
		}

		ULandscapeLayerInfoObject* GrassLayerInfo = nullptr;

		for (FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
		{
			const bool bIsGrassLayer = (LayerSettings.LayerName == GrassLayerName);

			if (LayerSettings.LayerInfoObj)
			{
				if (bIsGrassLayer)
				{
					GrassLayerInfo = LayerSettings.LayerInfoObj;
				}
				continue;
			}

			UPhysicalMaterial* PhysMaterial = nullptr;
			if (bIsGrassLayer)
			{
				PhysMaterial = ResolvedGrassPhysicalMaterial;
			}
			else if (LayerSettings.LayerName == OtherLayerName)
			{
				PhysMaterial = ResolvedOtherPhysicalMaterial;
			}
			else
			{
				UE_LOG(LogGrassPlugin, Warning,
					TEXT("Layer '%s' matches neither GrassLayerName nor OtherLayerName; leaving it alone."),
					*LayerSettings.LayerName.ToString());
				continue;
			}

			ULandscapeLayerInfoObject* LayerInfo = GetOrCreateLayerInfo(LayerSettings.LayerName, PhysMaterial);
			if (!LayerInfo)
			{
				UE_LOG(LogGrassPlugin, Warning, TEXT("Could not create a layer info for '%s'."),
					*LayerSettings.LayerName.ToString());
				continue;
			}

			LayerSettings.LayerInfoObj = LayerInfo;
			AddLayerInfoToLandscape(Landscape, LayerInfo);

			if (bIsGrassLayer)
			{
				GrassLayerInfo = LayerInfo;
			}
		}

		if (!GrassLayerInfo)
		{
			UE_LOG(LogGrassPlugin, Warning, TEXT("No '%s' layer on landscape '%s'; nothing to fill."),
				*GrassLayerName.ToString(), *Landscape->GetName());
			continue;
		}

		// Make sure every component carries an allocation for the grass layer.
		for (ULandscapeComponent* Component : Landscape->LandscapeComponents)
		{
			if (!Component)
			{
				continue;
			}

			const TArray<FWeightmapLayerAllocationInfo>& Allocations = Component->GetWeightmapLayerAllocations();
			const bool bAlreadyAllocated = Allocations.ContainsByPredicate(
				[GrassLayerInfo](const FWeightmapLayerAllocationInfo& Allocation)
				{
					return Allocation.LayerInfo == GrassLayerInfo;
				});

			if (bAlreadyAllocated)
			{
				continue;
			}

			Component->Modify();

			// INDEX_NONE asks the landscape system to pick the texture and channel during the
			// reallocation below.
			FWeightmapLayerAllocationInfo NewAllocation;
			NewAllocation.LayerInfo = GrassLayerInfo;
			NewAllocation.WeightmapTextureIndex = INDEX_NONE;
			Component->GetWeightmapLayerAllocations().Add(NewAllocation);

			Component->PostEditChange();
			Component->ReallocateWeightmaps(nullptr, true, true);
			Component->MarkRenderStateDirty();

			UE_LOG(LogGrassPlugin, Verbose, TEXT("Allocated the '%s' layer on component '%s'."),
				*GrassLayerName.ToString(), *Component->GetName());
		}

		TWeakObjectPtr<AGrassGenerator> WeakThis(this);
		TWeakObjectPtr<ALandscape> WeakLandscape(Landscape);
		TWeakObjectPtr<ULandscapeLayerInfoObject> WeakGrassLayerInfo(GrassLayerInfo);

		World->GetTimerManager().SetTimer(FillGrassTimer, [WeakThis, WeakLandscape, WeakGrassLayerInfo]()
		{
			AGrassGenerator* Self = WeakThis.Get();
			ALandscape* TargetLandscape = WeakLandscape.Get();
			ULandscapeLayerInfoObject* TargetLayerInfo = WeakGrassLayerInfo.Get();

			// Weak throughout: the original captured the actor, the landscape and the layer
			// info by raw pointer, any of which could be gone a second later.
			if (Self && TargetLandscape && TargetLayerInfo)
			{
				Self->FillGrassLayer(TargetLandscape, TargetLayerInfo);
			}
		}, LandscapeSettleDelay, false);

		Landscape->MarkPackageDirty();
		Landscape->PostEditChange();
		LandscapeInfo->UpdateAllComponentMaterialInstances();
	}
}

void AGrassGenerator::FillGrassLayer(ALandscape* Landscape, ULandscapeLayerInfoObject* GrassLayerInfo)
{
	const FScopedTransaction Transaction(NSLOCTEXT("GrassPlugin", "FillGrassLayer", "Fill Grass Layer"));

	ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		UE_LOG(LogGrassPlugin, Warning, TEXT("FillGrassLayer: '%s' has no landscape info."), *Landscape->GetName());
		return;
	}

	Landscape->InvalidateGeneratedComponentData();
	LandscapeInfo->UpdateAllComponentMaterialInstances();

	TArray<ULandscapeComponent*> LandscapeComponents;
	Landscape->GetComponents(LandscapeComponents);

	int32 FilledComponents = 0;

	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		if (!Component)
		{
			continue;
		}

		for (const FWeightmapLayerAllocationInfo& Allocation : Component->GetWeightmapLayerAllocations(true))
		{
			if (Allocation.LayerInfo != GrassLayerInfo)
			{
				continue;
			}

			const TArray<UTexture2D*>& WeightmapTextures = Component->GetWeightmapTextures();
			if (!WeightmapTextures.IsValidIndex(Allocation.WeightmapTextureIndex))
			{
				UE_LOG(LogGrassPlugin, Warning,
					TEXT("FillGrassLayer: weightmap index %d is out of range on '%s'."),
					Allocation.WeightmapTextureIndex, *Component->GetName());
				continue;
			}

			// Checked before the name is read from it; the previous version logged the texture
			// name first and only then tested for null.
			UTexture2D* WeightmapTexture = WeightmapTextures[Allocation.WeightmapTextureIndex];
			if (!WeightmapTexture || !WeightmapTexture->GetPlatformData())
			{
				UE_LOG(LogGrassPlugin, Warning,
					TEXT("FillGrassLayer: no platform data for the weightmap on '%s'."), *Component->GetName());
				continue;
			}

			// The layer's channel comes from the allocation. Writing all four components - as
			// the previous version did, despite a comment stating grass was on red - set every
			// layer sharing this texture to full weight, not just grass.
			uint8 FColor::* const ChannelMember = GetWeightmapChannelMember(Allocation.WeightmapTextureChannel);
			if (!ChannelMember)
			{
				UE_LOG(LogGrassPlugin, Warning, TEXT("FillGrassLayer: unexpected weightmap channel %d on '%s'."),
					Allocation.WeightmapTextureChannel, *Component->GetName());
				continue;
			}

			FTexture2DMipMap& Mip = WeightmapTexture->GetPlatformData()->Mips[0];
			FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
			if (!Pixels)
			{
				Mip.BulkData.Unlock();
				UE_LOG(LogGrassPlugin, Warning, TEXT("FillGrassLayer: could not lock the weightmap on '%s'."),
					*Component->GetName());
				continue;
			}

			const int32 PixelCount = WeightmapTexture->GetSizeX() * WeightmapTexture->GetSizeY();
			for (int32 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
			{
				Pixels[PixelIndex].*ChannelMember = FullLayerWeight;
			}

			Mip.BulkData.Unlock();
			WeightmapTexture->UpdateResource();
			++FilledComponents;
		}
	}

	Landscape->InvalidateGeneratedComponentData();
	LandscapeInfo->UpdateAllComponentMaterialInstances();

	UE_LOG(LogGrassPlugin, Log, TEXT("Filled the '%s' layer on %d of %d components of '%s'."),
		*GrassLayerInfo->LayerName.ToString(), FilledComponents, LandscapeComponents.Num(), *Landscape->GetName());
}

void AGrassGenerator::SetupVirtualTextureVolume()
{
	UWorld* World = GetWorld();
	if (!World || !ResolvedLandscapeVirtualTexture)
	{
		return;
	}

	const FScopedTransaction Transaction(
		NSLOCTEXT("GrassPlugin", "CreateRVTVolume", "Create Runtime Virtual Texture Volume"));

	for (TActorIterator<ARuntimeVirtualTextureVolume> It(World); It; ++It)
	{
		UE_LOG(LogGrassPlugin, Log, TEXT("A runtime virtual texture volume already exists; leaving it in place."));
		return;
	}

	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		ALandscape* Landscape = *It;
		Landscape->Modify();

		ARuntimeVirtualTextureVolume* Volume = World->SpawnActor<ARuntimeVirtualTextureVolume>();
		if (!Volume || !Volume->VirtualTextureComponent)
		{
			UE_LOG(LogGrassPlugin, Warning, TEXT("Could not spawn a runtime virtual texture volume."));
			continue;
		}

		Volume->Modify();
		Volume->VirtualTextureComponent->SetVirtualTexture(ResolvedLandscapeVirtualTexture);
		Landscape->RuntimeVirtualTextures.Add(ResolvedLandscapeVirtualTexture);

		const FQuat TargetRotation = Landscape->GetActorRotation().Quaternion();
		const FTransform LocalTransform(TargetRotation, Landscape->GetActorLocation(), FVector::OneVector);
		const FTransform WorldToLocal = LocalTransform.Inverse();

		// Grow the volume to cover every primitive writing into this virtual texture.
		FBox Bounds(ForceInit);
		for (TObjectIterator<UPrimitiveComponent> ComponentIt; ComponentIt; ++ComponentIt)
		{
			const TArray<URuntimeVirtualTexture*>& VirtualTextures = ComponentIt->GetRuntimeVirtualTextures();
			if (!VirtualTextures.Contains(ResolvedLandscapeVirtualTexture))
			{
				continue;
			}

			const FBoxSphereBounds LocalSpaceBounds =
				ComponentIt->CalcBounds(ComponentIt->GetComponentTransform() * WorldToLocal);

			if (LocalSpaceBounds.GetBox().GetVolume() > 0.0f)
			{
				Bounds += LocalSpaceBounds.GetBox();
			}
		}

		FTransform VolumeTransform(
			TargetRotation, LocalTransform.TransformPosition(Bounds.Min), Bounds.GetSize());

		if (Volume->VirtualTextureComponent->GetSnapBoundsToLandscape())
		{
			const ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
			if (LandscapeInfo)
			{
				const FVector LandscapeScale = Landscape->GetTransform().GetScale3D();

				int32 MinX, MinY, MaxX, MaxY;
				LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY);

				const FIntPoint LandscapeSize(MaxX - MinX + 1, MaxY - MinY + 1);
				const int32 LandscapeSizeLog2 =
					FMath::Max(FMath::CeilLogTwo(LandscapeSize.X), FMath::CeilLogTwo(LandscapeSize.Y));

				const int32 VirtualTextureSize = ResolvedLandscapeVirtualTexture->GetSize();
				const int32 VirtualTextureSizeLog2 = FMath::FloorLog2(VirtualTextureSize);

				const int32 TexelsPerVertexLog2 = FMath::Max(VirtualTextureSizeLog2 - LandscapeSizeLog2, 0);
				const int32 TexelsPerVertex = 1 << TexelsPerVertexLog2;
				const FVector TexelWorldSize = LandscapeScale / static_cast<float>(TexelsPerVertex);

				VolumeTransform.SetScale3D(FVector(
					(TexelWorldSize * static_cast<float>(VirtualTextureSize)).X,
					(TexelWorldSize * static_cast<float>(VirtualTextureSize)).Y,
					VolumeTransform.GetScale3D().Z));

				// Snap onto the landscape's texel grid so the virtual texture is not sampled
				// half a texel off.
				const FVector BasePosition = VolumeTransform.GetTranslation();
				const FVector SnapOrigin = Landscape->GetTransform().GetTranslation() - HalfTexel * TexelWorldSize;

				const float SnapOffsetX = FMath::Frac((BasePosition.X - SnapOrigin.X) / TexelWorldSize.X) * TexelWorldSize.X;
				const float SnapOffsetY = FMath::Frac((BasePosition.Y - SnapOrigin.Y) / TexelWorldSize.Y) * TexelWorldSize.Y;

				VolumeTransform.SetTranslation(BasePosition - FVector(SnapOffsetX, SnapOffsetY, 0.0f));
			}
		}

		Volume->SetActorTransform(VolumeTransform);
		Volume->VirtualTextureComponent->MarkRenderStateDirty();

		UE_LOG(LogGrassPlugin, Log, TEXT("Runtime virtual texture volume aligned to '%s'."), *Landscape->GetName());
	}
}

ULandscapeLayerInfoObject* AGrassGenerator::GetOrCreateLayerInfo(FName LayerName, UPhysicalMaterial* PhysMaterial)
{
	const FString PackageName = FString::Printf(TEXT("%s/%s"), *LayerInfoPackageRoot, *LayerName.ToString());
	const FString AssetPath = FString::Printf(TEXT("%s.%s"), *PackageName, *LayerName.ToString());

	if (ULandscapeLayerInfoObject* Existing = LoadObject<ULandscapeLayerInfoObject>(nullptr, *AssetPath))
	{
		UPackage* ExistingPackage = Existing->GetOutermost();
		if (ExistingPackage && !ExistingPackage->IsFullyLoaded())
		{
			ExistingPackage->FullyLoad();
		}
		return Existing;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogGrassPlugin, Warning, TEXT("Could not create package '%s'."), *PackageName);
		return nullptr;
	}

	ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(
		Package, LayerName, RF_Public | RF_Standalone | RF_Transactional);

	if (!LayerInfo)
	{
		UE_LOG(LogGrassPlugin, Warning, TEXT("Could not create a layer info for '%s'."), *LayerName.ToString());
		return nullptr;
	}

	LayerInfo->LayerName = LayerName;
	LayerInfo->PhysMaterial = PhysMaterial;
	Package->MarkPackageDirty();

	const FString PackageFileName =
		FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	SaveArgs.SaveFlags = SAVE_None;

	if (!UPackage::SavePackage(Package, LayerInfo, *PackageFileName, SaveArgs))
	{
		// The object is still usable in memory, so it is returned rather than discarded - but
		// it will not survive a restart, which the caller cannot tell without being told.
		UE_LOG(LogGrassPlugin, Warning,
			TEXT("Layer info '%s' was created but could not be saved to '%s'; it will be lost on reload."),
			*LayerName.ToString(), *PackageFileName);
		return LayerInfo;
	}

	Package->FullyLoad();
	UE_LOG(LogGrassPlugin, Log, TEXT("Created and saved layer info '%s'."), *LayerName.ToString());

	return LayerInfo;
}

void AGrassGenerator::AddLayerInfoToLandscape(ALandscape* Landscape, ULandscapeLayerInfoObject* LayerInfo)
{
	if (!Landscape || !LayerInfo)
	{
		return;
	}

	const auto MatchesLayerInfo = [LayerInfo](const auto& Settings)
	{
		return Settings.LayerInfoObj == LayerInfo;
	};

	if (!Landscape->EditorLayerSettings.ContainsByPredicate(MatchesLayerInfo))
	{
		Landscape->EditorLayerSettings.Add(FLandscapeEditorLayerSettings(LayerInfo));
	}

	if (ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo())
	{
		if (!LandscapeInfo->Layers.ContainsByPredicate(MatchesLayerInfo))
		{
			LandscapeInfo->Layers.Add(FLandscapeInfoLayerSettings(LayerInfo, Landscape));
		}
	}
}

#else // !WITH_EDITOR

void AGrassGenerator::GenerateGrass()
{
	// Generation creates and saves assets, so it exists only in editor builds. Declared
	// unconditionally so Blueprints referencing it still compile in a packaged build.
	UE_LOG(LogGrassPlugin, Warning, TEXT("GenerateGrass is an editor-only operation."));
}

#endif // WITH_EDITOR
