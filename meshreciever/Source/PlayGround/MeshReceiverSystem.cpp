
#include "MeshReceiverSystem.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "ProceduralMeshComponent.h"
#include "MeshDescription.h"
#include "StaticMeshDescription.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Editor.h"


// Helper to loop until we get exactly 'Count' bytes
bool RecvAll(FSocket* Socket, uint8* Buffer, int32 Count)
{
    int32 TotalRead = 0;
    while (TotalRead < Count)
    {
        int32 BytesRead = 0;
        // Recv can return fewer bytes than requested!
        if (!Socket->Recv(Buffer + TotalRead, Count - TotalRead, BytesRead))
        {
            return false; // Error or connection closed
        }
        TotalRead += BytesRead;
    }
    return true;
}

void UMeshReceiverSystem::BakeToStaticMesh(UProceduralMeshComponent* ProcMesh)
{
    if (!ProcMesh) return;

    // 1. Generate a unique name
    FString SavePath = "/Game/BakedMeshes/";
    FString MeshName = "SM_BlenderMesh_" + FDateTime::Now().ToString(TEXT("%H%M%S"));
    FString PackageName = SavePath + MeshName;

    // 2. Create the Package (The file container)
    UPackage* Package = CreatePackage(*PackageName);

    // 3. Create the Static Mesh Object
    UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, FName(*MeshName), RF_Public | RF_Standalone);
    StaticMesh->InitResources();
    StaticMesh->SetLightingGuid();

    // 4. Create Mesh Description (The modern way to edit meshes in UE5)
    FMeshDescription MeshDesc;
    FStaticMeshAttributes Attributes(MeshDesc);
    Attributes.Register();

    // Get Data from ProcMesh Section 0 (Assuming single section)
    FProcMeshSection* Section = ProcMesh->GetProcMeshSection(0);
    if (!Section) return;

    // -- BUILD MESH DESCRIPTION --
    TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
    TPolygonGroupAttributesRef<FName> PolygonGroupSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

    // A. Create a Polygon Group (Material Slot)
    FPolygonGroupID PolygonGroup = MeshDesc.CreatePolygonGroup();
    PolygonGroupSlotNames[PolygonGroup] = FName("MaterialSlot_0");

    // B. Add Vertices
    TMap<int32, FVertexID> IndexToVertexID; // Map ProcMesh index to MeshDescription VertexID

    for (int32 i = 0; i < Section->ProcVertexBuffer.Num(); i++)
    {
        FVertexID VertexID = MeshDesc.CreateVertex();
        VertexPositions[VertexID] = (FVector3f)Section->ProcVertexBuffer[i].Position; // Convert FVector to FVector3f
        IndexToVertexID.Add(i, VertexID);
    }

    // C. Add Triangles
    TArray<FVertexInstanceID> VertexInstanceIDs;
    VertexInstanceIDs.SetNum(3);

    for (int32 i = 0; i < Section->ProcIndexBuffer.Num(); i += 3)
    {
        // Get the 3 indices of the triangle
        int32 Index0 = Section->ProcIndexBuffer[i];
        int32 Index1 = Section->ProcIndexBuffer[i + 1];
        int32 Index2 = Section->ProcIndexBuffer[i + 2];

        // Create instances (UE needs unique instances for normals/UVs, but here we simplify)
        FVertexInstanceID Inst0 = MeshDesc.CreateVertexInstance(IndexToVertexID[Index0]);
        FVertexInstanceID Inst1 = MeshDesc.CreateVertexInstance(IndexToVertexID[Index1]);
        FVertexInstanceID Inst2 = MeshDesc.CreateVertexInstance(IndexToVertexID[Index2]);

        VertexInstanceIDs[0] = Inst0;
        VertexInstanceIDs[1] = Inst1;
        VertexInstanceIDs[2] = Inst2;

        MeshDesc.CreatePolygon(PolygonGroup, VertexInstanceIDs);
    }

    // 5. Build the Static Mesh from Description
    UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
    BuildParams.bBuildSimpleCollision = true;
    BuildParams.bFastBuild = true;

    StaticMesh->BuildFromMeshDescriptions({ &MeshDesc }, BuildParams);

    // 6. Finalize & Save
    StaticMesh->PostEditChange();
    Package->MarkPackageDirty();
    FAssetRegistryModule::AssetCreated(StaticMesh);

    UE_LOG(LogTemp, Warning, TEXT("✅ BAKE SUCCESS: Saved to %s"), *PackageName);
}

void UMeshReceiverSystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // 1. Create Socket
    FIPv4Endpoint Endpoint(FIPv4Address::Any, 8080);
    ListenerSocket = FTcpSocketBuilder(TEXT("BlenderListener"))
        .AsReusable()
        .BoundToEndpoint(Endpoint)
        .Listening(8);

    if (ListenerSocket)
    {
        UE_LOG(LogTemp, Warning, TEXT("Mesh Receiver Listening on Port 8080"));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Mesh Receiver Listening Failed!"));
    }

    // 2. Start Ticker (Checks socket 10 times a second)
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UMeshReceiverSystem::Tick),
        0.1f
    );
}

void UMeshReceiverSystem::Deinitialize()
{
    FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
    if (ListenerSocket)
    {
        ListenerSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenerSocket);
    }
    Super::Deinitialize();
}

bool UMeshReceiverSystem::Tick(float DeltaTime)
{
    //UE_LOG(LogTemp, Warning, TEXT("Mesh Receiver Tick"));
    if (!ListenerSocket) return true;
    //UE_LOG(LogTemp, Warning, TEXT("Mesh Receiver Has Socket"));

    bool bPending = false;
    ListenerSocket->HasPendingConnection(bPending);
    //UE_LOG(LogTemp, Warning, TEXT("Mesh Receiver Pending: %d"), bPending ? 1 : 0);


    if (bPending)
    {
        TSharedRef<FInternetAddr> RemoteAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
        FSocket* Connection = ListenerSocket->Accept(*RemoteAddr, TEXT("BlenderClient"));

        if (Connection)
        {
            UE_LOG(LogTemp, Log, TEXT("--- Connection Accepted ---"));

            // 1. Read Header STRICTLY (12 Bytes)
            TArray<uint8> HeaderBuffer;
            HeaderBuffer.SetNumUninitialized(12);

            if (RecvAll(Connection, HeaderBuffer.GetData(), 12))
            {
                // Parse Header
                int32 Magic, VCount, ICount;
                FMemory::Memcpy(&Magic, HeaderBuffer.GetData(), 4);
                FMemory::Memcpy(&VCount, HeaderBuffer.GetData() + 4, 4);
                FMemory::Memcpy(&ICount, HeaderBuffer.GetData() + 8, 4);

                UE_LOG(LogTemp, Log, TEXT("Header -> Magic: 0x%X | Verts: %d | Indices: %d"), Magic, VCount, ICount);

                if (Magic == 0xDEADBEEF)
                {
                    // 2. Calculate Body Size
                    int32 BodySize = (VCount * 4) + (ICount * 4);

                    TArray<uint8> BodyBuffer;
                    BodyBuffer.SetNumUninitialized(BodySize);

                    // 3. Read Body (Wait for all bytes)
                    UE_LOG(LogTemp, Log, TEXT("Waiting for %d bytes of mesh data..."), BodySize);

                    if (RecvAll(Connection, BodyBuffer.GetData(), BodySize))
                    {
                        // Combine Header + Body for ProcessData (or just pass Body)
                        // Ideally, refactor ProcessData to take VCount/ICount directly.
                        // For now, let's reconstruct the full buffer to match your existing ProcessData
                        TArray<uint8> FullData = HeaderBuffer;
                        FullData.Append(BodyBuffer);

                        ProcessData(FullData);
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("Failed to receive full mesh body!"));
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("Invalid Magic Number! Check sender endianness or protocol."));
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("Failed to receive Header!"));
            }

            Connection->Close();
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Connection);
        }
    }
    return true;
}

void UMeshReceiverSystem::ProcessData(const TArray<uint8>& Data)
{
    // Minimal Size: Header (4+4+4 bytes)
    if (Data.Num() < 12) return;

    // 1. Parse Header
    int32 Magic = 0;
    int32 FloatCount = 0;
    int32 IndexCount = 0;

    FMemory::Memcpy(&Magic, Data.GetData(), 4);

    // Check Magic Number (0xDEADBEEF)
    if (Magic != 0xDEADBEEF)
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid Magic Number"));
        return;
    }

    FMemory::Memcpy(&FloatCount, Data.GetData() + 4, 4);
    FMemory::Memcpy(&IndexCount, Data.GetData() + 8, 4);

    // 2. Parse Body
    int32 VertsBytes = FloatCount * 4;
    int32 IndexBytes = IndexCount * 4;

    if (Data.Num() < 12 + VertsBytes + IndexBytes) return;

    TArray<FVector> Vertices;
    TArray<int32> Indices;

    // Read Verts
    const float* FloatPtr = reinterpret_cast<const float*>(Data.GetData() + 12);
    for (int32 i = 0; i < FloatCount; i += 3)
    {
        // Blender to Unreal Conversion (Flip Y if needed, here we pass raw)
        // Usually: FVector(X, -Y, Z) or FVector(Y, X, Z) depending on preference
        Vertices.Add(FVector(FloatPtr[i], FloatPtr[i + 1], FloatPtr[i + 2]));
    }

    // Read Indices
    const int32* IntPtr = reinterpret_cast<const int32*>(Data.GetData() + 12 + VertsBytes);
    for (int32 i = 0; i < IndexCount; i++)
    {
        Indices.Add(IntPtr[i]);
    }

    // 3. Update Scene
    UpdateSceneMesh(Vertices, Indices);
}

void UMeshReceiverSystem::UpdateSceneMesh(const TArray<FVector>& Vertices, const TArray<int32>& Indices)
{
    if (!GEditor) return;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;

    AActor* TargetActor = nullptr;
    UProceduralMeshComponent* ProcMeshComp = nullptr;

    // 1. Search for existing actor
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->ActorHasTag(FName("BlenderTarget")))
        {
            TargetActor = *It;
            ProcMeshComp = TargetActor->FindComponentByClass<UProceduralMeshComponent>();
            break;
        }
    }

    // 2. If NOT found, Create a new one
    if (!TargetActor)
    {
        UE_LOG(LogTemp, Warning, TEXT("Target not found. Spawning new 'ReceivedMesh' actor..."));

        // Spawn generic Empty Actor
        FActorSpawnParameters SpawnParams;
        SpawnParams.Name = FName("ReceivedMesh");
        TargetActor = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);

        // Set Label in World Outliner
        TargetActor->SetActorLabel(TEXT("ReceivedMesh"));

        // Add Tag
        TargetActor->Tags.Add(FName("BlenderTarget"));
    }

    // 3. If Actor exists but has no Component, create it
    if (!ProcMeshComp)
    {
        // Dynamically create the component
        ProcMeshComp = NewObject<UProceduralMeshComponent>(TargetActor, UProceduralMeshComponent::StaticClass(), FName("ProcMesh"));

        // Register it so it appears in the editor
        ProcMeshComp->RegisterComponent();

        // Set as Root Component if none exists
        if (!TargetActor->GetRootComponent())
        {
            TargetActor->SetRootComponent(ProcMeshComp);
        }
        else
        {
            ProcMeshComp->AttachToComponent(TargetActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
        }
    }

    // 4. Update the Mesh Data
    if (ProcMeshComp)
    {
        ProcMeshComp->ClearAllMeshSections();
        ProcMeshComp->CreateMeshSection_LinearColor(
            0,
            Vertices,
            Indices,
            TArray<FVector>(), // Normals (Empty = Black shading until you calculate them)
            TArray<FVector2D>(), // UVs
            TArray<FLinearColor>(),
            TArray<FProcMeshTangent>(),
            true // Enable Collision
        );

        // Assign a default material if none exists (Optional, makes it visible)
        if (ProcMeshComp->GetMaterial(0) == nullptr)
        {
            UMaterialInterface* BasicMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial"));
            if (BasicMat)
            {
                ProcMeshComp->SetMaterial(0, BasicMat);
            }
        }

        UE_LOG(LogTemp, Log, TEXT("✅ SUCCESS: Mesh Updated (%d Verts)"), Vertices.Num());

        // 5. Force Editor Refresh
        GEditor->RedrawLevelEditingViewports();

        if (ProcMeshComp)
        {
            BakeToStaticMesh(ProcMeshComp);
        }
    }
}
