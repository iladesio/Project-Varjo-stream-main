#!/bin/bash

cd $(dirname $0)

# Usage function
usage() {
    echo "Usage: $0 --dataset_name <name> --gpu_mem_size <size>"
    echo "Options:"
    echo "  --dataset_name <name>    Specify the dataset name. Required."
    echo "  --gpu_mem_size <size>    Specify the GPU memory size (in GB). Required."
    exit 1
}

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
        --gpu_mem_size)
            if [[ -n $2 && ! $2 =~ ^- && $2 =~ ^[0-9]+$ ]]; then
                gpu_mem_size="$2"
                shift
            else
                echo "Error: --gpu_mem_size requires a numerical value."
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

# Check if mandatory parameters are provided
if [[ -z "$dataset_name" ]]; then
    echo "Error: --dataset_name is required."
    usage
fi

if [[ -z "$gpu_mem_size" ]]; then
    echo "Error: --gpu_mem_size is required."
    usage
fi

# Check if the dataset directory exists
if [[ ! -d "$(pwd)/../../Datasets/$dataset_name/GeneratedScenesBop" ]]; then
    echo "Error: Dataset directory ../../Datasets/$dataset_name/GeneratedScenesBop does not exist."
    exit 1
fi

# Run the Docker container
sudo docker run --entrypoint= -v $(pwd)/../../CustomPoET:/opt/project \
    -v $(pwd)/../../Datasets/$dataset_name/GeneratedScenesBop/:/TmpDataset \
    --shm-size=${gpu_mem_size}g --rm --gpus all aaucns/poet:latest python \
    -u /opt/project/data_utils/data_annotation/ycbv2poet.py
