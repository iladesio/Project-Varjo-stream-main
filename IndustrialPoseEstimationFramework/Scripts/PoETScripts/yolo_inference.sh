#!/bin/bash

cd $(dirname $0)

webcam=0

# Usage function
usage() {
    echo "Usage: $0 --gpu_mem_size <size> [--webcam <true|false>]"
    echo "Options:"
    echo "  --gpu_mem_size <size>    Specify the GPU memory size (in GB). Required."
    echo "  --webcam <true|false>    Specify if the webcam should be used. Optional, default is false."
    exit 1
}

# Parse command-line arguments
while [[ "$#" -gt 0 ]]; do
    case $1 in
        --gpu_mem_size)
            if [[ -n $2 && ! $2 =~ ^- && $2 =~ ^[0-9]+$ ]]; then
                gpu_mem_size="$2"
                shift
            else
                echo "Error: --gpu_mem_size requires a numerical value."
                usage
            fi
            ;;
        --webcam)
            if [[ $2 == "true" ]]; then 
                webcam=1
                shift
            elif [[ $2 == "false" ]]; then
                webcam=0
                shift
            else
                echo "Error: --webcam must be either true or false."
                usage
            fi
            ;;
        -h)
            usage
            ;;
        *)
            echo "Unknown parameter passed: $1"
            usage
            ;;
    esac
    shift
done

# Ensure gpu_mem_size is specified
if [[ -z "$gpu_mem_size" ]]; then
    echo "Error: --gpu_mem_size is required."
    usage
fi

# Function to clean up Docker container
cleanup() {
    echo "Stopping Docker container..."
    docker stop $CONTAINER_ID
    exit 0
}

# Set up trap to call cleanup on script exit
trap cleanup SIGINT SIGTERM

mount_webcam=""
mount_dataset=""
# Add device mount if webcam is specified
if [[ $webcam -eq 1 ]]; then
    mount_webcam="--device /dev/video0:/dev/video0"
else
    mount_dataset="-v $(pwd)/../../Datasets/YoloDatasetV2/:/YoloDataset"
fi

# Run Docker container in the foreground
sudo docker run --entrypoint= -v $(pwd)/../../CustomPoET:/opt/project \
    $mount_dataset \
    $mount_webcam \
    --shm-size=${gpu_mem_size}g --rm --gpus all -p 9999:9999 aaucns/poet:latest \
    python -u /opt/project/models/yolov4/yolo/detect.py \
    --webcam=$webcam \
    --names="/opt/project/models/yolov4/yolo/data/$dataset_name.names"& 

# Capture the Docker container ID
CONTAINER_ID=$!

# Wait for the container to start up
sleep 10

echo "Starting client"

# Run the Python client
python3 yolo_inference_client.py

# Wait for the container to exit
wait $CONTAINER_ID
