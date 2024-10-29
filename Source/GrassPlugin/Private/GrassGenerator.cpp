#include "GrassGenerator.h"
#include <Editor/LandscapeEditor/Public/LandscapeEditorUtils.h>
#include <Editor/LandscapeEditor/Public/LandscapeEditorObject.h>
#include "Engine/Texture2D.h"
#include "EngineUtils.h" // Para TActorIterator
#include "ScopedTransaction.h"

// Constructor
AGrassGenerator::AGrassGenerator()
{
    PrimaryActorTick.bCanEverTick = false; // No necesitamos Tick
    bAutoGenerateGrass = true; // Por defecto, no auto-generar césped

    // Asignar rutas de activos a los miembros de clase existentes
    LandscapeMaterialPath = TEXT("/Game/StylizedGrass/Materials/MI_Landscape.MI_Landscape");
    GrassPhysicalMaterialPath = TEXT("/Game/StylizedGrass/Materials/PM_Grass.PM_Grass");
    OtherPhysicalMaterialPath = TEXT("/Game/StylizedGrass/Materials/PM_Other.PM_Other");
    LandscapeVirtualTexturePath = TEXT("/Game/StylizedGrass/Materials/RVT_Landscape.RVT_Landscape");

    LandscapeMaterial = LoadObject<UMaterialInterface>(nullptr, *this->LandscapeMaterialPath);
    GrassPhysicalMaterial = LoadObject<UPhysicalMaterial>(nullptr, *this->GrassPhysicalMaterialPath);
    OtherPhysicalMaterial = LoadObject<UPhysicalMaterial>(nullptr, *this->OtherPhysicalMaterialPath);
    LandscapeVirtualTexture = LoadObject<URuntimeVirtualTexture>(nullptr, *this->LandscapeVirtualTexturePath);
}


// Called when the actor is constructed (en el editor o durante el juego)
void AGrassGenerator::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

#if WITH_EDITOR
    if (bAutoGenerateGrass)
    {
        GenerateGrass();
    }
#endif
}

void AGrassGenerator::GenerateGrass()
{
#if WITH_EDITOR
    UE_LOG(LogTemp, Log, TEXT("Inicio de GenerateGrass."));



    if (!LandscapeMaterial || !GrassPhysicalMaterial || !OtherPhysicalMaterial || !LandscapeVirtualTexture)
    {
        UE_LOG(LogTemp, Warning, TEXT("No se pudieron cargar todos los activos necesarios."));
        return;
    }
    UE_LOG(LogTemp, Log, TEXT("Todos los activos necesarios cargados correctamente."));

    UE_LOG(LogTemp, Log, TEXT("Transacción iniciada para generación de césped."));

    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscape::StaticClass(), FoundActors);
    UE_LOG(LogTemp, Log, TEXT("Se encontraron %d actores de tipo Landscape."), FoundActors.Num());

    for (AActor* Actor : FoundActors)
    {
        ALandscape* Landscape = Cast<ALandscape>(Actor);
        if (Landscape)
        {
            UE_LOG(LogTemp, Log, TEXT("Procesando Landscape: %s"), *Landscape->GetName());

            Landscape->Modify();

            if (Landscape->GetLandscapeMaterial() != LandscapeMaterial)
            {
                Landscape->LandscapeMaterial = LandscapeMaterial;

                FPropertyChangedEvent MaterialChangedEvent(FindFieldChecked<FProperty>(ALandscapeProxy::StaticClass(), GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LandscapeMaterial)));
                Landscape->PostEditChangeProperty(MaterialChangedEvent);
                Landscape->MarkPackageDirty();
                UE_LOG(LogTemp, Log, TEXT("Material del Landscape actualizado."));
            }

            FTimerHandle VolumeTimerHandle;
            GetWorld()->GetTimerManager().SetTimer(VolumeTimerHandle, this, &AGrassGenerator::SetupVirtualTextureVolume, 1.0f, false, 0.1f);

            FTimerHandle LayerTimerHandle;
            GetWorld()->GetTimerManager().SetTimer(LayerTimerHandle, this, &AGrassGenerator::SetupLayerInfos, 1.0f, false, 0.1f);
   


        }
    }

    UE_LOG(LogTemp, Log, TEXT("Finalización inicial de GenerateGrass."));
#endif
}

void AGrassGenerator::SetupLayerInfos()
{
#if WITH_EDITOR
    UE_LOG(LogTemp, Log, TEXT("Iniciando configuración de LayerInfos."));

    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscape::StaticClass(), FoundActors);

    for (AActor* Actor : FoundActors)
    {
        ALandscape* Landscape = Cast<ALandscape>(Actor);
        if (Landscape)
        {
            UE_LOG(LogTemp, Log, TEXT("Procesando Landscape: %s"), *Landscape->GetName());

            ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
            if (!LandscapeInfo)
            {
                UE_LOG(LogTemp, Warning, TEXT("No se pudo obtener LandscapeInfo para el Landscape."));
                continue;
            }

            ULandscapeLayerInfoObject* GrassLayerInfo = nullptr;

            // Iterar sobre los LayerSettings
            for (FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
            {
                if (LayerSettings.LayerInfoObj == nullptr)
                {
                    UPhysicalMaterial* PhysMaterial = nullptr;

                    if (LayerSettings.LayerName == FName(TEXT("Grass")))
                    {
                        UE_LOG(LogTemp, Log, TEXT("Asignando LayerInfo para la capa 'Grass'."));
                        PhysMaterial = GrassPhysicalMaterial;
                    }
                    else if (LayerSettings.LayerName == FName(TEXT("Other")))
                    {
                        UE_LOG(LogTemp, Log, TEXT("Asignando LayerInfo para la capa 'Other'."));
                        PhysMaterial = OtherPhysicalMaterial;
                    }
                    else
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Capa desconocida: %s"), *LayerSettings.LayerName.ToString());
                        continue;
                    }

                    // Crear o cargar LayerInfo
                    ULandscapeLayerInfoObject* LayerInfo = GetOrCreateLayerInfo(LayerSettings.LayerName, PhysMaterial);
                    if (LayerInfo)
                    {
                        LayerSettings.LayerInfoObj = LayerInfo;

                        // Asignar LayerInfo al Landscape
                        AddLayerInfoToLandscape(Landscape, LayerInfo);
                        UE_LOG(LogTemp, Log, TEXT("LayerInfo '%s' asignada correctamente a la capa '%s'"), *LayerInfo->GetName(), *LayerSettings.LayerName.ToString());

                        // Si la capa es "Grass", guardar la referencia al LayerInfo
                        if (LayerSettings.LayerName == FName(TEXT("Grass")))
                        {
                            GrassLayerInfo = LayerInfo;
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Warning, TEXT("No se pudo crear o cargar completamente LayerInfo para la capa '%s'."), *LayerSettings.LayerName.ToString());
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Log, TEXT("La capa '%s' ya tiene asignada una LayerInfo."), *LayerSettings.LayerName.ToString());

                    // Si la capa es "Grass", guardar la referencia al LayerInfo
                    if (LayerSettings.LayerName == FName(TEXT("Grass")))
                    {
                        GrassLayerInfo = LayerSettings.LayerInfoObj;
                    }
                }
            }

            // Verificar si se obtuvo GrassLayerInfo antes de proceder
            if (!GrassLayerInfo)
            {
                UE_LOG(LogTemp, Warning, TEXT("No se encontró LayerInfo para la capa 'Grass' en el Landscape '%s'."), *Landscape->GetName());
                continue;
            }

            // Combinar ambos bucles en uno solo para procesar los componentes del Landscape
            for (ULandscapeComponent* Component : Landscape->LandscapeComponents)
            {
                if (!Component || !GrassLayerInfo)
                {
                    UE_LOG(LogTemp, Warning, TEXT("Componente o GrassLayerInfo inválido en el Landscape '%s'."), *Landscape->GetName());
                    continue;
                }

                // Modificar el componente antes de hacer cualquier cambio
                Component->Modify();

                // Verificar si la capa 'Grass' ya está asignada al componente
                bool bLayerFound = false;
                for (const FWeightmapLayerAllocationInfo& Allocation : Component->GetWeightmapLayerAllocations())
                {
                    if (Allocation.LayerInfo == GrassLayerInfo)
                    {
                        bLayerFound = true;
                        break;
                    }
                }

                if (!bLayerFound)
                {
                    // Agregar la capa 'Grass' a las asignaciones del componente
                    FWeightmapLayerAllocationInfo NewAllocation;
                    NewAllocation.LayerInfo = GrassLayerInfo;
                    NewAllocation.WeightmapTextureIndex = INDEX_NONE; // Inicializar como INDEX_NONE para que Unreal lo reasigne automáticamente
                    Component->GetWeightmapLayerAllocations().Add(NewAllocation);
                    UE_LOG(LogTemp, Log, TEXT("Capa 'Grass' agregada al componente '%s'."), *Component->GetName());

                    // Llamar a PostEditChange para asegurarse de que la asignación se procese completamente
                    Component->PostEditChange();

                    // Forzar la reasignación de weightmaps
                    Component->ReallocateWeightmaps(nullptr, true, true);
                    Component->MarkRenderStateDirty();
                    UE_LOG(LogTemp, Log, TEXT("Reasignando weightmaps para el componente: %s"), *Component->GetName());

                    // Verificar y asegurar que hay suficiente espacio en ComponentWeightmapTextures
                    int32 NumWeightmapTextures = Component->GetWeightmapTextures().Num();
                    if (NumWeightmapTextures <= NewAllocation.WeightmapTextureIndex)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("Aumentando espacio en WeightmapTextures para el componente '%s'."), *Component->GetName());
                        // Agregar una nueva textura al arreglo si es necesario
                        UTexture2D* NewWeightmapTexture = CreateNewWeightmapTexture();
                        Component->GetWeightmapTextures().Add(NewWeightmapTexture);
                    }

                    // Verificar que se ha reasignado correctamente
                    bool bValidIndex = true;
                    for (const FWeightmapLayerAllocationInfo& Allocation : Component->GetWeightmapLayerAllocations())
                    {
                        if (!Component->GetWeightmapTextures().IsValidIndex(Allocation.WeightmapTextureIndex))
                        {
                            bValidIndex = false;
                            UE_LOG(LogTemp, Warning, TEXT("Índice inválido encontrado en WeightmapTextureIndex después de realloc para el componente: %s"), *Component->GetName());
                            break;
                        }
                    }

                    if (!bValidIndex)
                    {
                        // Si el índice no es válido, intentar reasignar nuevamente
                        Component->ReallocateWeightmaps(nullptr, true, true);
                        Component->MarkRenderStateDirty();
                        UE_LOG(LogTemp, Warning, TEXT("Intentando reasignar weightmaps nuevamente para el componente: %s"), *Component->GetName());
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Log, TEXT("La capa 'Grass' ya está asignada al componente '%s'."), *Component->GetName());
                }
            }

            // Llamar a FillGrassLayer después de configurar todas las capas y asignaciones
            FTimerHandle FillGrassTimerHandle;
            GetWorld()->GetTimerManager().SetTimer(FillGrassTimerHandle, [this, Landscape, GrassLayerInfo]()
                {
                    FillGrassLayer(Landscape, GrassLayerInfo);
                    UE_LOG(LogTemp, Log, TEXT("FillGrassLayer ejecutado para la capa 'Grass' en el Landscape '%s'."), *Landscape->GetName());
                }, 1.0f, false, 0.1);
            UE_LOG(LogTemp, Log, TEXT("FillGrassLayer programado para el Landscape '%s'."), *Landscape->GetName());

            // Forzar actualización del Landscape después de configurar las capas y llamar a FillGrassLayer
            Landscape->MarkPackageDirty();
            Landscape->PostEditChange();
            FPropertyChangedEvent MaterialChangedEvent(FindFieldChecked<FProperty>(ALandscapeProxy::StaticClass(), GET_MEMBER_NAME_CHECKED(ALandscapeProxy, LandscapeMaterial)));
            Landscape->PostEditChangeProperty(MaterialChangedEvent);
            LandscapeInfo->UpdateAllComponentMaterialInstances();
            UE_LOG(LogTemp, Log, TEXT("Capa del Landscape '%s' actualizada correctamente."), *Landscape->GetName());
        }
    }
#endif
}


UTexture2D* AGrassGenerator::CreateNewWeightmapTexture()
{
    // Implementar la lógica para crear una nueva textura para Weightmap
    // Esto puede incluir crear una textura con un tamaño específico y configuraciones apropiadas.
    return NewObject<UTexture2D>();
}


void AGrassGenerator::FillGrassLayer(ALandscape* Landscape, ULandscapeLayerInfoObject* GrassLayerInfo)
{
#if WITH_EDITOR
    // Iniciar una transacción para soporte de Undo/Redo
    const FScopedTransaction Transaction(FText::FromString("Fill Grass Layer"));
    UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: Transacción iniciada para llenar la capa 'Grass'."));

    // Obtener LandscapeInfo
    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (!LandscapeInfo)
    {
        UE_LOG(LogTemp, Warning, TEXT("FillGrassLayer: LandscapeInfo no válido."));
        return;
    }

    // Actualizar los materiales de los componentes y generar datos para asegurarse de que todo esté listo
    Landscape->InvalidateGeneratedComponentData();
    LandscapeInfo->UpdateAllComponentMaterialInstances();
    UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: Todos los componentes han sido actualizados."));

    // Obtener todos los componentes del Landscape
    TArray<ULandscapeComponent*> LandscapeComponents;
    Landscape->GetComponents(LandscapeComponents);
    UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: Se encontraron %d componentes de Landscape."), LandscapeComponents.Num());


        // Iterar sobre todos los componentes del Landscape
        for (ULandscapeComponent* Component : LandscapeComponents)
        {
            if (!Component)
            {
                UE_LOG(LogTemp, Warning, TEXT("FillGrassLayer: Componente de Landscape no válido."));
                continue;
            }
            UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: Procesando componente de Landscape: %s"), *Component->GetName());

            // Obtener las asignaciones de capa de peso (WeightmapLayerAllocations)
            TArray<FWeightmapLayerAllocationInfo>& LayerAllocations = Component->GetWeightmapLayerAllocations(true);
            UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: Se encontraron %d asignaciones de capas en el componente: %s"), LayerAllocations.Num(), *Component->GetName());

            if (LayerAllocations.Num() == 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("FillGrassLayer: No se encontraron asignaciones de capas en el componente: %s"), *Component->GetName());
                continue;
            }

            // Iterar sobre todas las asignaciones de capas de peso
            for (FWeightmapLayerAllocationInfo& LayerInfo : LayerAllocations)
            {
                UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: Iterando sobre una LayerInfo en el componente '%s'."), *Component->GetName());

                if (LayerInfo.LayerInfo)
                {
                    // Obtener el nombre de la capa y convertirlo a texto
                    FString LayerName = LayerInfo.LayerInfo->LayerName.ToString();
                    UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: Nombre de la capa en el componente '%s': %s"), *Component->GetName(), *LayerName);

                    // Comparar el nombre de la capa con "Grass"
                    if (LayerName == "Grass")
                    {
                        UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: Capa 'Grass' encontrada en el componente: %s"), *Component->GetName());

                        // Obtener el índice de la capa en el array de WeightmapTextures
                        int32 WeightmapIndex = LayerInfo.WeightmapTextureIndex;
                        UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: Índice de Weightmap para la capa 'Grass': %d"), WeightmapIndex);

                        // Verificar si el índice es válido
                        if (Component->GetWeightmapTextures().IsValidIndex(WeightmapIndex))
                        {
                            // Obtener la textura de Weightmap correspondiente
                            UTexture2D* WeightmapTexture = Component->GetWeightmapTextures()[WeightmapIndex];
                            UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: WeightmapTexture obtenida para el componente: %s"), *WeightmapTexture->GetName());

                            // Verificar si la textura y sus datos de plataforma son válidos
                            if (!WeightmapTexture || !WeightmapTexture->GetPlatformData())
                            {
                                UE_LOG(LogTemp, Warning, TEXT("FillGrassLayer: WeightmapTexture no válida para la capa 'Grass' en el componente: %s"), *Component->GetName());
                                continue;
                            }

                            // Bloquear el acceso a los datos de la textura
                            FTexture2DMipMap& Mip = WeightmapTexture->GetPlatformData()->Mips[0];
                            void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
                            FColor* FormData = static_cast<FColor*>(Data);

                            if (!FormData)
                            {
                                UE_LOG(LogTemp, Warning, TEXT("FillGrassLayer: No se pudo acceder a los datos de la textura para 'Grass' en el componente: %s"), *Component->GetName());
                                Mip.BulkData.Unlock();
                                continue;
                            }

                            // Obtener las dimensiones de la textura
                            int32 TextureSizeX = WeightmapTexture->GetSizeX();
                            int32 TextureSizeY = WeightmapTexture->GetSizeY();
                            UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: Dimensiones de la textura: (%d, %d)"), TextureSizeX, TextureSizeY);

                            // Iterar sobre todos los píxeles y establecer el valor de la capa "Grass" al máximo
                            for (int32 Y = 0; Y < TextureSizeY; ++Y)
                            {
                                for (int32 X = 0; X < TextureSizeX; ++X)
                                {
                                    int32 Index = Y * TextureSizeX + X;
                                    FColor& Pixel = FormData[Index];

                                    // Asumiendo que "Grass" está asignado al canal Rojo (R)
                                    Pixel.R = 255;
                                    Pixel.G = 255;
                                    Pixel.B = 255;
                                    Pixel.A = 255;
                                }
                            }

                            // Desbloquear y actualizar la textura
                            Mip.BulkData.Unlock();
                            WeightmapTexture->UpdateResource();

                            UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: Capa 'Grass' completamente pintada en el componente: %s"), *Component->GetName());
                        }
                        else
                        {
                            UE_LOG(LogTemp, Warning, TEXT("FillGrassLayer: Índice de WeightmapTexture no válido para el componente: %s"), *Component->GetName());
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Log, TEXT("FillGrassLayer: La capa actual no es 'Grass' (es '%s'). Continuando con la siguiente."), *LayerName);
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("FillGrassLayer: LayerInfo.LayerInfo es nullptr en el componente '%s'."), *Component->GetName());
                }
            }
        }

        // Asegurarse de que los datos estén actualizados después de la primera iteración
        Landscape->InvalidateGeneratedComponentData();
        LandscapeInfo->UpdateAllComponentMaterialInstances();
    

#endif
}











void AGrassGenerator::SetupVirtualTextureVolume()
{
#if WITH_EDITOR
    UE_LOG(LogTemp, Log, TEXT("Iniciando configuración de Virtual Texture Volume."));

    // Iniciar una transacción para soportar Undo/Redo
    const FScopedTransaction Transaction(FText::FromString("Crear Runtime Virtual Texture Volume"));

    // Comprobar si ya existe un RVT_Volume en la escena
    for (TActorIterator<ARuntimeVirtualTextureVolume> It(GetWorld()); It; ++It)
    {
        ARuntimeVirtualTextureVolume* ExistingVolume = *It;
        if (ExistingVolume)
        {
            UE_LOG(LogTemp, Log, TEXT("Ya existe un Runtime Virtual Texture Volume en la escena, no se creará otro."));
            return; // Salir si ya existe un RVT_Volume
        }
    }

    // Obtener todos los actores de tipo Landscape en el mundo nuevamente
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ALandscape::StaticClass(), FoundActors);

    for (AActor* Actor : FoundActors)
    {
        ALandscape* Landscape = Cast<ALandscape>(Actor);
        if (Landscape)
        {
            // Marcar el Landscape como modificado para registrar los cambios en la transacción
            Landscape->Modify();

            ARuntimeVirtualTextureVolume* RVT_Volume = GetWorld()->SpawnActor<ARuntimeVirtualTextureVolume>(ARuntimeVirtualTextureVolume::StaticClass());
            if (RVT_Volume)
            {
                // Marcar el RVT_Volume como modificado para soportar Undo/Redo
                RVT_Volume->Modify();

                // *** Asignar la LandscapeVirtualTexture ***
                if (LandscapeVirtualTexture) // Verificamos que esté cargada correctamente
                {
                    RVT_Volume->VirtualTextureComponent->SetVirtualTexture(LandscapeVirtualTexture);
                    Landscape->RuntimeVirtualTextures.Add(LandscapeVirtualTexture);
                    UE_LOG(LogTemp, Log, TEXT("LandscapeVirtualTexture asignada correctamente al Runtime Virtual Texture Volume."));
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("No se pudo asignar LandscapeVirtualTexture porque no está cargada."));
                }

                // Cálculo de los límites del Landscape
                FVector Origin;
                FVector BoxExtent;
                Landscape->GetActorBounds(false, Origin, BoxExtent);

                // Alinear el componente Virtual Texture con el landscape
                FQuat TargetRotation = Landscape->GetActorRotation().Quaternion();
                FVector TargetPosition = Landscape->GetActorLocation();

                FTransform LocalTransform(TargetRotation, TargetPosition, FVector::OneVector);
                FTransform WorldToLocal = LocalTransform.Inverse();

                // Expandir los límites para el actor alineado a los bounds y todas las primitivas que escriben en esta textura virtual
                FBox Bounds(ForceInit);
                for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
                {
                    bool bUseBounds = false;
                    const TArray<URuntimeVirtualTexture*>& VirtualTextures = It->GetRuntimeVirtualTextures();
                    for (int32 Index = 0; Index < VirtualTextures.Num(); ++Index)
                    {
                        if (VirtualTextures[Index] == RVT_Volume->VirtualTextureComponent->GetVirtualTexture())
                        {
                            bUseBounds = true;
                            break;
                        }
                    }

                    if (bUseBounds)
                    {
                        FBoxSphereBounds LocalSpaceBounds = It->CalcBounds(It->GetComponentTransform() * WorldToLocal);
                        if (LocalSpaceBounds.GetBox().GetVolume() > 0.f)
                        {
                            Bounds += LocalSpaceBounds.GetBox();
                        }
                    }
                }

                // Calcular la transformación para ajustar los bounds
                FVector LocalPosition = Bounds.Min;
                FVector WorldPosition = LocalTransform.TransformPosition(LocalPosition);
                FVector WorldSize = Bounds.GetSize();
                FTransform Transform(TargetRotation, WorldPosition, WorldSize);

                // Ajustar y snapear al landscape si es necesario
                if (RVT_Volume->VirtualTextureComponent->GetSnapBoundsToLandscape())
                {
                    const ALandscape* LandscapeActor = Cast<ALandscape>(Landscape);
                    if (LandscapeActor)
                    {
                        const FTransform LandscapeTransform = LandscapeActor->GetTransform();
                        const FVector LandscapePosition = LandscapeTransform.GetTranslation();
                        const FVector LandscapeScale = LandscapeTransform.GetScale3D();

                        const ULandscapeInfo* LandscapeInfo = LandscapeActor->GetLandscapeInfo();
                        int32 MinX, MinY, MaxX, MaxY;
                        LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY);
                        const FIntPoint LandscapeSize(MaxX - MinX + 1, MaxY - MinY + 1);
                        const int32 LandscapeSizeLog2 = FMath::Max(FMath::CeilLogTwo(LandscapeSize.X), FMath::CeilLogTwo(LandscapeSize.Y));

                        const int32 VirtualTextureSize = LandscapeVirtualTexture->GetSize();
                        const int32 VirtualTextureSizeLog2 = FMath::FloorLog2(VirtualTextureSize);

                        // Ajustar escala
                        const int32 VirtualTexelsPerLandscapeVertexLog2 = FMath::Max(VirtualTextureSizeLog2 - LandscapeSizeLog2, 0);
                        const int32 VirtualTexelsPerLandscapeVertex = 1 << VirtualTexelsPerLandscapeVertexLog2;
                        const FVector VirtualTexelWorldSize = LandscapeScale / (float)VirtualTexelsPerLandscapeVertex;
                        const FVector VirtualTextureScale = VirtualTexelWorldSize * (float)VirtualTextureSize;

                        Transform.SetScale3D(FVector(VirtualTextureScale.X, VirtualTextureScale.Y, Transform.GetScale3D().Z));

                        // Snap de posición para alinear con el landscape
                        const FVector BaseVirtualTexturePosition = Transform.GetTranslation();
                        const FVector LandscapeSnapPosition = LandscapePosition - 0.5f * VirtualTexelWorldSize;
                        const float SnapOffsetX = FMath::Frac((BaseVirtualTexturePosition.X - LandscapeSnapPosition.X) / VirtualTexelWorldSize.X) * VirtualTexelWorldSize.X;
                        const float SnapOffsetY = FMath::Frac((BaseVirtualTexturePosition.Y - LandscapeSnapPosition.Y) / VirtualTexelWorldSize.Y) * VirtualTexelWorldSize.Y;
                        const FVector VirtualTexturePosition = BaseVirtualTexturePosition - FVector(SnapOffsetX, SnapOffsetY, 0);
                        Transform.SetTranslation(FVector(VirtualTexturePosition.X, VirtualTexturePosition.Y, VirtualTexturePosition.Z));
                    }
                }

                // Aplicar la transformación calculada
                RVT_Volume->SetActorTransform(Transform);

                // Marcar el estado de renderizado como sucio
                RVT_Volume->VirtualTextureComponent->MarkRenderStateDirty();

                UE_LOG(LogTemp, Log, TEXT("Runtime Virtual Texture Volume configurado correctamente."));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("Error al configurar el RVT Volume para el Landscape."));
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Finalización del proceso de configuración de Virtual Texture Volume."));
#endif
}

ULandscapeLayerInfoObject* AGrassGenerator::GetOrCreateLayerInfo(FName LayerName, UPhysicalMaterial* PhysMaterial)
{
#if WITH_EDITOR
    ULandscapeLayerInfoObject* LayerInfo = nullptr;
    // Construir el AssetPath con el sufijo _LayerInfo
    FString AssetPath = FString::Printf(TEXT("/Game/StylizedGrass/LayerInfos/%s.%s"), *LayerName.ToString(), *LayerName.ToString());

    // Intentar cargar el asset existente
    LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *AssetPath);

    if (!LayerInfo)
    {
        // Definir el nombre del paquete con el sufijo _LayerInfo
        FString PackageName = FString::Printf(TEXT("/Game/StylizedGrass/LayerInfos/%s"), *LayerName.ToString());
        UPackage* Package = CreatePackage(*PackageName);
        FString ObjectName = FString::Printf(TEXT("%s"), *LayerName.ToString());

        // Crear el objeto LayerInfo dentro del paquete creado
        LayerInfo = NewObject<ULandscapeLayerInfoObject>(Package, FName(*ObjectName), RF_Public | RF_Standalone | RF_Transactional);

        if (LayerInfo)
        {
            // Asignar propiedades al LayerInfo
            LayerInfo->LayerName = LayerName;
            LayerInfo->PhysMaterial = PhysMaterial;

            // Marcar el paquete como sucio para indicar que ha sido modificado
            Package->MarkPackageDirty();

            // Definir el nombre de archivo para guardar el paquete
            FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

            // Configurar los argumentos para guardar el paquete
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            SaveArgs.Error = GError;
            SaveArgs.SaveFlags = SAVE_None;

            // Guardar el paquete
            bool bSaved = UPackage::SavePackage(Package, LayerInfo, *PackageFileName, SaveArgs);
            if (bSaved)
            {
                UE_LOG(LogTemp, Log, TEXT("LayerInfo '%s' creada y guardada exitosamente."), *LayerInfo->GetName());

                // Asegurarse de que el paquete esté completamente cargado
                Package->FullyLoad();

                bool bFullyLoaded = Package->IsFullyLoaded();
                if (bFullyLoaded)
                {
                    UE_LOG(LogTemp, Log, TEXT("Paquete '%s' cargado completamente después de guardar."), *PackageName);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("No se pudo cargar completamente el paquete '%s' después de guardarlo."), *PackageName);
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("No se pudo guardar el LayerInfo para la capa '%s'."), *LayerName.ToString());
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("No se pudo crear el LayerInfo para la capa '%s'."), *LayerName.ToString());
        }
    }
    else
    {
        // Asegurarse de que el paquete del asset cargado esté completamente cargado
        UPackage* Package = LayerInfo->GetOutermost();
        if (Package && !Package->IsFullyLoaded())
        {
            Package->FullyLoad(); // Llama a FullyLoad sin asignar a una variable bool

            bool bLoaded = Package->IsFullyLoaded();
            if (bLoaded)
            {
                UE_LOG(LogTemp, Log, TEXT("Paquete del LayerInfo '%s' cargado completamente."), *LayerInfo->GetName());
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("No se pudo cargar completamente el paquete del LayerInfo '%s'."), *LayerInfo->GetName());
            }
        }
    }

    return LayerInfo;
#else
    return nullptr;
#endif
}


void AGrassGenerator::AddLayerInfoToLandscape(ALandscape* Landscape, ULandscapeLayerInfoObject* LayerInfo)
{
#if WITH_EDITOR
    if (!LayerInfo || !Landscape)
    {
        return;
    }

    if (!Landscape->EditorLayerSettings.ContainsByPredicate([LayerInfo](const FLandscapeEditorLayerSettings& Settings)
        {
            return Settings.LayerInfoObj == LayerInfo;
        }))
    {
        Landscape->EditorLayerSettings.Add(FLandscapeEditorLayerSettings(LayerInfo));
    }

    ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo();
    if (LandscapeInfo)
    {
        if (!LandscapeInfo->Layers.ContainsByPredicate([LayerInfo](const FLandscapeInfoLayerSettings& Settings)
            {
                return Settings.LayerInfoObj == LayerInfo;
            }))
        {
            LandscapeInfo->Layers.Add(FLandscapeInfoLayerSettings(LayerInfo, Landscape));
        }
    }
#endif
}