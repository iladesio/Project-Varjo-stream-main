#!/bin/bash

cd $(dirname $0)
sudo echo

# Installing blenderproc using a venv
cd CustomBlenderproc

# Downloading background textures for dataset generation
#wget https://bertagnoli.ddns.net/static/PublicDrive/BackgroundTextures.zip
#unzip BackgroundTextures.zip
#rm BackgroundTextures.zip

# Downloading original BOP datasets for dataset generation
#wget https://bertagnoli.ddns.net/static/PublicDrive/datasets.zip
#unzip datasets.zip
#rm datasets.zip
deactivate
cd ..

# Downloading pre-trained weights
#cd CustomPoET
#mkdir PreTrained
#cd PreTrained
#wget https://bertagnoli.ddns.net/static/PublicDrive/ycbv_yolo_weights.pt
#wget https://bertagnoli.ddns.net/static/PublicDrive/poet_yolo.pth
#cd ../..

# Pull docker image
#docker pull aaucns/poet:latest

sudo chmod -R +x Scripts


#!/bin/bash

# Check if python3.8 is installed
if command -v python3.8 &> /dev/null; then
    ./Scripts/install_requirements.sh
else
   echo -e "Python 3.8 is not installed. Please install it and then run the command:\n\n\t./Scripts/install_requirements.sh\n"
fi

cd ..
