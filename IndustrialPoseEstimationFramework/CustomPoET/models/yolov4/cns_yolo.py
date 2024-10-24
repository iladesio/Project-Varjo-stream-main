# Copyright (c) 2022 University of Klagenfurt - Control of Networked Systems (CNS). All Rights Reserved.
# Author: Thomas Jantos (thomas.jantos@aau.at)

from collections import OrderedDict

import torch
import torch.nn.functional as F
from torch import Tensor
from typing import Dict, Optional, List

from .yolo.backbone_models.models import Darknet, load_darknet_weights
from .yolo.yolo_utils.general import non_max_suppression
from .yolo.yolo_utils.torch_utils import select_device

ONNX_EXPORT = False


class CNSYOLO(Darknet):
    """
    YOLOv4 Object Detector.
    Returns detected/predicted objects (class, bounding box) and the feature maps.
    """
    def __init__(self, args, train_backbone: bool, return_interm_layers: bool, return_layers=[142, 157, 172]):

        # TODO: Check whether Imagesize argument is needed
        super().__init__(args.backbone_cfg)

        self.return_iterm_layers = return_interm_layers

        if return_interm_layers:
            self.return_layers = {str(i): v for i, v in enumerate(return_layers)}
            self.num_channels = [x[0].out_channels for x in [self.module_list[i] for i in return_layers]]
            self.strides = [x[0].stride[0] for x in [self.module_list[i] for i in return_layers]]
        else:
            self.return_layers = {'0': 172}
            self.num_channels = self.module_list[return_layers[0]].out_channels
            self.strides = self.module_list[return_layers[0]].strides

        # Set threshold parameters
        self.conf_thres = args.backbone_conf_thresh
        self.iou_thres = args.backbone_iou_thresh
        self.agnostic_nms = args.backbone_agnostic_nms

        # Freeze backbone if it should not be trained
        self.train_backbone = train_backbone
        if not train_backbone:
            for name, parameter in self.named_parameters():
                parameter.requires_grad_(False)

    def forward_backbone(self, x, verbose=False):
        yolo_out, out = [], []
        intermediate = OrderedDict()
        intermediate_i = 0
        if verbose:
            print('0', x.shape)
            str_o = ''

        # Passing the image through the YOLO model layer by layer
        for i, module in enumerate(self.module_list):
            name = module.__class__.__name__
            if name in ['WeightedFeatureFusion', 'FeatureConcat', 'FeatureConcat2', 'FeatureConcat3',
                        'FeatureConcat_l']:  # sum, concat
                if verbose:
                    l = [i - 1] + module.layers  # layers
                    sh = [list(x.shape)] + [list(out[i].shape) for i in module.layers]  # shapes
                    str_o = ' >> ' + ' + '.join(['layer %g %s' % x for x in zip(l, sh)])
                x = module(x, out)  # WeightedFeatureFusion(), FeatureConcat()
            elif name == 'YOLOLayer':
                yolo_out.append(module(x, out))
            else:  # run module directly, i.e. mtype = 'convolutional', 'upsample', 'maxpool', 'batchnorm2d' etc.
                x = module(x)

            if i in self.return_layers.values():
                intermediate[str(intermediate_i)] = x
                intermediate_i += 1

            out.append(x if self.routs[i] else [])
            if verbose:
                print('%g/%g %s -' % (i, len(self.module_list), name), list(x.shape), str_o)
                str_o = ''

        if self.training:
            # TODO: Write code when backbone is not frozen
            # We want to return the same as the original yolo, but also the predicted outputs as we need them for further processing
            raise NotImplementedError
        else:
            x, p = zip(*yolo_out)  # inference output, training output
            x = torch.cat(x, 1)  # cat yolo outputs
            # Determine prediction from yolo output layers: pred = [bbox (4), conf, class]
            pred = non_max_suppression(x, self.conf_thres, self.iou_thres, classes=None,
                                       agnostic=self.agnostic_nms)
        return pred, intermediate

    def forward_once(self, tensor_list, augment=False, verbose=False):
        # Pass Image through YOLO
        predictions, xs = self.forward_backbone(tensor_list.tensors)
        # Adjust predicted classes by 1 as class 0 is "background / dummy" in PoET
        for i in range(len(predictions)):
            if predictions[i] is not None:
                predictions[i][:, 5] += 1
        out: Dict[str, NestedTensor] = {}
        for name, x in xs.items():
            m = tensor_list.mask
            assert m is not None
            mask = F.interpolate(m[None].float(), size=x.shape[-2:]).to(torch.bool)[0]
            out[name] = NestedTensor(x, mask)
        return predictions, out


class NestedTensor(object):
    def __init__(self, tensors, mask: Optional[Tensor]):
        self.tensors = tensors
        self.mask = mask

    def to(self, device, non_blocking=False):
        cast_tensor = self.tensors.to(device, non_blocking=non_blocking)
        mask = self.mask
        if mask is not None:
            assert mask is not None
            cast_mask = mask.to(device, non_blocking=non_blocking)
        else:
            cast_mask = None
        return NestedTensor(cast_tensor, cast_mask)

    def record_stream(self, *args, **kwargs):
        self.tensors.record_stream(*args, **kwargs)
        if self.mask is not None:
            self.mask.record_stream(*args, **kwargs)

    def decompose(self):
        return self.tensors, self.mask

    def __repr__(self):
        return str(self.tensors)


def build_cns_yolo(args):
    train_backbone = args.lr_backbone > 0
    return_interm_layers = (args.num_feature_levels > 1)
    cns_yolo = CNSYOLO(args, train_backbone, return_interm_layers)
    if args.backbone_weights is not None:
        try:
            cns_yolo.load_state_dict(torch.load(args.backbone_weights, map_location=select_device())['model'])
        except:
            load_darknet_weights(cns_yolo, args.backbone_weights)
    return cns_yolo
