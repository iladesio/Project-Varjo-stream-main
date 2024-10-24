# Varjo Communication Framework
This project is part of a bigger Github Project, [IndustrialPoseEstimationFramework](https://github.com/DanieleBertagnoli/IndustrialPoseEstimationFramework). In the linked project there is the possibility to run a pose estimation model in inference mode through a client-server architecture, however in that case the client uses a classical webcam to capture images. In this case we want to modify things to accept the Varjo XR-4 visor raw-camera images instead of a classical webcam.

**IMPORTANT NOTE: The `Varjo` folder containes the files needed to run the client that retrieves the infos from the visor, IT HAS TO BE RUN USING WINDOWS and VISUAL STUDIO CODE 2022**. This is not our choice, Varjo unfortunately does not provide the necessary softwares for other OS. 

## Installation 🚀

### Visual Studio 2022
As I specified in the previous section, the varjo client needs to be run on windows through visual studio 2022. You can download and setup visual studio from the official [Microsoft website](https://visualstudio.microsoft.com/it/downloads/). Once downloaded you have to install also the presets for building C++ applications. To compile and use the project you have to:
1. Open the folder `VarjoApp/examples` using Visual Studio
2. Go to Tools -> Command Line -> Developer Command Prompt
3. Run the in the prompt:
```
vcpkg integrate install
vcpkg install
```
4. Go to Project -> Delete Cache and Reconfigure
5. Open `AppLogic.cpp` file and modify the server IP with the one of your server.
6. Go to Build -> Build all

### Varjo Base
If you have a Varjo visor probably you already installed the Varjo Base software. If not, you can download it at the [Varjo website](https://varjo.com/downloads/).

### Server
The server is a simple `.py` server. If you want to use it for your own purposes, it will be sufficien to install `OpenCV` through pip and run the server. 

If you want to run the server on a different machine using Linux or WSL, simply run:
```
python3 -m venv venv
source venv/bin/activate
pip install opencv-python
```

For Windows users:
```
python -m venv venv
source venv/Scripts/Activate.ps1
pip install opencv-python
```

## Usage 👾

### Client
Once compiled all the project, to run the client you have to open Varjo Base software, turn on the visor and wait for its connection to the driver. Then through Visual Studio you have to select the target and press the run button.

### Server
To run the server it will be sufficient the following command:
```
python server.py
```

## Issues 🚨
Feel free to contact me or open a public issue. Help me improve the project!
