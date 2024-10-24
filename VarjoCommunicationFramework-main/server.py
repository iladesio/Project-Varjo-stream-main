import socket
import struct
import cv2
import numpy as np
from pathlib import Path

def receive_and_save_image(client_socket, save_path):
    # Receive the size of the incoming image data
    print("Receiving image size...")
    packed_msg_size = client_socket.recv(8)  # 8 bytes for the size (uint64_t)
    if not packed_msg_size:
        print("No data received for image size.")
        return False

    msg_size = struct.unpack("Q", packed_msg_size)[0]

    # Acknowledge receipt of image size
    client_socket.sendall(b'SZE')

    # Receive the actual image data
    print("Receiving image data...")
    data = b""
    while len(data) < msg_size:
        packet = client_socket.recv(4096)
        if not packet:
            print("No more data received during image data reception.")
            return False
        data += packet

    print("Image data received successfully.")

    # Acknowledge receipt of the full image
    client_socket.sendall(b'IMG')

    # Decode the image data (PNG format) using OpenCV
    nparr = np.frombuffer(data, np.uint8)
    frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

    if frame is None:
        print("Failed to decode the image.")
        return False

    # Get the dimensions of the frame
    height, width, _ = frame.shape

    # Crop the frame to accommodate the model input size
    if height != 480 or width != 640:
        start_x = max(0, width // 2 - 320)
        start_y = max(0, height // 2 - 240)
        frame = frame[start_y:start_y + 480, start_x:start_x + 640]

    # Save the image to the specified path
    save_path = Path(save_path)
    save_path.parent.mkdir(parents=True, exist_ok=True)
    frame = cv2.cvtColor(frame, cv2.COLOR_RGBA2BGR)
    cv2.imwrite(str(save_path), frame)
    print(f"Image saved to {save_path}")
    return True

def webcam_inference():
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind(('0.0.0.0', 9999))
    server_socket.listen(1)
    print("Waiting for a connection...")
    client_socket, addr = server_socket.accept()
    print("Connected to:", addr)

    count = 0
    while True:
        image_path = f'tmp/{count}.png'

        if not receive_and_save_image(client_socket, image_path):
            print("Failed to receive image")
            break

        count += 1

if __name__ == '__main__':
    webcam_inference()
