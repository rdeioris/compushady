@echo off
cd test
set COMPUSHADY_BACKEND=d3d12
python -m unittest
set COMPUSHADY_BACKEND=vulkan
python -m unittest