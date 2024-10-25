#!/bin/bash

cd $(dirname $0)

# Function to check if a command or package is installed
check_installed() {
    if ! command -v $1 &> /dev/null; then
        echo "$2 is not installed. Install it to proceed."
        exit 1
    fi
}

# Check if python3.8 is installed
check_installed python3.8 "Python 3.8"

# Check if pip for Python 3.8 is installed
#check_installed pip3.8 "pip for Python 3.8"

# Check if python3.8-venv package is installed
if dpkg -l | grep -q python3.8-venv; then
    echo "python3.8-venv is installed."
else
    echo "python3.8-venv is not installed."
fi

python3.8 -m venv venv
source venv/bin/activate
#python3.8 -m pip install opencv
python3.8 -m pip install opencv-contrib-python
deactivate