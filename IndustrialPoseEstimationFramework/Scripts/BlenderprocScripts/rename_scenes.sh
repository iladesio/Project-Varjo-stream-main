#!/bin/bash
cd $(dirname $0)

start_from=195
cp -r ../GeneratedScenesBop ../GeneratedScenesBopCopy
cd ../GeneratedScenesBopCopy/bop_data/ycbv/train_pbr

i=$start_from
for scene_dir in ./*; do 

    echo $scene_dir " into " $i
    new_dir_name=$(printf "%06d" "$i")
    mv $scene_dir $new_dir_name 
    i=$((i+1))

done