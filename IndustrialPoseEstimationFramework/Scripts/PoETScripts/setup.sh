#!/bin/bash

function usage() {
    echo "Usage: $0 --dataset_name <name>"
    exit 1
}

cd $(dirname $0)

copy_ply=0

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
        *)
            echo "Unknown parameter passed: $1"
            usage
            ;;
    esac
    shift
done

annotations_path="../../CustomPoET/PoetDataset/annotations/$dataset_name"
models_eval_path="../../CustomPoET/PoetDataset/models_eval/$dataset_name"

# Create the additional directories
mkdir -p $annotations_path
mkdir -p $models_eval_path

echo "Created the necessary directories."

# Copy the .ply files from Blenderproc dataset
if [[ -d "../../CustomBlenderproc/datasets/$dataset_name/models/" ]]; then
    cp ../../CustomBlenderproc/datasets/$dataset_name/models/*.ply $models_eval_path
    echo "All .ply have been copied."
    cp ../../CustomBlenderproc/datasets/$dataset_name/models/models_info.json $models_eval_path
else   
    echo "Cannot find the directory \"../../CustomBlenderproc/datasets/$dataset_name/models/\"" 
    exit 1
fi



### classes.json generation ###

# Initialize the JSON string
json_content="{\n"

# Counter to keep track of object numbers
counter=1

# Iterate over all .ply files in the directory
for file in "$models_eval_path"/obj_*.ply; do
    # Extract the object name without the extension
    object_name=$(basename "${file%.ply}")
    
    # Append to the JSON content
    json_content+="\"$counter\":\"$object_name\""
    
    # If it's not the last file, add a comma and newline
    if [ "$counter" -lt "$(ls "$models_eval_path"/obj_*.ply | wc -l)" ]; then
        json_content+=",\n"
    fi
    
    # Increment the counter
    counter=$((counter + 1))
done

# Close the JSON string
json_content+="\n}"

# Write the JSON content to classes.json
echo -e "$json_content" > "$annotations_path/classes.json"



### symmetries.json generation ###

# Initialize the JSON string
json_content="{\n"

# Counter to keep track of the number of files
counter=1

# Iterate over all .ply files in the directory
for file in "$models_eval_path"/obj_*.ply; do
    # Extract the object name without the extension
    object_name=$(basename "${file%.ply}")
    
    # Append to the JSON content
    json_content+="\"$object_name\": false"
    
    # If it's not the last file, add a comma and newline
    if [ "$counter" -lt "$(ls "$models_eval_path"/obj_*.ply | wc -l)" ]; then
        json_content+=",\n"
    fi
    
    # Increment the counter
    counter=$((counter + 1))
done

# Close the JSON string
json_content+="\n}"

# Write the JSON content to symmetries.json
echo -e "$json_content" > "$annotations_path/symmetries.json"



### dataset.names generation ###

# Initialize the string
file_content=""

# Counter to keep track of the number of files
counter=1

# Iterate over all .ply files in the directory
for file in "$models_eval_path"/obj_*.ply; do
    # Extract the object name without the extension
    object_name=$(basename "${file%.ply}")
    
    # Append to the JSON content
    file_content+="$object_name"
    
    # If it's not the last file, add a comma and newline
    if [ "$counter" -lt "$(ls "$models_eval_path"/obj_*.ply | wc -l)" ]; then
        file_content+="\n"
    fi
    
    # Increment the counter
    counter=$((counter + 1))
done

# Write the file content to dataset.names
yolo_names_path="../../CustomPoET/models/yolov4/yolo/data"
echo -e "$file_content" > $yolo_names_path/$dataset_name.names




### dataset.yaml generation ###

# Path to the output configuration file
output_file="$yolo_names_path/$dataset_name.yaml"

# Set paths for train, val, and test datasets
train_path="/YoloDataset/train/train.txt"
val_path="/YoloDataset/test/val.txt"
test_path="/YoloDataset/test/test.txt"

# Initialize the configuration content
config_content="# train and val datasets (image directory or *.txt file with image paths)\n"
config_content+="train: $train_path\n"
config_content+="val: $val_path\n"
config_content+="test: $test_path\n\n"

# Get the number of .ply files and set it as the number of classes
num_classes=$(ls "$models_eval_path"/obj_*.ply | wc -l)
config_content+="# number of classes\n"
config_content+="nc: $num_classes\n\n"

# Initialize the class names list
config_content+="# class names\n"
config_content+="names: [\n"

# Counter to keep track of object numbers
counter=1

# Iterate over all .ply files in the directory
for file in "$models_eval_path"/obj_*.ply; do
    # Extract the object name without the extension
    object_name=$(basename "${file%.ply}")
    
    # Append to the names list
    config_content+="  '$object_name'"
    
    # If it's not the last file, add a comma
    if [ "$counter" -lt "$num_classes" ]; then
        config_content+=",\n"
    else
        config_content+="\n"
    fi
    
    # Increment the counter
    counter=$((counter + 1))
done

# Close the names list
config_content+="]"

# Write the configuration content to the output file
echo -e "$config_content" > "$output_file"

echo "All the files have been generated."