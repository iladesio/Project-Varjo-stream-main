# ------------------------------------------------------------------------
# PoET: Pose Estimation Transformer for Single-View, Multi-Object 6D Pose Estimation
# Copyright (c) 2022 Thomas Jantos (thomas.jantos@aau.at), University of Klagenfurt - Control of Networked Systems (CNS). All Rights Reserved.
# Licensed under the BSD-2-Clause-License with no commercial use [see LICENSE for details]
# ------------------------------------------------------------------------
# Modified from Deformable DETR (https://github.com/fundamentalvision/Deformable-DETR)
# Copyright (c) 2020 SenseTime. All Rights Reserved.
# Licensed under the Apache License, Version 2.0 [see LICENSE_DEFORMABLE_DETR in the LICENSES folder for details]
# ------------------------------------------------------------------------
# Modified from DETR (https://github.com/facebookresearch/detr)
# Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved
# ------------------------------------------------------------------------

import json
import torch
import util.misc as utils
import cv2
import numpy as np
import os
import pickle
import struct
import socket
from pathlib import Path
import shutil

from data_utils.data_prefetcher import data_prefetcher
from models import build_model
from inference_tools.dataset import build_dataset
from torch.utils.data import DataLoader, SequentialSampler

def draw_axes(img, center, rot, scale=50):
    # Define unit vectors for axes in 3D
    axes = np.array([[scale, 0, 0], [0, scale, 0], [0, 0, scale]])
    point_center = np.array([center[0], center[1], 0])  # Add a dummy zero for the 3D center

    # Colors for the axes: Red for X, Green for Y, Blue for Z
    colors = [(0, 0, 255), (0, 255, 0), (255, 0, 0)]

    # Transform axes using the rotation matrix
    transformed_axes = rot.dot(axes.T).T

    for i, color in enumerate(colors):
        # Calculate the end point of each axis in 3D, then drop the z-component to project it to 2D
        axis_end = point_center + transformed_axes[i]
        cv2.line(img, tuple(point_center[:2].astype(int)), tuple(axis_end[:2].astype(int)), color, 2)




def inference(args):
    """
    Script for Inference with PoET. The datalaoder loads all the images and then iterates over them. PoET processes each
    image and stores the detected objects and their poses in a JSON file. Currently, this script allows only batch sizes
    of 1.
    """
    if not os.path.exists(args.inference_output):
        os.makedirs(args.inference_output)

    device = torch.device(args.device)
    model, criterion, matcher = build_model(args)
    model.to(device)
    model.eval()

    # Load model weights
    checkpoint = torch.load(args.resume, map_location='cpu')
    model.load_state_dict(checkpoint['model'], strict=False)

    # Construct dataloader that loads the images for inference
    dataset_inference = build_dataset(args)
    sampler_inference = SequentialSampler(dataset_inference)
    data_loader_inference = DataLoader(dataset_inference, 1, sampler=sampler_inference,
                                       drop_last=False, collate_fn=utils.collate_fn, num_workers=0,
                                       pin_memory=True)

    prefetcher = data_prefetcher(data_loader_inference, device, prefetch=False)
    samples, targets = prefetcher.next()
    results = {}
    # Iterate over all images, perform pose estimation and store results.
    for i, idx in enumerate(range(len(data_loader_inference))):
        print("Processing {}/{}".format(i, len(data_loader_inference) - 1))
        outputs, n_boxes_per_sample = model(samples, targets)

        # Iterate over all the detected predictions
        img_file = data_loader_inference.dataset.image_paths[i]
        img_id = img_file[img_file.find("_")+1:img_file.rfind(".")]
        img = cv2.imread(f'/data/0000/{img_file}')
        height, width, _ = img.shape

        results[img_id] = {}
        for d in range(n_boxes_per_sample[0]):
            pred_t = outputs['pred_translation'][0][d].detach().cpu().tolist()
            pred_rot = outputs['pred_rotation'][0][d].detach().cpu().tolist()
            pred_rot_numpy = outputs['pred_rotation'][0][d].detach().cpu().numpy()
            pred_box = outputs['pred_boxes'][0][d].detach().cpu().tolist()
            pred_class = outputs['pred_classes'][0][d].detach().cpu().tolist()
            results[img_id][d] = {
                "t": pred_t,
                "rot": pred_rot,
                "box": pred_box,
                "class": pred_class
            }

    
            x_c, y_c, w, h = pred_box
            x1 = int((x_c - w/2) * width)
            y1 = int((y_c - h/2) * height)
            x2 = int((x_c + w/2) * width)
            y2 = int((y_c + h/2) * height)

            cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 2)  # Draw Rectangle
            center = np.array([(x1 + x2) // 2, (y1 + y2) // 2])
            draw_axes(img, center, pred_rot_numpy)
            cv2.imwrite(os.path.join(args.inference_output, f"{img_file[:-4]}_predicted.png"), img)


        samples, targets = prefetcher.next()

    # Store the json-file
    out_file_name = "results.json"
    with open(args.inference_output + '/' + out_file_name, 'w') as out_file:
        json.dump(results, out_file)
    return



def send_image(sock, img):
    print('Sending frame to client')
    data = pickle.dumps(img)
    message_size = struct.pack("Q", len(data))
    sock.sendall(message_size + data)

def receive_and_save_image(client_socket, save_path):
    data = b""
    payload_size = struct.calcsize("Q")

    while len(data) < payload_size:
        packet = client_socket.recv(4096)
        if not packet:
            return False  # No more data received
        data += packet

    if len(data) >= payload_size:
        packed_msg_size = data[:payload_size]
        data = data[payload_size:]
        msg_size = struct.unpack("Q", packed_msg_size)[0]

        while len(data) < msg_size:
            data += client_socket.recv(4096)

        frame_data = data[:msg_size]
        frame = pickle.loads(frame_data)

        # Get the dimensions of the frame
        height, width, _ = frame.shape

        # Crop the frame to accomodate the model input size
        if height != 480 or width != 640:
            # Calculate the starting point for cropping (center crop)
            start_x = width//2 - 320
            start_y = height//2 - 240

            # Ensure the cropping area is within the frame dimensions
            start_x = max(0, start_x)
            start_y = max(0, start_y)

            # Crop the frame to 640x480
            frame = frame[start_y:start_y + 480, start_x:start_x + 640]

        # Save the image to the specified path
        save_path = Path(save_path)
        save_path.parent.mkdir(parents=True, exist_ok=True)
        cv2.imwrite(str(save_path), frame)
        return True

    return False

def webcam_inference(args):
    device = torch.device(args.device)
    model, criterion, matcher = build_model(args)
    model.to(device)
    model.eval()

    # Load model weights
    checkpoint = torch.load(args.resume, map_location='cpu')
    model.load_state_dict(checkpoint['model'], strict=False)

    # Set up socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.bind(('0.0.0.0', 9999))  # Bind to all interfaces and port 9999
    server_socket.listen(1)
    print("Waiting for a connection...")
    client_socket, addr = server_socket.accept()
    print("Connected to:", addr)

    # Path to save the received image
    image_path = Path(args.inference_path) / '0.png'
    # Receive and save the new image
    if receive_and_save_image(client_socket, image_path):
        print(f"Image saved to {image_path}")
    else:
        print("Failed to receive image")
        return

    print('Instanciating the dataset')

    # Construct dataloader that loads the images for inference
    dataset_inference = build_dataset(args)
    sampler_inference = SequentialSampler(dataset_inference)
    data_loader_inference = DataLoader(dataset_inference, 1, sampler=sampler_inference,
                                       drop_last=False, collate_fn=utils.collate_fn, num_workers=0,
                                       pin_memory=True)

    prefetcher = data_prefetcher(data_loader_inference, device, prefetch=False)

    count = 1

    print('Starting inference')
    while True:        
        # Reset the prefetcher to ensure the new image is loaded
        prefetcher.reset()

        # Perform inference on the new image
        samples, targets = prefetcher.next()
        if samples is None:
            print("No samples to process")
            break

        outputs, n_boxes_per_sample = model(samples, None)

        # Load the image for drawing
        img = cv2.imread(str(image_path))
        height, width, _ = img.shape

        for d in range(n_boxes_per_sample[0]):
            pred_t = outputs['pred_translation'][0][d].detach().cpu().tolist()
            pred_rot = outputs['pred_rotation'][0][d].detach().cpu().tolist()
            pred_rot_numpy = outputs['pred_rotation'][0][d].detach().cpu().numpy()
            pred_box = outputs['pred_boxes'][0][d].detach().cpu().tolist()
            pred_class = outputs['pred_classes'][0][d].detach().cpu().tolist()
    
            x_c, y_c, w, h = pred_box
            x1 = int((x_c - w/2) * width)
            y1 = int((y_c - h/2) * height)
            x2 = int((x_c + w/2) * width)
            y2 = int((y_c + h/2) * height)

            cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 2)  # Draw Rectangle
            center = np.array([(x1 + x2) // 2, (y1 + y2) // 2])
            draw_axes(img, center, pred_rot_numpy)

        # Send the image back to the client
        send_image(client_socket, img)

        # Receive and save the new image
        if not receive_and_save_image(client_socket, image_path):
            print("Failed to receive image")
            break