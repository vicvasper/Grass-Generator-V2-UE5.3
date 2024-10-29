#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeEdit.h"
#include "LandscapeSubsystem.h"
#include "Landscape.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SavePackage.h"
#include "Logging/LogMacros.h"
#include "Editor.h"
#include "VT/RuntimeVirtualTextureVolume.h"
#include "VT/RuntimeVirtualTexture.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "GrassGenerator.generated.h"



UCLASS()
class GRASSPLUGIN_API AGrassGenerator : public AActor
{
    GENERATED_BODY()

public:
    // Constructor
    AGrassGenerator();

protected:
    // Called when the actor is constructed (in editor o durante el juego)
    virtual void OnConstruction(const FTransform& Transform) override;

public:
    // Método para generar césped
    UFUNCTION(CallInEditor, Category = "Grass Generation")
    void GenerateGrass();

private:
    // Método para obtener o crear LayerInfo para una capa específica
    ULandscapeLayerInfoObject* GetOrCreateLayerInfo(FName LayerName, class UPhysicalMaterial* PhysMaterial);

    // Método para configurar y alinear el Runtime Virtual Texture Volume con el Landscape
    void SetupVirtualTextureVolume();

    // Método que se ejecutará tras un delay para configurar las LayerInfos
    void SetupLayerInfos();

    // Método para añadir la LayerInfo al Landscape
    void AddLayerInfoToLandscape(ALandscape* Landscape, ULandscapeLayerInfoObject* LayerInfo); // <-- Añadir esta declaración

    void FillGrassLayer(ALandscape* Landscape, ULandscapeLayerInfoObject* GrassLayerInfo);

    UTexture2D* CreateNewWeightmapTexture();

    // Propiedad para determinar si se debe auto-generar césped al construir el actor
    UPROPERTY(BlueprintReadWrite, Category = "Grass Generation", meta = (AllowPrivateAccess = "true"))
    bool bAutoGenerateGrass;

    // Rutas de los activos
    UPROPERTY(meta = (AllowPrivateAccess = "true"))
    FString LandscapeMaterialPath;

    UPROPERTY(meta = (AllowPrivateAccess = "true"))
    FString GrassPhysicalMaterialPath;

    UPROPERTY(meta = (AllowPrivateAccess = "true"))
    FString OtherPhysicalMaterialPath;

    UPROPERTY(meta = (AllowPrivateAccess = "true"))
    FString LandscapeVirtualTexturePath;

    // Material físico del césped
    UPROPERTY( BlueprintReadOnly, Category = "Grass Generation", meta = (AllowPrivateAccess = "true"))
    UPhysicalMaterial* GrassPhysicalMaterial;

    // Material físico del césped
    UPROPERTY( BlueprintReadOnly, Category = "Grass Generation", meta = (AllowPrivateAccess = "true"))
    UPhysicalMaterial* OtherPhysicalMaterial;

    UPROPERTY( BlueprintReadOnly, Category = "Grass Generation", meta = (AllowPrivateAccess = "true"))
    URuntimeVirtualTexture* LandscapeVirtualTexture;

    UMaterialInterface* LandscapeMaterial;

};
