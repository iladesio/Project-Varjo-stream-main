"""
Helper functions.

Source -> https://github.com/jrosebr1/imutils/blob/master/imutils/video/webcamvideostream.py
"""

import datetime
import io
import yaml
from PIL import Image
import sqlite3
import numpy as np
import cv2  # Aggiungi OpenCV per la manipolazione delle immagini
from io import BytesIO

DATETIME_STR_FORMAT = "%Y-%m-%d_%H:%M:%S.%f"


def pil_image_to_byte_array(image):
    img_byte_arr = BytesIO()
    image.save(img_byte_arr, format="JPEG", quality=85)  # Cambia il formato in JPEG
    return img_byte_arr.getvalue()


def pil_image_to_compressed_byte_array(image, format="JPEG", quality=50):
    """
    Convert a PIL image to a byte array, compressing it in the specified format (default JPEG).
    """
    byte_arr = BytesIO()
    image.save(byte_arr, format=format, quality=quality)
    byte_arr = byte_arr.getvalue()
    return byte_arr


def byte_array_to_pil_image(byte_array):
    return Image.open(io.BytesIO(byte_array))


def get_now_string() -> str:
    return datetime.datetime.now().strftime(DATETIME_STR_FORMAT)


def get_config(config_filepath: str) -> dict:
    with open(config_filepath) as f:
        config = yaml.safe_load(f)
    return config


# Create a function to connect to a database with SQLite
def sqlite_connect(db_name: str) -> sqlite3.Connection:
    """Connect to a database if exists. Create an instance if otherwise.
    Args:
        db_name: The name of the database to connect
    Returns:
        an sqlite3.connection object
    """
    try:
        # Create a connection
        conn = sqlite3.connect(db_name)
    except sqlite3.Error:
        print(f"Error connecting to the database '{db_name}'")
    finally:
        return conn


def convert_into_binary(file_path: str):
    with open(file_path, "rb") as file:
        binary = file.read()
    return binary


# Aggiungi le funzioni per la conversione tra byte array e immagini OpenCV


def byte_array_to_cv2_image(byte_array):
    """Converte un byte array in un'immagine OpenCV"""
    np_array = np.frombuffer(byte_array, np.uint8)
    image = cv2.imdecode(np_array, cv2.IMREAD_COLOR)
    return image


def cv2_image_to_byte_array(image, quality=50):
    """Converte un'immagine OpenCV in un byte array"""
    _, buffer = cv2.imencode(".jpg", image, [int(cv2.IMWRITE_JPEG_QUALITY), quality])
    return np.array(buffer).tobytes()


def rotate_image_cv2(image, angle):
    """Ruota un'immagine OpenCV di un certo angolo"""
    (h, w) = image.shape[:2]
    center = (w // 2, h // 2)
    matrix = cv2.getRotationMatrix2D(center, angle, 1.0)
    return cv2.warpAffine(image, matrix, (w, h))


def frame_to_byte_array(frame):
    # Convertire il frame direttamente in JPEG senza passare per PIL
    encode_param = [int(cv2.IMWRITE_JPEG_QUALITY), 85]  # Imposta la qualità del JPEG
    result, encoded_image = cv2.imencode(".jpg", frame, encode_param)
    return encoded_image.tobytes()
