# Industrial Pose Estimation Framework

The project is mainly concerned with:
1. Generating a custom synthetic dataset using your own 3D object models.
2. Training a deep-learning transformer model for the 6D pose estimation task.

The framework is composed of two projects from which we started:
1. [Blenderproc](https://github.com/DLR-RM/BlenderProc)
2. [PoET Transformer](https://github.com/aau-cns/poet)

Our work consisted of modifying and adjusting some bugs and problems encountered while using the two listed projects. Moreover, we developed a set of scripts to simplify the usability of the two projects.

## Installation 🚀
**IMPORTANT NOTE: In order to run the inference scripts you must have Python3.8 installed. Otherwise they won't work. Please install it and then follow the instructions**.

To install all the necessary components, run the following code:

```
# Clone the repository
git clone https://github.com/DanieleBertagnoli/IndustrialPoseEstimationFramework

# Run the installtion script
cd IndustrialPoseEstimationFramework
sudo chmod +x install.sh
./install.sh
```

## Dataset Generation Task 
The synthetic dataset is based on the BOP Challenge, with the BlenderProc command used to generate most of the synthetic datasets provided in the challenge. In particular, since the PoET transformer requires a YCB-Video like format, we will use the script provided by the original BlenderProc GitHub project. The high-level idea is to create a 3D scene where a random subset of objects is positioned in a realistic way. After that, the camera position is randomized, ensuring that it will point toward the center of mass of the objects loaded in the scene. 

### Setting Up 🗂️
To generate your dataset, create a new folder in `CustomBlenderproc/datasets/` named as you want. Inside the new dataset folder, create a file named `camera_uw.json`. This file will contain the camera intrinsics that will be assigned to the Blender camera. These are the suggested values:

```
{
    "cx": 312.9869,
    "cy": 241.3109,
    "depth_scale": 0.1,
    "fx": 1066.778,
    "fy": 1067.487,
    "height": 480,
    "width": 640
}

```

After that, in the same dataset folder, create a new folder called `models/`. This will contain all the .ply files representing the 3D models of your objects. The objects must be named using the following format:

```
obj_000001.ply
obj_000002.ply
...
obj_000010.ply
...
```

If your objects are not provided with a texture, BlenderProc will automatically assign the default solid-gray tint to them. To specify a texture, it's sufficient to put a .png file with the texture in the same `models/` folder, always named with the format:

```
obj_000001.png
obj_000002.png
...
obj_000010.png
...
```

### Usage
To run the dataset generation, we developed the `Scripts/generate_custom_dataset.sh` script. You can run it by also specifying some additional parameters:

```
./Scripts/generate_custom_dataset.sh --dataset_name DATASET_FOLDER_NAME --num_scenes 100 --yolo_conversion 1
```

The `dataset_name` must be equal to the folder you created during the setup phase in the `CustomBlenderproc/datasets/` folder. `num_scenes` indicates the number of sequences that will be generated. Each sequence is composed of 1000 frames, and every 25 frames the scene is reset (i.e., the objects, lights, background textures, camera positions, etc., are re-initialized). The `yolo_conversion` parameter can be either 0 or 1, where 0 indicates False and 1 indicates True. At the end of the generation, a new dataset will be generated using a YOLO-like format (this can be used to train the YOLO backbone in PoET, see next sections).

In YOLO dataset, the objects ID will be assigned starting from 0 following the order:

```
ID 0: obj_000001
ID 1: obj_000002
...
ID 10: obj_000010
...
```

## Pose Estimation Task
Perform a pose estimation task is a state-of-the-art problem, it consists into retrieving the 2D bounding boxes of objects within a frame and estimate their real-world position throguh a 6D matrix indicating the translation and rotation with respect to the camera point of view. The PoET tranformer achieved competitive results in pose estimation even for complex objects (symmetry problem), therefore we used it as main core of the pose estimation part. The project is completely deployed using Docker, the pre-built image is automatically downloaded by running the installation script mentioned above.

### Setting Up 🗂️
Before training the model you have to setup your dataset. As first step, you have to run the script:

```
./Scripts/PoETScripts/setup.sh --dataset_name YOUR_DATASET_NAME
```

This script will create the following directories:

```
CustomPoet/PoetDataset/annotations/YOUR_DATASET_NAME/
CustomPoet/PoetDataset/models_eval/YOUR_DATASET_NAME/
```

The script will automatically place in the folder `models_eval/YOUR_DATASET_NAME/` all the object `.ply` files taken from `CustomBlenderproc/datasets/YOUR_DATASET_NAME/models`. If you didn't didn't use the generation part, you have to manually add these files to the path. However, **we recommend to setup the dataset files since all the next steps are automatically performed only if the script finds those files**.

The script will also put a copy of `CustomBlenderproc/datasets/YOUR_DATASET_NAME/models/models_info.json` into `CustomPoet/PoetDataset/models_eval/YOUR_DATASET_NAME/`.

After that, two additional files are generated:

```
CustomPoet/PoetDataset/annotations/YOUR_DATASET_NAME/classes.json
CustomPoet/PoetDataset/annotations/YOUR_DATASET_NAME/symmetries.json
```

The first file is a JSON file containing the list of the objects composing the dataset and their IDs:

```
{
    "1": "obj_000001"
    "2": "obj_000002"
    ...
    "10": "obj_000010"
    ...
}
```

As you can see, unlike YOLO where the IDs start from 0, PoET requires that the object IDs start from 1. This ID mismatch is dynamically adjusted in PoET's code. 

**IMPORTANT NOTE**: When you generate the synthetic dataset, the objects must be named by following the convention we mentioned in the dedicated section, however, starting from there you can also decide to set the object's name as you want. For instance, in  `classes.json` you can rename `"obj_000001"` into `"chair"`, the objects are represented through their ID not their name. The name is used in the evaluation files where the model performance are exposed using the names. 

**IF YOU RENAME THE OBJECTS IN THE `classes.json` FILE, REMEMBER TO RENAME ALL THE ASSOCIATED `.ply` FILES IN `CustomPoet/PoetDataset/models_eval/YOUR_DATASET_NAME/`, `symmetries.json` AND `models_info.json` AS WELL**.

The `symmetries.json` file is a simple JSON file indicated which object is symmetric or not, by default all the objects are set as non-symmetric. You can keep this file unchanged, evevn if some object is symmetric since during the model evaluation, the model will provide the evaluation considering your objects are symmetric and another file with the evaluation results considering you models as non-symmetric. 

The last two files that are created are:

```
CustomPoET/models/yolov4/yolo/data/YOUR_DATASET_NAME.names
CustomPoET/models/yolov4/yolo/data/YOUR_DATASET_NAME.yaml
```

The first file contains the list of the object names (**IF YOU CHANGED THE NAMES BE CONSISTENT EVEN IN THESE TWO FILES**), while the seconds contains the list of the names and the number of classes (number of object) that YOLO will use during the training. Don't touch these files unless you changed the object names, in that case, simply rename the correspondant objects with the new name.

### Usage

Before training the PoET transformer, you have to train the YOLO backbone. PoET can be trained in backbone mode (i.e. the 2D boudning boxes are retrieved from the backbone) or ground-truth mode (i.e. the 2D boxes are retrieved from the ground-truth data). However, evevn if you decide to train the model in ground-truth mode, you still have to train the backbone since you need a checkpoint that will be loaded but not used during the transformer training. Currently, the backbone and the transformer cannot be trained toghether. 

#### YOLO Backbone Training
Training the backbone is very simple:
1.  First of all you have to choose the backbone hyperparameters by modifying the file `CustomPoET/models/yolov4/yolo/data/hyp.scratch.yaml`. You can also leave them unchanged as they should work well in most of the case, however for a particular fine-tuning could be usefull to slightly adjust them. 

2. The second step consists in updating the `CustomPoET/configs/yolo_run_configs.yml`. The parameters that you should change are:
    - `weights`: Path of a pre-trained model weights. We strongly advice you to start from the pretrained weights already available in `CusomPoET/PreTrained`, this will speed up the training as the model will be only fine-tuned on your dataset.

    - `to_freeze`: Percentage of layers which will be froze during the training (transfer learning technique). Suggested value: 30.

    - `epochs`: Number of epochs.

    - `batch_size`

    - `data`: Path of `.names` file created by the setup script (`CustomPoET/models/yolov4/yolo/data/YOUR_DATASET_NAME.names`).

3. Modify the `CustomPoET/configs/yolo.cfg` file. This file contains the YOLO architecture description. Basically, to adjust the file based on your dataset you have to:
    - Adjust the `classes` value in the each `[yolo]` layer with the number of objects in your dataset.
    - Adjust the `filters` value in each `[convolutional]` layer that preceeds the `[yolo]` layer. The numer of filters is calculated as follow: **(number of classes + 5) * 3**. Suppose that you have 20 classes, then the filters must be set to (20 + 5) * 3 = 75.

4. Run the training script:

```
./Scripts/PoETScripts/yolo_train.sh --dataset_name YOUR_DATASET_NAME --gpu_mem_size GPU_MEM_IN_GB
```    

The results will be put in the `CustomPoET/YoloRuns` folder, probably to access them you have to change the directory permissions, since Docker will make it unreadable for everyone else except docker itself:

```
sudo chmod -R 755 CustomPoET/YoloRuns
```

#### PoET Transformer Training
Once you trained the backbone, the next step is to train the transformer model:

1. The first step consists into modify the model hyperparameters through the file `CustomPoET/configs/poet_run_configs.yml`:
    - `batch_size`
    - `eval_batch_size`
    - `epochs`
    - `backbone_weights`: Path of the `.pt` backbone file, we strongly suggest to put the file in `CustomPoET/PreTrained` folder.
    - `dataset`: Dataset name.
    - `n_classes`: Number of classes.
    - `num_queries`: Number of queries, hence maximum number of objects that can be detected in the scene. The higher the number is, the more complex the model will be (i.e. the training will require more resources and time to be completed).
    - `enc_layers`: Number of transformer encoding layers. Suggested value: 5.
    - `dec_layers`: Number of transformer decoding layers. Suggested value: 5.
    - `nheads`: Number of transformer heads. Suggested value: 5.

2. Run the training script:

```
./Scripts/PoETScripts/poet_train.sh --dataset_name YOUR_DATASET_NAME --gpu_mem_size GPU_MEM_IN_GB
```

The results will be generated in `CustomPoET/PoetOutput` folder, probably to access them you have to change the directory permissions, since Docker will make it unreadable for everyone else except docker itself:

```
sudo chmod -R 755 CustomPoET/PoetOutput
```