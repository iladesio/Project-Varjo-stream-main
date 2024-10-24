"""
Capture frames from a camera using OpenCV and publish on an MQTT topic.
"""

import os
import time
import cv2

from helpers import get_config, get_now_string, frame_to_byte_array
from imutils.video import VideoStream
from mqtt import get_mqtt_client

CONFIG_FILE_PATH = os.getenv("MQTT_CAMERA_CONFIG", "./config/config.yml")
CONFIG = get_config(CONFIG_FILE_PATH)

MQTT_BROKER = CONFIG["mqtt"]["broker"]
MQTT_PORT = CONFIG["mqtt"]["port"]
MQTT_QOS = CONFIG["mqtt"]["QOS"]

MQTT_TOPIC_CAMERA = CONFIG["camera"]["mqtt_topic"]
VIDEO_SOURCE = CONFIG["camera"]["video_source"]
FPS = CONFIG["camera"]["fps"]


def main():
    client = get_mqtt_client()
    client.connect(MQTT_BROKER, port=MQTT_PORT)
    time.sleep(4)  # Wait for connection setup to complete
    client.loop_start()

    # Open camera
    camera = VideoStream(src=VIDEO_SOURCE, framerate=FPS).start()
    time.sleep(2)  # Webcam light should come on if using one

    while True:
        frame = camera.read()

        # Convert frame from BGR to RGB as OpenCV uses BGR by default
        frame_RGB = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

        # Get original dimensions of the frame
        original_height, original_width = frame_RGB.shape[:2]

        # Set the desired width while keeping the aspect ratio
        new_width = 640  # Desired width
        aspect_ratio = original_width / original_height
        new_height = int(
            new_width / aspect_ratio
        )  # Calculate height maintaining aspect ratio

        # Resize the frame to maintain aspect ratio
        frame_resized = cv2.resize(frame_RGB, (new_width, new_height))

        # Convert the resized frame directly to byte array using the frame_to_byte_array function
        byte_array = frame_to_byte_array(frame_resized)

        # Publish the frame to the MQTT broker
        client.publish(MQTT_TOPIC_CAMERA, byte_array, qos=MQTT_QOS)

        now = get_now_string()
        print(f"published frame on topic: {MQTT_TOPIC_CAMERA} at {now}")
        time.sleep(1 / FPS)


if __name__ == "__main__":
    main()
