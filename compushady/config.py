
import os
import platform

debug = False
wanted_backend = None

compushady_backend_env = 'COMPUSHADY_BACKEND'
if compushady_backend_env in os.environ:
    wanted_backend = os.environ[compushady_backend_env]
else:
    if platform.system() == 'Windows':
        wanted_backend = 'd3d12'
    elif platform.system() == 'Darwin':
        wanted_backend = 'metal'
    else:
        wanted_backend = 'vulkan'


def set_backend(backend_name):
    global wanted_backend
    wanted_backend = backend_name


def set_debug(enable):
    global debug
    debug = enable
