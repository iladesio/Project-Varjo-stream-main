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

import os

# os.system('python -m pip install wandb')

import argparse
from pathlib import Path

import yaml
import numpy as np
from inference_tools.inference_engine import inference, webcam_inference

# import wandb


def get_args_parser():

    parser = argparse.ArgumentParser('Pose Estimation Transformer', add_help=False)

    # Learning
    parser.add_argument('--lr', default=2e-4, type=float)
    parser.add_argument('--lr_backbone_names', default=["backbone.0"], type=str, nargs='+')
    parser.add_argument('--lr_backbone', default=2e-5, type=float)
    parser.add_argument('--lr_linear_proj_names', default=['reference_points', 'sampling_offsets'], type=str, nargs='+')
    parser.add_argument('--lr_linear_proj_mult', default=0.1, type=float)
    parser.add_argument('--batch_size', default=16, type=int)
    parser.add_argument('--eval_batch_size', default=16, type=int, help='Batch size for evaluation')
    parser.add_argument('--weight_decay', default=1e-4, type=float)
    parser.add_argument('--epochs', default=50, type=int)
    parser.add_argument('--lr_drop', default=100, type=int)
    parser.add_argument('--lr_drop_epochs', default=None, type=int, nargs='+')
    parser.add_argument('--clip_max_norm', default=0.1, type=float,
                        help='gradient clipping max norm')

    # * Backbone
    parser.add_argument('--backbone', default='yolov4', type=str, choices=['yolov4', 'maskrcnn', 'fasterrcnn'],
                        help="Name of the convolutional backbone to use")
    parser.add_argument('--backbone_cfg', default='/opt/project/configs/ycbv_yolov4-csp.cfg', type=str,
                        help="Path to the backbone config file to use")
    parser.add_argument('--backbone_weights', default=None, type=str,
                        help="Path to the pretrained weights for the backbone."
                             "None if no weights should be loaded.")
    parser.add_argument('--backbone_conf_thresh', default=0.4, type=float,
                        help="Backbone confidence threshold which objects to keep.")
    parser.add_argument('--backbone_iou_thresh', default=0.5, type=float, help="Backbone IOU threshold for NMS")
    parser.add_argument('--backbone_agnostic_nms', action='store_true',
                        help="Whether backbone NMS should be performed class-agnostic")
    parser.add_argument('--dilation', action='store_true',
                        help="If true, we replace stride with dilation in the last convolutional block (DC5)")
    parser.add_argument('--position_embedding', default='sine', type=str, choices=('sine', 'learned'),
                        help="Type of positional embedding to use on top of the image features")
    parser.add_argument('--position_embedding_scale', default=2 * np.pi, type=float,
                        help="position / size * scale")
    parser.add_argument('--num_feature_levels', default=4, type=int, help='number of feature levels')

    # ** PoET configs
    parser.add_argument('--bbox_mode', default='gt', type=str, choices=('gt', 'backbone', 'jitter'),
                        help='Defines which bounding boxes should be used for PoET to determine query embeddings.')
    parser.add_argument('--reference_points', default='bbox', type=str, choices=('bbox', 'learned'),
                        help='Defines whether the transformer reference points are learned or extracted from the bounding boxes')
    parser.add_argument('--query_embedding', default='bbox', type=str, choices=('bbox', 'learned'),
                        help='Defines whether the transformer query embeddings are learned or determined by the bounding boxes')
    parser.add_argument('--rotation_representation', default='6d', type=str, choices=('6d', 'quat', 'silho_quat'),
                        help="Determine the rotation representation with which PoET is trained.")
    parser.add_argument('--class_mode', default='specific', type=str, choices=('agnostic', 'specific'),
                        help="Determine whether PoET ist trained class-specific or class-agnostic")

    # * Transformer
    parser.add_argument('--enc_layers', default=6, type=int,
                        help="Number of encoding layers in the transformer")
    parser.add_argument('--dec_layers', default=6, type=int,
                        help="Number of decoding layers in the transformer")
    parser.add_argument('--dim_feedforward', default=1024, type=int,
                        help="Intermediate size of the feedforward layers in the transformer blocks") ###############################
    parser.add_argument('--hidden_dim', default=256, type=int,
                        help="Size of the embeddings (dimension of the transformer)") ######################################
    parser.add_argument('--dropout', default=0.1, type=float,
                        help="Dropout applied in the transformer")
    parser.add_argument('--nheads', default=8, type=int,
                        help="Number of attention heads inside the transformer's attentions")
    parser.add_argument('--num_queries', default=10, type=int,
                        help="Number of query slots")
    parser.add_argument('--dec_n_points', default=4, type=int)
    parser.add_argument('--enc_n_points', default=4, type=int)

    # * Matcher
    parser.add_argument('--matcher_type', default='pose', choices=['pose'], type=str)
    parser.add_argument('--set_cost_class', default=1, type=float,
                        help="Class coefficient in the matching cost")
    parser.add_argument('--set_cost_bbox', default=1, type=float,
                        help="L1 box coefficient in the matching cost")
    parser.add_argument('--set_cost_giou', default=2, type=float,
                        help="giou box coefficient in the matching cost")

    # * Loss
    parser.add_argument('--no_aux_loss', dest='aux_loss', action='store_false',
                        help="Disables auxiliary decoding losses (loss at each layer)")
    # * Loss coefficients
    # Pose Estimation losses
    parser.add_argument('--translation_loss_coef', default=1, type=float, help='Loss weighing parameter for the translation')
    parser.add_argument('--rotation_loss_coef', default=1, type=float, help='Loss weighing parameter for the rotation')

    # dataset parameters
    parser.add_argument('--dataset', default='ycbv', type=str, choices=('ycbv', 'lmo'),
                        help="Choose the dataset to train/evaluate PoET on.")
    parser.add_argument('--dataset_path', default='/data', type=str,
                        help='Path to the dataset ')
    parser.add_argument('--train_set', default="train", type=str, help="Determine on which dataset split to train")
    parser.add_argument('--eval_set', default="test", type=str, help="Determine on which dataset split to evaluate")
    parser.add_argument('--synt_background', default=None, type=str,
                        help="Directory containing the background images from which to sample")
    parser.add_argument('--n_classes', default=21, type=int, help="Number of classes present in the dataset")
    parser.add_argument('--jitter_probability', default=0.5, type=float,
                        help='If bbox_mode is set to jitter, this value indicates the probability '
                             'that jitter is applied to a bounding box.')
    parser.add_argument('--rgb_augmentation', action='store_true',
                        help='Activate image augmentation for training pose estimation.')
    parser.add_argument('--grayscale', action='store_true', help='Activate grayscale augmentation.')

    # * Evaluator
    parser.add_argument('--eval_interval', type=int, default=10,
                        help="Epoch interval after which the current model is evaluated")
    parser.add_argument('--class_info', type=str, default='/annotations/classes.json',
                        help='path to .txt-file containing the class names')
    parser.add_argument('--models', type=str, default='/models_eval/',
                        help='path to a directory containing the classes models')
    parser.add_argument('--model_symmetry', type=str, default='/annotations/symmetries.json',
                        help='path to .json-file containing the class symmetries')

    # * Inference
    parser.add_argument('--inference', action='store_true',
                        help="Flag indicating that PoET should be launched in inference mode.")
    parser.add_argument('--inference_path', type=str,
                        help="Path to the directory containing the files for inference.")
    parser.add_argument('--inference_output', type=str,
                        help="Path to the directory where the inference results should be stored.")

    # * Misc
    parser.add_argument('--sgd', action='store_true')
    parser.add_argument('--save_interval', default=5, type=int,
                        help="Epoch interval after which the current checkpoint will be stored")
    parser.add_argument('--output_dir', default='',
                        help='path where to save, empty for no saving')
    parser.add_argument('--device', default='cuda',
                        help='device to use for training / testing')
    parser.add_argument('--seed', default=42, type=int)
    parser.add_argument('--resume', default='', help='resume from checkpoint')
    parser.add_argument('--start_epoch', default=0, type=int, metavar='N',
                        help='start epoch')
    parser.add_argument('--eval', action='store_true', help='Run model in evaluation mode')
    parser.add_argument('--eval_bop', action='store_true', help="Run model in BOP challenge evaluation mode")
    parser.add_argument('--num_workers', default=1, type=int)
    parser.add_argument('--cache_mode', default=False, action='store_true', help='whether to cache images on memory')

    # * Distributed training parameters
    parser.add_argument('--distributed', action='store_true',
                        help='Use multi-processing distributed training to launch ')
    parser.add_argument('--world_size', default=3, type=int,
                        help='number of distributed processes/ GPUs to use')
    parser.add_argument('--dist_url', default='env://',
                        help='url used to set up distributed training')
    parser.add_argument('--dist_backend', default='nccl', type=str,
                        help='distributed backend') 
    parser.add_argument('--local_rank', default=0, type=int,
                        help='rank of the process')     
    parser.add_argument('--gpu', default=0, type=int, help='rank of the process')

    parser.add_argument('--webcam', default=True, type=int, help='rank of the process')

    return parser


if __name__ == '__main__':
    parser = argparse.ArgumentParser('PoET training and evaluation script', parents=[get_args_parser()])
    args = parser.parse_args()

    configs_path = '/opt/project/configs/poet_run_configs.yml'
    with open(configs_path, 'r') as f:
        configs = yaml.safe_load(f)

    for key, value in configs['inference_configs'].items():
        if hasattr(args, key):
            setattr(args, key, value)
    
    if args.output_dir:
        Path(args.output_dir).mkdir(parents=True, exist_ok=True)

    if args.webcam == True:
        webcam_inference(args)
    else:
        inference(args)