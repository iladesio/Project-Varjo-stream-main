# ------------------------------------------------------------------------
# PoET: Pose Estimation Transformer for Single-View, Multi-Object 6D Pose Estimation
# Copyright (c) 2022 Thomas Jantos (thomas.jantos@aau.at), University of Klagenfurt - Control of Networked Systems (CNS). All Rights Reserved.
# Licensed under the BSD-2-Clause-License with no commercial use [see LICENSE for details]
# ------------------------------------------------------------------------
# Modified from Deformable DETR (https://github.com/fundamentalvision/Deformable-DETR)
# Copyright (c) 2020 SenseTime. All Rights Reserved.
# Licensed under the Apache License, Version 2.0 [see LICENSE_DEFORMABLE_DETR in the LICENSES folder for details]
# ------------------------------------------------------------------------

import torch


def to_cuda(samples, targets, device):
    samples = samples.to(device, non_blocking=True)
    if targets is not None:
        # Only write targets to cuda device if not None, otherwise return None
        targets = [{k: v.to(device, non_blocking=True) for k, v in t.items()} for t in targets]
    return samples, targets


class data_prefetcher:
    def __init__(self, loader, device, prefetch=True):
        self.loader = loader
        self.device = device
        self.prefetch = prefetch
        self.stream = torch.cuda.Stream() if prefetch else None
        self.reset()

    def reset(self):
        self.loader_iter = iter(self.loader)
        if self.prefetch:
            self.preload()

    def preload(self):
        try:
            self.next_samples, self.next_targets = next(self.loader_iter)
        except StopIteration:
            self.next_samples = None
            self.next_targets = None
            return
        with torch.cuda.stream(self.stream):
            self.next_samples, self.next_targets = to_cuda(self.next_samples, self.next_targets, self.device)

    def next(self):
        if self.prefetch:
            torch.cuda.current_stream().wait_stream(self.stream)
            samples = self.next_samples
            targets = self.next_targets
            self.preload()
        else:
            try:
                samples, targets = next(self.loader_iter)
                samples, targets = to_cuda(samples, targets, self.device)
            except StopIteration:
                samples = None
                targets = None
        return samples, targets
