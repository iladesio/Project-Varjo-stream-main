import os
import shutil
import json
import argparse
from time import time

def convert(path: str, train: bool, new_yolo_path: str, img_width: int, img_height: int):
    # Create new directories
    if train:
        new_path = os.path.join(new_yolo_path, 'train')
    else:   
        new_path = os.path.join(new_yolo_path, 'test')
    
    start = time()
    print('Copying files...')
    shutil.copytree(path, new_path)
    print(f'Copy done in {time()-start}s')

    # Move all the images
    scenes = sorted(os.listdir(new_path))

    os.makedirs(os.path.join(new_path, 'images'))
    os.makedirs(os.path.join(new_path, 'labels'))

    files = ''
    non_visible = 0
    outside_frame = 0

    start = time()
    print('Refactoring labels and files...')
    for scene in scenes:
        print(f'Processing scene {scene}/{len(scenes)}')

        scene_path = os.path.join(new_path, scene)
        rgb_path = os.path.join(scene_path, 'rgb')

        # BBoxe labels        
        with open(os.path.join(scene_path, 'scene_gt_info.json'), 'r') as f:
            scene_gt_info = json.load(f)

        # Pose labels used to retrieve the object ID
        with open(os.path.join(scene_path, 'scene_gt.json'), 'r') as f:
            scene_gt = json.load(f)
        
        for img in sorted(os.listdir(rgb_path)):
            labels = ''

            img_pose_labels = scene_gt[str(int(img[:-4]))]
            for i, obj in enumerate(img_pose_labels):
                object_id = int(obj['obj_id']) - 1
                bbox_labels = scene_gt_info[str(int(img[:-4]))][i]['bbox_visib']

                if scene_gt_info[str(int(img[:-4]))][i]['visib_fract'] < 0.2:
                    non_visible += 1
                    print('Object too occluded, skipping')
                    continue

                if bbox_labels[0] < 0 or bbox_labels[1] < 0 or bbox_labels[2] < 0 or bbox_labels[3] < 0:
                    outside_frame += 1
                    print('Object is outside the frame, skipping')
                    continue

                x_min = bbox_labels[0]
                y_min = bbox_labels[1]
                w = bbox_labels[2] / img_width
                h = bbox_labels[3] / img_height

                cx = (x_min + bbox_labels[2] / 2) / img_width
                cy = (y_min + bbox_labels[3] / 2) / img_height 

                labels += f'{object_id} {cx} {cy} {w} {h}\n'

            # Yolo labels
            with open(os.path.join(new_path, 'labels', f'{scene}-{img[:-4]}.txt'), 'w') as f:
                f.write(labels)

        # Move images
        for img in os.listdir(rgb_path):
            files += f'./images/{scene}-{img[:-4]}.jpg\n'
            shutil.move(os.path.join(rgb_path, img), os.path.join(new_path, 'images', f'{scene}-{img[:-4]}.jpg'))

        if train:
            filename = 'train.txt'
        else:
            filename = 'val.txt'
            
        with open(os.path.join(new_path, filename), 'w') as f:
            f.write(files)

        shutil.rmtree(scene_path)

    print(f'\n\nSkipped {non_visible} annotations as the objects are too occluded')
    print(f'\n\nSkipped {outside_frame} annotations as the objects are outside the frame')

    print(f'File refactoring done in {time()-start}s')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert dataset to YOLO format.')
    parser.add_argument('--dataset_name', type=str, required=True, help='Specify the dataset name.')
    args = parser.parse_args()

    dataset_name = args.dataset_name

    GENERATED_SCENES_BOP_PATH = f'{os.path.dirname(__file__)}/../../Datasets/{dataset_name}/GeneratedScenesBop'
    NEW_YOLO_PATH = f'{os.path.dirname(__file__)}/../../Datasets/{dataset_name}/YoloDatasetV2'

    IMG_WIDTH = 640
    IMG_HEIGHT = 480

    # Check if the paths exist
    if not os.path.exists(GENERATED_SCENES_BOP_PATH):
        print(f"Error: The path {GENERATED_SCENES_BOP_PATH} does not exist.")
        exit(1)
    
    if not os.path.exists(NEW_YOLO_PATH):
        os.makedirs(NEW_YOLO_PATH)

    convert(os.path.join(GENERATED_SCENES_BOP_PATH, 'train', 'train_synt'), True, NEW_YOLO_PATH, IMG_WIDTH, IMG_HEIGHT)
    convert(os.path.join(GENERATED_SCENES_BOP_PATH, 'test_all', 'test'), False, NEW_YOLO_PATH, IMG_WIDTH, IMG_HEIGHT)
