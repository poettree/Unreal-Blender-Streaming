<p align="center">
  <img src="https://img.shields.io/badge/Blender-v3.0+-F5792A?style=for-the-badge&logo=blender&logoColor=white" alt="Blender Version"/>
  <img src="https://img.shields.io/badge/Unreal%20Engine-5.6-313131?style=for-the-badge&logo=unrealengine&logoColor=white" alt="Unreal Engine"/>
  <img src="https://img.shields.io/badge/License-MIT-2196F3?style=for-the-badge" alt="MIT License"/>
  <img src="https://img.shields.io/badge/Protocol-TCP/IP-4CAF50?style=for-the-badge" alt="TCP/IP Protocol"/>
</p>

<h1 align="center">
  ▣ Blender → Unreal Engine<br/>
  <sub>Real-Time Mesh Streaming</sub>
</h1>

<p align="center">
  <strong>Stream 3D meshes directly from Blender to Unreal Engine in real-time</strong><br/>
  <em>No file exports. No manual imports. Just instant synchronization.</em>
</p>

<br/>

<p align="center">
  <a href="#-features">Features</a> •
  <a href="#-quick-start">Quick Start</a> •
  <a href="#-architecture">Architecture</a> •
  <a href="#-protocol-specification">Protocol</a> •
  <a href="#-api-reference">API Reference</a> •
  <a href="#-roadmap">Roadmap</a>
</p>

---

## ◎ Features

<table>
<tr>
<td width="33%" valign="top">

### ▷ Real-Time Sync

Stream mesh data instantaneously over TCP/IP with zero latency overhead. See your Blender changes in Unreal Engine immediately.

</td>
<td width="33%" valign="top">

### ▷ Auto Conversion

Automatic coordinate system conversion from Blender's right-handed Z-up to Unreal's left-handed Z-up coordinate space.

</td>
<td width="33%" valign="top">

### ▷ Auto Baking

Received meshes are automatically baked to Static Mesh assets in your Content Browser for permanent storage.

</td>
</tr>
<tr>
<td width="33%" valign="top">

### ▷ Zero Config

Works out of the box with sensible defaults. Just run the script in Blender and watch the magic happen.

</td>
<td width="33%" valign="top">

### ▷ Robust Protocol

Binary protocol with magic number validation, error checking, and reliable data transmission.

</td>
<td width="33%" valign="top">

### ▷ Editor Subsystem

Runs as a UE5 Editor Subsystem — always active, no manual initialization required.

</td>
</tr>
</table>

---

## ※ Quick Start

### Prerequisites

| Tool              | Version   | Notes                           |
| ----------------- | --------- | ------------------------------- |
| **Blender**       | 3.0+      | Python API required             |
| **Unreal Engine** | 5.6+      | ProceduralMeshComponent enabled |
| **Network**       | localhost | Default: `127.0.0.1:8080`       |

### Step 1: Set Up Unreal Engine (Receiver)

```bash
# 1. Clone the repository
git clone https://github.com/yourusername/Unreal-Blender-Streaming.git

# 2. Open the UE5 project
cd meshreciever
# Double-click PlayGround.uproject
```

> **※ Tip:** The receiver starts automatically when the editor loads.  
> Look for `Mesh Receiver Listening on Port 8080` in the Output Log.

### Step 2: Send Mesh from Blender (Sender)

```python
# Open Blender's Text Editor and paste this:
import bpy
import sys
sys.path.append(r"path/to/Unreal-Blender-Streaming/blender")

from blender_mesh_sender import send_mesh_to_unreal

# Select a mesh object and run:
send_mesh_to_unreal()
```

<details>
<summary>▸ <b>Alternative: Run from Console</b></summary>

```python
# In Blender's Python console:
exec(open("path/to/blender/blender_mesh_sender.py").read())
send_mesh_to_unreal()
```

</details>

---

## ▦ Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                        BLENDER (SENDER)                               │
│  ┌──────────────┐     ┌───────────────┐     ┌────────────────────┐  │
│  │  Active Mesh │ ──► │ BMesh Extract │ ──► │ Binary Serialize   │  │
│  │  Selection   │     │ + Transform   │     │ (Vertices/Indices) │  │
│  └──────────────┘     └───────────────┘     └─────────┬──────────┘  │
└──────────────────────────────────────────────────────│───────────────┘
                                                       │ TCP/IP
                                                       ▼ Port 8080
┌──────────────────────────────────────────────────────────────────────┐
│                      UNREAL ENGINE (RECEIVER)                         │
│  ┌──────────────────┐     ┌───────────────┐     ┌────────────────┐  │
│  │ MeshReceiverSys  │ ──► │ ProcessData   │ ──► │ ProceduralMesh │  │
│  │ (EditorSubsystem)│     │ (Deserialize) │     │ Component      │  │
│  └──────────────────┘     └───────────────┘     └────────┬───────┘  │
│                                                          │          │
│                           ┌──────────────────────────────▼───────┐  │
│                           │  BakeToStaticMesh → /Game/BakedMeshes │  │
│                           └──────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────────┘
```

---

## ▤ Protocol Specification

### Header Format (12 bytes)

| Offset | Size | Type     | Description                       |
| ------ | ---- | -------- | --------------------------------- |
| `0x00` | 4    | `uint32` | Magic Number (`0xDEADBEEF`)       |
| `0x04` | 4    | `uint32` | Vertex Float Count (vertices × 3) |
| `0x08` | 4    | `uint32` | Index Count                       |

### Body Format

```
┌─────────────────────────────────────────────────────────────┐
│  VERTICES (float32[])  │  INDICES (uint32[])  │  NORMALS   │
│  [x,y,z,x,y,z,...]     │  [i0,i1,i2,...]      │  (reserved) │
└─────────────────────────────────────────────────────────────┘
```

### Coordinate Conversion

| Axis | Blender (RH Z-Up) | Unreal (LH Z-Up) |
| ---- | ----------------- | ---------------- |
| X    | `+X`              | `+X`             |
| Y    | `+Y`              | `-Y` _(flipped)_ |
| Z    | `+Z`              | `+Z`             |

---

## ▧ API Reference

### Blender Python API

```python
def send_mesh_to_unreal(
    host: str = '127.0.0.1',
    port: int = 8080
) -> None:
    """
    Stream the active selected mesh to Unreal Engine.

    Args:
        host: Target IP address (default: localhost)
        port: Target port number (default: 8080)

    Raises:
        Exception: Connection failed or invalid mesh selection
    """
```

### Unreal C++ API

```cpp
UCLASS()
class PLAYGROUND_API UMeshReceiverSystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    // Automatically initialized when editor starts
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    // TCP socket listener on port 8080
    FSocket* ListenerSocket;

    // Process incoming mesh data
    void ProcessData(const TArray<uint8>& Data);

    // Update scene with received mesh
    void UpdateSceneMesh(
        const TArray<FVector>& Vertices,
        const TArray<int32>& Indices
    );

    // Bake procedural mesh to static mesh asset
    void BakeToStaticMesh(UProceduralMeshComponent* ProcMesh);
};
```

---

## ▥ Project Structure

```
Unreal-Blender-Streaming/
├── ▣ blender/
│   └── ○ blender_mesh_sender.py    # Blender addon script
│
├── ▣ meshreciever/                  # UE5 project
│   ├── ▣ Source/
│   │   └── ▣ PlayGround/
│   │       ├── ○ MeshReceiverSystem.h
│   │       └── ○ MeshReceiverSystem.cpp
│   ├── ○ PlayGround.uproject
│   └── ○ PlayGround.sln
│
├── ○ LICENSE                        # MIT License
└── ○ README.md
```

---

## ▒ Roadmap

<table>
<tr><td>

### ● v1.0 — Current

- [x] Real-time mesh streaming
- [x] Automatic coordinate conversion
- [x] Static mesh baking
- [x] ProceduralMesh visualization

</td><td>

### ◐ v1.1 — Planned

- [ ] Material property streaming
- [ ] UV coordinate support
- [ ] Vertex color streaming
- [ ] Multi-object selection

</td></tr>
<tr><td>

### ◑ v2.0 — Future

- [ ] Bidirectional sync
- [ ] Animation data streaming
- [ ] Asset hot-reload
- [ ] Blueprint integration

</td><td>

### ○ v3.0 — Vision

- [ ] AI-assisted mesh optimization
- [ ] LOD generation
- [ ] Collaborative editing
- [ ] Cloud relay support

</td></tr>
</table>

---

## ◇ Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the repository
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

---

## ◈ License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

```
MIT License
Copyright (c) 2026 MyunggyunKim
```

---

<p align="center">
  <strong>Made with ♦ for the 3D development community</strong>
</p>

<p align="center">
  <a href="https://github.com/yourusername/Unreal-Blender-Streaming/issues">Report Bug</a> •
  <a href="https://github.com/yourusername/Unreal-Blender-Streaming/discussions">Request Feature</a>
</p>
