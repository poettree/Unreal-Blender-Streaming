import bpy
import socket
import struct
import bmesh
from typing import List, Optional
from dataclasses import dataclass

#-------------------------------------------------------------------------------
#   region  [ CONFIGURATION ]
#-------------------------------------------------------------------------------
DEFAULT_HOST = '127.0.0.1'
DEFAULT_PORT = 8080
MAGIC_NUMBER = 0xDEADBEEF

@dataclass
class MeshData:
    name: str
    vertices: List[float]
    normals: List[float]
    indices: List[int]

#-------------------------------------------------------------------------------
#   region  [ GEOMETRY PROCESSING ]
#-------------------------------------------------------------------------------
def _validate_active_object() -> Optional[bpy.types.Object]:
    obj = bpy.context.active_object
    if not obj or obj.type != 'MESH':
        return None
    if bpy.context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    return obj

def _extract_mesh_data(obj: bpy.types.Object) -> MeshData:
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.transform(obj.matrix_world)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    
    # CRITICAL: Fix Index Ordering
    bm.verts.ensure_lookup_table()
    bm.verts.index_update() 

    vertices = []
    normals = []

    # Blender (RH Z-Up) -> Unreal (LH Z-Up): Swap Y, Scale cm
    for v in bm.verts:
        vertices.extend([v.co.x , -v.co.y , v.co.z ])
        normals.extend([v.normal.x, -v.normal.y, v.normal.z])

    indices = []
    for f in bm.faces:
        idx = [v.index for v in f.verts]
        indices.extend([idx[0], idx[1], idx[2]])

    bm.free()
    return MeshData(name=obj.name, vertices=vertices, normals=normals, indices=indices)

#-------------------------------------------------------------------------------
#   region  [ SERIALIZATION - FIXED FOR UNREAL CORE ]
#-------------------------------------------------------------------------------
def _serialize_payload(data: MeshData) -> bytes:
    """
    FIXED PROTOCOL:
    1. Header uses TOTAL FLOAT COUNT (len(vertices)), not Vertex Count.
    2. Body Order is Vertices -> Indices -> Normals.
       (Your C++ reads Indices immediately after Vertices, skipping Normals)
    """
    
    # Use len(data.vertices) which is (VertexCount * 3)
    # This matches your C++ reader's expectation from Error #2
    v_float_count = len(data.vertices) 
    i_count = len(data.indices)

    # 1. Pack Header
    header = struct.pack('<III', MAGIC_NUMBER, v_float_count, i_count)

    # 2. Pack Body
    # 'f' = float, 'I' = uint32
    vert_body = struct.pack(f'<{len(data.vertices)}f', *data.vertices)
    norm_body = struct.pack(f'<{len(data.normals)}f', *data.normals) 
    index_body = struct.pack(f'<{len(data.indices)}I', *data.indices)

    # REORDERED: Header -> Vertices -> INDICES -> Normals
    # This ensures C++ reads integers when it expects integers.
    return header + vert_body + index_body + norm_body

#-------------------------------------------------------------------------------
#   region  [ NETWORKING & MAIN ]
#-------------------------------------------------------------------------------
def _transmit_bytes(host: str, port: int, payload: bytes) -> None:
    print(f"▣ Connecting to {host}:{port}...")
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as client:
            client.settimeout(5.0)
            client.connect((host, port))
            client.sendall(payload)
            print(f"▣ Success! Sent {len(payload)} bytes.")
    except Exception as e:
        print(f"X Network Error: {e}")

def send_mesh_to_unreal(host: str = DEFAULT_HOST, port: int = DEFAULT_PORT) -> None:
    obj = _validate_active_object()
    if not obj:
        print("X Error: Select a Mesh object.")
        return

    try:
        print(f"▣ Processing '{obj.name}'...")
        mesh_data = _extract_mesh_data(obj)
        payload = _serialize_payload(mesh_data)
        _transmit_bytes(host, port, payload)
    except Exception as e:
        print(f"X Error: {e}")

if __name__ == "__main__":
    send_mesh_to_unreal()