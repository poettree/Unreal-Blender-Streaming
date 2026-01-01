// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Networking.h"
#include "MeshReceiverSystem.generated.h"

class UProceduralMeshComponent;

/**
 * 
 */
UCLASS()
class PLAYGROUND_API UMeshReceiverSystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    FSocket* ListenerSocket;
    FTSTicker::FDelegateHandle TickerHandle;

    // Ticker function (Runs repeatedly)
    bool Tick(float DeltaTime);

    // Update logic
    void ProcessData(const TArray<uint8>& Data);
    void UpdateSceneMesh(const TArray<FVector>& Vertices, const TArray<int32>& Indices);

    void BakeToStaticMesh(UProceduralMeshComponent* ProcMesh);
	
};
