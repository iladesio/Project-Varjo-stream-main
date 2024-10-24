#!/bin/bash

cd $(dirname $0)

if [ ! -d "../venv" ]; then
    echo -e "\nPlease install the venv through the Scripts/install_requirements.sh script.\n\n"
fi

webcam=0

# Usage function
usage() {
    echo "Usage: $0 [--dataset_name <name> | --webcam <true|false>] --gpu_mem_size <size>"
    echo "Options:"
    echo "  --dataset_name <name>   Specify the dataset name. Required if --webcam is not specified."
    echo "  --webcam <true|false>   Specify if the webcam should be used. If true, --dataset_name must not be specified."
    echo "  --gpu_mem_size <size>   Specify the GPU memory size (in GB). Required."
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
        --gpu_mem_size)
            if [[ -n $2 && ! $2 =~ ^- && $2 =~ ^[0-9]+$ ]]; then
                gpu_mem_size="$2"
                shift
            else
                echo "Error: --gpu_mem_size requires a numerical value."
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

# Ensure either --dataset_name or --webcam is specified correctly
if [[ $webcam -eq 0 && -z "$dataset_name" ]]; then
    echo "Error: --dataset_name must be specified if --webcam is not specified."
    usage
elif [[ $webcam -eq 1 && -n "$dataset_name" ]]; then
    echo "Error: --dataset_name must not be specified if --webcam is true."
    usage
fi

# Ensure gpu_mem_size is specified
if [[ -z "$gpu_mem_size" ]]; then
    echo "Error: --gpu_mem_size is required."
    usage
fi

# Check if dataset folder exists
if [[ -n "$dataset_name" && ! -d "$(pwd)/../../Datasets/$dataset_name" ]]; then 
    echo "Error: Dataset directory ../../Datasets/$dataset_name does not exist."
    exit 1
fi

# Function to clean up Docker container
cleanup() {
    echo "Stopping Docker container..."
    docker stop $CONTAINER_ID
    exit 0
}

# Set up trap to call cleanup on script exit
trap cleanup SIGINT SIGTERM

mount=""
# Add dataset volume mount if dataset_name is specified
if [[ -n "$dataset_name" ]]; then
    mount="-v $(pwd)/../../Datasets/$dataset_name/GeneratedScenesBop/train/train_synt/000000/rgb:/data/0000"
fi

sudo docker run --entrypoint= -v $(pwd)/../../CustomPoET:/opt/project \
    -p 9999:9999 \
    $mount \
    --shm-size=${gpu_mem_size}g --rm --gpus all aaucns/poet:latest python \
    -u /opt/project/poet_inference.py \
    --webcam=$webcam \
    --models="/models_eval/$dataset_name/" \
    --model_symmetry="/annotations/$dataset_name/symmetries.json" \
    --class_info="/annotations/$dataset_name/classes.json" &

# Capture the Docker container ID
CONTAINER_ID=$!

# Wait for the container to start up
sleep 10

if [[ $webcam -eq 1 ]]; then
    echo "Starting client"

    source ../venv/bin/activate
    # Run the Python client
    python3.8 poet_inference_client.py &
fi

# Wait for the container to exit
wait $CONTAINER_ID
