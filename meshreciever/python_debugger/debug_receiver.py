import socket
import struct
import numpy as np
import pyvista as pv
import threading

# Configuration
IP = '127.0.0.1'
PORT = 8080
MAGIC_NUMBER = 0xDEADBEEF

class MeshDebugReceiver:
    def __init__(self):
        self.plotter = pv.Plotter()
        self.plotter.show(interactive_update=True)
        self.running = True

        # Setup Socket
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.bind((IP, PORT))
        self.server.listen(1)
        self.server.settimeout(0.1)  # Non-blocking accept
        
        print(f"✅ Debug Receiver listening on {IP}:{PORT}...")
        print("   Waiting for data from Blender...")

    def update_mesh(self, vertices, indices):
        """
        Update the Pyvista plotter with new mesh data
        """
        # 1. Convert to Numpy
        points = np.array(vertices).reshape(-1, 3)
        
        # 2. Format Indices for Pyvista/VTK
        # Pyvista expects faces as: [n, v1, v2, v3, n, v1, v2, v3...]
        # Since we are receiving triangles, n is always 3.
        raw_indices = np.array(indices).reshape(-1, 3)
        padding = np.full((raw_indices.shape[0], 1), 3) # Create column of 3s
        faces = np.hstack((padding, raw_indices)).flatten() # Merge [3, v1, v2, v3...]

        # 3. Create Mesh
        mesh = pv.PolyData(points, faces)

        # 4. Update Plotter
        self.plotter.clear()
        self.plotter.add_mesh(mesh, color="orange", show_edges=True)
        self.plotter.add_text(f"Verts: {len(points)} | Tris: {len(raw_indices)}", font_size=10)
        self.plotter.reset_camera()
        print("   -> Mesh updated in Viewer.")

    def listen(self):
        while self.running:
            # Keep the window responsive
            self.plotter.update()
            if self.plotter.render_window.GetGenericDisplayId() == 0:
                break # Window closed

            try:
                client, addr = self.server.accept()
                self.handle_client(client)
            except socket.timeout:
                continue # No connection, loop back to update plotter
            except Exception as e:
                print(f"Error: {e}")

    def handle_client(self, client):
        try:
            # 1. Receive Header (12 bytes: Magic + VCount + ICount)
            header_data = self.recv_all(client, 12)
            if not header_data: return

            magic, v_count, i_count = struct.unpack('<III', header_data)

            if magic != MAGIC_NUMBER:
                print(f"❌ Invalid Magic Number: {hex(magic)}")
                return

            # 2. Receive Body
            # Floats = 4 bytes, Ints = 4 bytes
            vert_bytes = v_count * 4
            index_bytes = i_count * 4
            
            # Read Vertices
            vert_data = self.recv_all(client, vert_bytes)
            vertices = struct.unpack(f'<{v_count}f', vert_data)

            # Read Indices
            index_data = self.recv_all(client, index_bytes)
            indices = struct.unpack(f'<{i_count}I', index_data)

            print(f"📦 Received: {v_count//3} Vertices, {i_count//3} Triangles")
            
            # Update Visualization
            self.update_mesh(vertices, indices)

        except Exception as e:
            print(f"❌ Data Error: {e}")
        finally:
            client.close()

    def recv_all(self, sock, count):
        """Helper to ensure we read exactly 'count' bytes"""
        buf = b''
        while len(buf) < count:
            newbuf = sock.recv(count - len(buf))
            if not newbuf: return None
            buf += newbuf
        return buf

if __name__ == "__main__":
    receiver = MeshDebugReceiver()
    try:
        receiver.listen()
    except KeyboardInterrupt:
        print("Stopping...")