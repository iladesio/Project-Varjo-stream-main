#!/bin/bash

# Usage function
function usage() {
    echo "Usage: $0 --dataset_name <name> --num_scenes <number> --yolo_conversion <true|false>"
    echo "Options:"
    echo "  --dataset_name <name>        Specify the dataset name. Required."
    echo "  --num_scenes <number>        Specify the number of scenes. Optional."
    echo "  --yolo_conversion <true|false>      Specify whether to perform YOLO conversion (0 or 1). Optional."
    exit 1
}

# Function to count the number of images in a directory
count_images() {
    local dir=$1
    find "$dir" -type f -name '*.jpg' | wc -l
}

# Use the script directory as main folder
script_dir=$(dirname $0)
cd $script_dir
script_dir=$(pwd)

blenderproc_dir="$(pwd)/../../CustomBlenderproc"

source $blenderproc_dir/venv/bin/activate

# Default number of scenes to generate
num_scenes=100

# Default yolo_conversion is false
yolo_conversion=0

# Parse command-line arguments
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --dataset_name)
            if [[ -n $2 && ! $2 =~ ^- ]]; then
                dataset_name="$2"
                shift
            else
                echo "Error: --dataset_name requires a value."
                usage
            fi
            ;;
        --num_scenes)
            if [[ -n $2 && ! $2 =~ ^- ]]; then
                num_scenes="$2"
                shift
            else
                echo "Error: --num_scenes requires a value."
                usage
            fi
            ;;
        --yolo_conversion)
            if [[ $2 == "true" ]]; then 
                yolo_conversion=1
                shift
            elif [[ $2 == "false" ]]; then
                yolo_conversion=0
                shift
            else
                echo "Error: --yolo_conversion must be either true or false."
                usage
            fi
            ;;
        *)
            echo "Unknown parameter passed: $1"
            usage
            ;;
    esac
    shift
done

if [ -z "$dataset_name" ]; then
    echo "Error: --dataset_name is required."
    usage
fi

if [ ! -d "$blenderproc_dir/datasets/$dataset_name" ]; then
    echo "$blenderproc_dir/datasets/$dataset_name does not exist"
    exit 1
fi

if [ -d "$blenderproc_dir/datasets/ycbv" ]; then
    echo "ATTENTION! The folder datasets/ycbv must be reserved to the script. Remove it, otherwise the script won't work."
    exit 1
fi

cp -r "$blenderproc_dir/datasets/$dataset_name" "$blenderproc_dir/datasets/ycbv"

# Report path
report_path="$blenderproc_dir/GeneratedScenesBop/report.txt"

# Directory paths
base_dir="$blenderproc_dir/GeneratedScenesBop/bop_data/ycbv/train_pbr"
blenderproc_cmd="blenderproc run $blenderproc_dir/generate_ycbv_like.py $blenderproc_dir/datasets $blenderproc_dir/BackgroundTextures $blenderproc_dir/GeneratedScenesBop/ --num_scenes=40"

current_gen_scenes=0

if [ -d $base_dir ]; then
    current_gen_scenes=$(find $base_dir -mindepth 1 -maxdepth 1 -type d | wc -l)
fi

echo "$current_gen_scenes scenes found."

# Generate scenes
for ((i=$current_gen_scenes; i<num_scenes; i++)); do
    
    # Run BlenderProc to generate the scene
    eval $blenderproc_cmd
    
    scene_dir=$(printf "%s/%06d/rgb" "$base_dir" "$i")
    
    # Count the number of images in the scene directory
    num_images=$(count_images "$scene_dir")
    
    echo $num_images >> $report_path

    # Check if the scene contains 1000 images
    if [ "$num_images" -ne 1000 ]; then
        # If not, delete the folder and regenerate the scene
        echo "Scene $i does not contain 1000 images. Regenerating..." >> $report_path
        rm -rf "$base_dir/$(printf '%06d' $i)"
        i=$((i-1))  # Decrement i to retry the current index
    else
        echo "Scene $i generated successfully with 1000 images." >> $report_path
    fi
done

# Refactoring the dataset structure
cd $blenderproc_dir/GeneratedScenesBop/bop_data
mv ycbv/train_pbr/ ./train_synt/
mv ycbv/camera.json ./
rm -rf ycbv
cd ..
mv bop_data/* ./
rm -rf bop_data
mkdir train/
mv train_synt/ train/
mkdir -p test_all/test/

source_dir="train/train_synt"
destination_dir="test_all/test"

# Get the list of folders in the source directory
folders=("$source_dir"/*)

# Calculate the number of folders to move (10% of the total)
total_folders=${#folders[@]}
num_to_move=$((total_folders / 10))

# Check if there are folders to move
if [ "$num_to_move" -gt 0 ]; then
    # Move the last 10% of folders
    for ((i = total_folders - num_to_move; i < total_folders; i++)); do
        mv "${folders[i]}" "$destination_dir"
    done
else
    echo "Not enough folders to move 10%. Moving only 1 folder to the test directory."
    i=$((total_folders-1))
    mv "${folders[i]}" "$destination_dir"
fi

cd $script_dir

# Remove the temporary dataset
rm -rf $blenderproc_dir/datasets/ycbv

# Move the models_info.json into the original dataset
mv $blenderproc_dir/datasets/models_info.json $blenderproc_dir/datasets/$dataset_name

# Create the datasets folder if it doesn't exist
if [[ ! -d "../../Datasets" ]]; then
    mkdir ../../Datasets
fi

# Move the generated dataset into the datasets folder
mv $blenderproc_dir/GeneratedScenesBop ../../Datasets

# Start the yolo conversion if enabled
if [[ $yolo_conversion -eq 1 ]]; then 
    python3 yolo_conversion.py --dataset_name $dataset_name
fi

deactivate