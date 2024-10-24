import socket
import cv2
import pickle
import struct
import os

SCRIPT_DIR = os.path.dirname(__file__)

def client():

    if not os.path.exists(f'{SCRIPT_DIR}/../../CustomPoET/InferenceOutput'):
        os.mkdir(f'{SCRIPT_DIR}/../../CustomPoET/InferenceOutput')
    video_path = f'{SCRIPT_DIR}/../../CustomPoET/InferenceOutput/webcam_video_inference.mp4'

    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.connect(('localhost', 9999))

    cap = cv2.VideoCapture(0)

    count = 0
    while cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            break

        # Send frame to server
        data = pickle.dumps(frame)
        message_size = struct.pack("Q", len(data))
        client_socket.sendall(message_size + data)

        # Receive processed frame from server
        data = b""
        payload_size = struct.calcsize("Q")

        while len(data) < payload_size:
            packet = client_socket.recv(4096)
            if not packet:
                break
            data += packet

        if len(data) < payload_size:
            break

        packed_msg_size = data[:payload_size]
        data = data[payload_size:]
        msg_size = struct.unpack("Q", packed_msg_size)[0]

        while len(data) < msg_size:
            data += client_socket.recv(4096)

        frame_data = data[:msg_size]
        frame = pickle.loads(frame_data)

        cv2.imwrite(f'{SCRIPT_DIR}/../../CustomPoET/InferenceOutput/{count}.png', frame)
        count += 1

        cv2.imshow('Processed Frame', frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cap.release()
    client_socket.close()
    cv2.destroyAllWindows()

    inference_frames = os.listdir(f'{SCRIPT_DIR}/../../CustomPoET/InferenceOutput')
    inference_frames.sort()

    fourcc = cv2.VideoWriter_fourcc(*'mp4v')
    out = cv2.VideoWriter(video_path, fourcc=fourcc, fps=30, frameSize=(640, 480), isColor=True)

    for frame_name in inference_frames:
        frame = cv2.imread(f'{SCRIPT_DIR}/../../CustomPoET/InferenceOutput/{frame_name}')
        out.write(frame)

    out.release()
    print(f'Video saved in {video_path}')

if __name__ == "__main__":
    client()
