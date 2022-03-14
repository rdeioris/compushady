import numpy
import json
import base64


def vector4(x, y, z, w):
    return numpy.array((x, y, z, w), dtype=numpy.float32)


def vector3(x, y, z):
    return vector4(x, y, z, 1)


def identity_matrix():
    return numpy.array((
        (1, 0, 0, 0),
        (0, 1, 0, 0),
        (0, 0, 1, 0),
        (0, 0, 0, 1),
    ), dtype=numpy.float32)


def scale_matrix(x, y, z):
    return numpy.array((
        (x, 0, 0, 0),
        (0, y, 0, 0),
        (0, 0, z, 0),
        (0, 0, 0, 1),
    ), dtype=numpy.float32
    )


def translation_matrix(x, y, z):
    return numpy.array((
        (1, 0, 0, x),
        (0, 1, 0, y),
        (0, 0, 1, z),
        (0, 0, 0, 1),
    ), dtype=numpy.float32
    )


def rotation_matrix_y(angle):
    return numpy.array((
        (numpy.cos(angle), 0, numpy.sin(angle), 0),
        (0, 1, 0, 0),
        (-numpy.sin(angle), 0, numpy.cos(angle), 0),
        (0, 0, 0, 1),
    ), dtype=numpy.float32
    )


def perspective_matrix(left, right, top, bottom, near, far):
    return numpy.array((
        ((2 * near) / (right - left), 0, 0, (right+left) / (right-left)),
        (0, (2 * near) / (top-bottom), 0, (top+bottom)/(top-bottom)),
        (0, 0, -(far + near) / (far-near), -(2 * far * near)/(far-near)),
        (0, 0, -1, 0)
    ), dtype=numpy.float32
    )


def perspective_matrix_fov(fov, aspect_ratio, near, far):
    top = near * numpy.tan(fov/2)
    bottom = -top
    right = top * aspect_ratio
    left = -right
    return perspective_matrix(left, right, top, bottom, near, far)


class GLTF:
    def __init__(self, filename):
        self.asset = json.load(open(filename, 'rb'))
        self.size_table = {
            5123: 2,  # unsigned short
            5126: 4,  # float
            5125: 4,  # unsigned int
            'VEC3': 3,
            'VEC2': 2,
            'VEC4': 4,
            'SCALAR': 1
        }

    def get_accessor_bytes(self, accessor_index):
        accessor = self.asset['accessors'][accessor_index]
        buffer_view = self.asset['bufferViews'][accessor['bufferView']]
        print('bufferView', buffer_view)
        prefix = 'data:application/octet-stream;base64,'
        buffer = base64.b64decode(
            self.asset['buffers'][buffer_view['buffer']]['uri'][len(prefix):])
        print('buffer size', len(buffer))
        buffer_data = buffer[buffer_view['byteOffset']:buffer_view['byteOffset']+buffer_view['byteLength']]
        print('buffer data size', len(buffer_data))
        accessor_data_start = accessor['byteOffset']
        accessor_data_size = accessor['count'] * \
            self.size_table[accessor['componentType']] * \
            self.size_table[accessor['type']]
        return buffer_data[accessor_data_start:
                           accessor_data_start+accessor_data_size]

    def get_indices(self, mesh_index):
        mesh = self.asset['meshes'][mesh_index]
        primitive = mesh['primitives'][0]
        indices = primitive['indices']
        return self.get_accessor_bytes(indices)

    def get_vertices(self, mesh_index):
        mesh = self.asset['meshes'][mesh_index]
        primitive = mesh['primitives'][0]
        position = primitive['attributes']['POSITION']
        return self.get_accessor_bytes(position)

    def get_normals(self, mesh_index):
        mesh = self.asset['meshes'][mesh_index]
        primitive = mesh['primitives'][0]
        normal = primitive['attributes']['NORMAL']
        return self.get_accessor_bytes(normal)

    def get_colors(self, mesh_index):
        mesh = self.asset['meshes'][mesh_index]
        primitive = mesh['primitives'][0]
        color = primitive['attributes']['COLOR_0']
        return self.get_accessor_bytes(color)

    def get_nvertices(self, mesh_index):
        mesh = self.asset['meshes'][mesh_index]
        primitive = mesh['primitives'][0]
        indices = primitive['indices']
        return self.asset['accessors'][indices]['count']
