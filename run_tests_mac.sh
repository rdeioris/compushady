cd test
COMPUSHADY_BACKEND=metal python3 -m unittest $1
COMPUSHADY_BACKEND=vulkan python3 -m unittest $1
