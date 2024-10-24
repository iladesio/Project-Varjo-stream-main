from setuptools import setup, find_packages
import os

# Extract version from blenderproc/version.py
here = os.path.abspath(os.path.dirname(__file__))
version = {}
with open(os.path.join(here, "blenderproc", "version.py")) as fp:
    exec(fp.read(), version)

setup(name='blenderproc',
    version=version['__version__'],
    url='https://github.com/DLR-RM/BlenderProc',
    author='Maximilian Denninger, Dominik Winkelbauer, Martin Sundermeyer',
    maintainer='Dominik Winkelbauer',
    packages=find_packages(exclude=['docs', 'examples', 'external', 'images', 'resources', 'scripts', 'tests']),
    include_package_data=True,
    entry_points={
        'console_scripts': ['blenderproc=blenderproc.command_line:cli'],
    },      
    install_requires=["setuptools", "pyyaml", "requests", "matplotlib", "numpy", "Pillow", "h5py", "progressbar"],
)
