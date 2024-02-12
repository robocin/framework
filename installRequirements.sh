#!/bin/bash
BASE_DIR=`pwd`

sudo apt install qtbase5-dev qt5-qmake libeigen3-dev libeigen3-dev -y
cd /usr/local/include
sudo ln -sf eigen3/Eigen Eigen
sudo ln -sf eigen3/unsupported unsupported

cd ${BASE_DIR}