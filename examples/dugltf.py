"""
MIT License

Copyright (c) Duality Robotics (https://duality.ai)
Copyright (c) Heat - SPA (https://heat.tech/)
Copyright (c) Roberto De Ioris (roberto.deioris@gmail.com) 

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""

import base64
import json
import os
import struct
from zipfile import ZipFile, ZIP_DEFLATED

GLTF_INT = 5125
GLTF_FLOAT = 5126
GLTF_USHORT = 5123

GLTF_ARRAY_BUFFER = 34962
GLTF_ELEMENT_ARRAY_BUFFER = 34963


def flatten(a):
    return [col for row in a for col in row]


def fix_uv(uv):
    new_uv = []
    for idx, item in enumerate(uv):
        if idx % 2 == 0:
            new_uv.append(item)
        else:
            new_uv.append(1 - item)
    return new_uv


def chunks(items, n):
    for i in range(0, len(items), n):
        yield items[i : i + n]


class DuGLTF:
    def __init__(self, filename=None):
        self.binary = b""
        self.gltf = {"asset": {"version": "2.0"}}
        self.gltf["scene"] = 0
        self.gltf["scenes"] = [{"nodes": []}]
        self.gltf["nodes"] = []
        self.gltf["meshes"] = []
        self.gltf["skins"] = []
        self.gltf["bufferViews"] = []
        self.gltf["buffers"] = []
        self.gltf["accessors"] = []
        self.gltf["materials"] = []
        self.gltf["images"] = []
        self.gltf["textures"] = []
        self.gltf["samplers"] = []
        self.gltf["animations"] = []
        self.gltf["extensions"] = []
        if filename:
            with open(filename, "rb") as handle:
                data = handle.read()
                if data[0:4] == b"glTF":
                    chunk_length, _ = struct.unpack("II", data[12:20])
                    json_data = data[20 : 20 + chunk_length]
                    binary_length, _ = struct.unpack(
                        "II", data[20 + chunk_length : 20 + chunk_length + 8]
                    )
                    self.binary = data[
                        20 + chunk_length + 8 : 20 + chunk_length + 8 + binary_length
                    ]
                else:
                    json_data = data
                gltf_to_merge = json.loads(json_data)
                self.gltf = {**self.gltf, **gltf_to_merge}

    def add_node(
        self,
        parent_id=None,
        name=None,
        mesh_id=None,
        translation=None,
        rotation=None,
        scale=None,
        matrix=None,
    ):
        self.gltf["nodes"].append({})
        node_id = len(self.gltf["nodes"]) - 1
        if name:
            self.gltf["nodes"][node_id]["name"] = name
        if mesh_id is not None:
            self.gltf["nodes"][node_id]["mesh"] = mesh_id
        if matrix is None:
            if translation is not None:
                self.gltf["nodes"][node_id]["translation"] = translation
            if rotation is not None:
                self.gltf["nodes"][node_id]["rotation"] = rotation
            if scale is not None:
                self.gltf["nodes"][node_id]["scale"] = scale
        else:
            self.gltf["nodes"][node_id]["matrix"] = matrix
        if parent_id is not None:
            if not "children" in self.gltf["nodes"][parent_id]:
                self.gltf["nodes"][parent_id]["children"] = []
            self.gltf["nodes"][parent_id]["children"].append(node_id)
        else:
            self.gltf["scenes"][0]["nodes"].append(node_id)
        return node_id

    def add_image(self, data, mime_type):
        image_buffer_view, _ = self.add_buffer_view(data)
        self.gltf["images"].append(
            {"bufferView": image_buffer_view, "mimeType": mime_type}
        )
        return len(self.gltf["images"]) - 1

    def add_texture(self, image_id=None, sampler_id=None):
        self.gltf["textures"].append({})
        texture_id = len(self.gltf["textures"]) - 1
        if image_id is not None:
            self.gltf["textures"][texture_id]["source"] = image_id
        if sampler_id is not None:
            self.gltf["textures"][texture_id]["sampler"] = sampler_id
        return texture_id

    def add_sampler(self, data=None):
        self.gltf["samplers"].append({})
        sampler_id = len(self.gltf["samplers"]) - 1
        if data is not None:
            self.gltf["samplers"][sampler_id] = data
        return sampler_id

    def set_node_mesh(self, node_id, mesh_id):
        self.gltf["nodes"][node_id]["mesh"] = mesh_id

    def set_node_matrix(self, node_id, matrix):
        if "translation" in self.gltf["nodes"][node_id]:
            del self.gltf["nodes"][node_id]["translation"]
        if "rotation" in self.gltf["nodes"][node_id]:
            del self.gltf["nodes"][node_id]["rotation"]
        if "scale" in self.gltf["nodes"][node_id]:
            del self.gltf["nodes"][node_id]["scale"]
        self.gltf["nodes"][node_id]["matrix"] = matrix

    def set_node_skin(self, node_id, skin_id):
        self.gltf["nodes"][node_id]["skin"] = skin_id

    def add_mesh(self):
        self.gltf["meshes"].append({"primitives": []})
        return len(self.gltf["meshes"]) - 1

    def add_skin(self):
        self.gltf["skins"].append({})
        return len(self.gltf["skins"]) - 1

    def add_material(self, pbr=None, name=None, items=None):
        self.gltf["materials"].append({})
        material_id = len(self.gltf["materials"]) - 1
        if name is not None:
            self.gltf["materials"][material_id]["name"] = name
        if pbr is not None:
            self.gltf["materials"][material_id]["pbrMetallicRoughness"] = pbr
        if items:
            self.set_material_items(material_id, items)
        return material_id

    def set_material_pbr(self, material_id, items):
        if "pbrMetallicRoughness" not in self.gltf["materials"][material_id]:
            self.gltf["materials"][material_id]["pbrMetallicRoughness"] = {}
        self.gltf["materials"][material_id]["pbrMetallicRoughness"].update(items)

    def set_material_items(self, material_id, items):
        self.gltf["materials"][material_id].update(items)

    def set_texture_image(self, texture_id, image_id):
        self.gltf["textures"][texture_id]["source"] = image_id

    def add_primitive(
        self,
        mesh_id,
        indices=None,
        attributes={},
        material_id=None,
        morph_targets=[],
        mode=4,
    ):
        self.gltf["meshes"][mesh_id]["primitives"].append({})
        primitive_id = len(self.gltf["meshes"][mesh_id]["primitives"]) - 1
        if indices is not None:
            self.gltf["meshes"][mesh_id]["primitives"][primitive_id][
                "indices"
            ] = indices
        if material_id is not None:
            self.gltf["meshes"][mesh_id]["primitives"][primitive_id][
                "material"
            ] = material_id
        self.gltf["meshes"][mesh_id]["primitives"][primitive_id][
            "attributes"
        ] = attributes
        if morph_targets:
            self.gltf["meshes"][mesh_id]["primitives"][primitive_id][
                "targets"
            ] = morph_targets
        self.gltf["meshes"][mesh_id]["primitives"][primitive_id]["mode"] = mode
        return primitive_id

    def add_animation(self, name=None, channels=[], samplers=[]):
        animation = {"channels": channels, "samplers": samplers}
        if name is not None:
            animation["name"] = name
        self.gltf["animations"].append(animation)
        return len(self.gltf["animations"]) - 1

    def get_extensions(self):
        return self.gltf["extensions"]

    def get_extension(self, name):
        if name in self.get_extensions():
            return self.get_extensions()[name]
        return None

    def get_animations(self):
        return self.gltf["animations"]

    def get_animation_channels(self, animation_id):
        return self.gltf["animations"][animation_id].get("channels", [])

    def get_animation_samplers(self, animation_id):
        return self.gltf["animations"][animation_id].get("samplers", [])

    def get_animation_sampler(self, animation_id, sampler_id):
        return self.gltf["animations"][animation_id]["samplers"][sampler_id]

    def get_animation_sampler_by_node_and_path(self, animation_id, node_id, path):
        for channel in self.get_animation_channels(animation_id):
            if channel["target"]["node"] == node_id:
                if channel["target"]["path"] == path:
                    return channel["sampler"]
        return None

    def remap_mesh_materials(self, mesh_id, materials_map):
        for primitive in self.gltf["meshes"][mesh_id]["primitives"]:
            if "material" in primitive:
                primitive["material"] = materials_map[primitive["material"]]

    def remap_node_materials(self, node_id, materials_map, remapped_meshes=[]):
        if "mesh" in self.gltf["nodes"][node_id]:
            mesh_id = self.gltf["nodes"][node_id]["mesh"]
            if not mesh_id in remapped_meshes:
                self.remap_mesh_materials(mesh_id, materials_map)
                remapped_meshes.append(mesh_id)

    def add_joint(self, skin_id, node_id):
        if "joints" not in self.gltf["skins"][skin_id]:
            self.gltf["skins"][skin_id]["joints"] = []
        self.gltf["skins"][skin_id]["joints"].append(node_id)
        return len(self.gltf["skins"][skin_id]["joints"]) - 1

    def set_inverse_bind_matrices(self, skin_id, inverse_bind_matrices):
        self.gltf["skins"][skin_id]["inverseBindMatrices"] = inverse_bind_matrices

    def add_buffer_view(self, data, target=None):
        buffer_start = len(self.binary)
        self.binary += data
        buffer_length = len(self.binary) - buffer_start
        if buffer_length % 4 != 0:
            self.binary += bytes(4 - (buffer_length % 4))
        buffer_view = {
            "buffer": 0,
            "byteOffset": buffer_start,
            "byteLength": buffer_length,
        }
        if target is not None:
            buffer_view["target"] = target
        self.gltf["bufferViews"].append(buffer_view)
        return (len(self.gltf["bufferViews"]) - 1, buffer_length)

    def add_accessor(
        self,
        component_type,
        data,
        elements=1,
        add_minmax=False,
        raw=False,
        target=None,
        _min=None,
        _max=None,
    ):
        formats = {GLTF_INT: ("I", 4), GLTF_FLOAT: ("f", 4), GLTF_USHORT: ("H", 2)}
        element_type = {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4", 16: "MAT4"}
        if not raw:
            buffer_view, buffer_length = self.add_buffer_view(
                struct.pack(
                    "{0}{1}".format(len(data), formats[component_type][0]), *data
                ),
                target=target,
            )
        else:
            buffer_view, buffer_length = self.add_buffer_view(data, target=target)
        self.gltf["accessors"].append(
            {
                "bufferView": buffer_view,
                "componentType": component_type,
                "count": buffer_length // (formats[component_type][1] * elements),
                "type": element_type[elements],
            }
        )
        accessor_id = len(self.gltf["accessors"]) - 1

        if add_minmax:
            _min = [None] * elements
            _max = [None] * elements
            for chunk in chunks(data, elements):
                for element in range(0, elements):
                    if _min[element] is None or chunk[element] < _min[element]:
                        _min[element] = chunk[element]
                    if _max[element] is None or chunk[element] > _max[element]:
                        _max[element] = chunk[element]
            self.gltf["accessors"][accessor_id]["min"] = _min
            self.gltf["accessors"][accessor_id]["max"] = _max
        else:
            if _min is not None:
                self.gltf["accessors"][accessor_id]["min"] = _min
            if _max is not None:
                self.gltf["accessors"][accessor_id]["max"] = _max

        return accessor_id

    def set_node_parent(self, node_id, parent_id):
        self._make_node_orphan(node_id)
        if parent_id is None:
            if node_id not in self.gltf["scenes"][0]["nodes"]:
                self.gltf["scenes"][0]["nodes"].append(node_id)
        else:
            if "children" not in self.gltf["nodes"][parent_id]:
                self.gltf["nodes"][parent_id]["children"] = []
            self.gltf["nodes"][parent_id]["children"].append(node_id)
            self.gltf["scenes"][0]["nodes"] = list(
                filter(lambda x: x != node_id, self.gltf["scenes"][0]["nodes"])
            )

    def get_node_parent(self, node_id):
        for current_node_id, node in enumerate(self.gltf["nodes"]):
            if node_id in node.get("children", []):
                return current_node_id
        return None

    def is_node_child_of(self, node_id, parent_id):
        current_parent_id = node_id
        while current_parent_id is not None:
            if current_parent_id == parent_id:
                return True
            current_parent_id = self.get_node_parent(current_parent_id)
        return False

    def is_root_node(self, node_id):
        return self.get_node_parent(node_id) is None

    def get_root_node(self, node_id):
        parent_id = self.get_node_parent(node_id)
        if parent_id is None:
            return node_id
        while True:
            new_parent_id = self.get_node_parent(parent_id)
            if new_parent_id is None:
                return parent_id
            parent_id = new_parent_id

    def set_node_children(self, node_id, children_ids):
        if "children" not in self.gltf["nodes"][node_id]:
            self.gltf["nodes"][node_id]["children"] = []
        self.gltf["nodes"][node_id]["children"] = children_ids
        for child_id in children_ids:
            self.gltf["scenes"][0]["nodes"].remove(child_id)

    def set_node_translation(self, node_id, translation):
        self.gltf["nodes"][node_id]["translation"] = translation

    def set_node_rotation(self, node_id, rotation):
        self.gltf["nodes"][node_id]["rotation"] = rotation

    def set_node_scale(self, node_id, scale):
        self.gltf["nodes"][node_id]["scale"] = scale

    def get_accessor(self, accessor_id):
        return self.gltf["accessors"][accessor_id]

    def get_material(self, material_id):
        return self.gltf["materials"][material_id]

    def get_accessor_component_type(self, accessor_id):
        return self.gltf["accessors"][accessor_id]["componentType"]

    def get_accessor_type(self, accessor_id):
        return self.gltf["accessors"][accessor_id]["type"]

    def get_accessor_count(self, accessor_id):
        return self.gltf["accessors"][accessor_id]["count"]

    def get_buffer_view(self, buffer_view_id):
        return self.gltf["bufferViews"][buffer_view_id]

    def get_buffer(self, buffer_id):
        return self.gltf["buffers"][buffer_id]

    def get_buffer_view_data(self, buffer_view_id):
        buffer_view = self.get_buffer_view(buffer_view_id)
        byte_offset = buffer_view["byteOffset"]
        byte_length = buffer_view["byteLength"]
        byte_stride = buffer_view.get("byteStride", 0)
        # TODO: manage unaligned stride?
        buffer_id = buffer_view.get("buffer", 0)
        if buffer_id < len(self.gltf["buffers"]):
            buffer = self.gltf["buffers"][buffer_id]
            uri = buffer.get("uri", None)
            if uri is not None:
                prefixes = [
                    "data:application/octet-stream;base64,",
                    "application/gltf-buffer",
                ]
                for prefix in prefixes:
                    if uri.startswith(prefix):
                        return base64.b64decode(uri[len(prefix) :])[
                            byte_offset : byte_offset + byte_length
                        ]
        return self.binary[byte_offset : byte_offset + byte_length]

    def get_accessor_size(self, accessor_id):
        component_type_size = {GLTF_INT: 4, GLTF_FLOAT: 4, GLTF_USHORT: 2}
        type_size = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
        return (
            self.get_accessor_count(accessor_id)
            * component_type_size[self.get_accessor_component_type(accessor_id)]
            * type_size[self.get_accessor_type(accessor_id)]
        )

    def get_accessor_data(self, accessor_id):
        accessor = self.get_accessor(accessor_id)
        data = self.get_buffer_view_data(accessor["bufferView"])
        size = self.get_accessor_size(accessor_id)
        offset = 0
        if "byteOffset" in accessor:
            offset = accessor["byteOffset"]
        return data[offset : offset + size]

    def _make_node_orphan(self, node_id):
        for node in self.gltf["nodes"]:
            if "children" in node:
                if node_id in node["children"]:
                    node["children"] = list(
                        filter(lambda x: x != node_id, node["children"])
                    )
                    if not node["children"]:
                        del node["children"]

    def get_json(self):
        return json.dumps(self.gltf, indent=True)

    def _cleanup(self):
        for check in (
            "images",
            "materials",
            "skins",
            "textures",
            "samplers",
            "animations",
            "meshes",
            "accessors",
            "bufferViews",
            "buffers",
            "extensions",
        ):
            if check in self.gltf and not self.gltf[check]:
                del self.gltf[check]

    def get_node_by_name(self, name):
        for node_id, node in enumerate(self.gltf["nodes"]):
            if node.get("name") == name:
                return node_id, node
        return None, None

    def get_inverse_bind_matrix(self, skin_id, joint_id):
        skin = self.get_skin(skin_id)
        data = self.get_accessor_data(skin["inverseBindMatrices"])
        items = struct.unpack("{0}f".format(len(data) // 4), data)
        offset = joint_id * 16
        return items[offset : offset + 16]

    def get_inverse_bind_matrices(self, skin_id):
        skin = self.get_skin(skin_id)
        return self.get_accessor_data(skin["inverseBindMatrices"])

    def get_joint_from_node(self, skin_id, node_id):
        skin = self.get_skin(skin_id)
        return skin["joints"].index(node_id)

    def get_joints(self, skin_id):
        skin = self.get_skin(skin_id)
        return skin["joints"]

    def get_node_from_joint(self, skin_id, joint_id):
        skin = self.get_skin(skin_id)
        return skin["joints"][joint_id]

    def get_animation_sampler_frames(self, sampler, path):
        frames = []
        _input = self.get_accessor_data(sampler["input"])
        _output = self.get_accessor_data(sampler["output"])
        times = struct.unpack("{0}f".format(len(_input) // 4), _input)
        if path in ("translation", "scale"):
            values = struct.unpack("{0}f".format(len(_output) // 4), _output)
            for i in range(0, len(times)):
                frames.append([times[i], values[i * 3 : i * 3 + 3]])
        elif path == "rotation":
            values = struct.unpack("{0}f".format(len(_output) // 4), _output)
            for i in range(0, len(times)):
                frames.append([times[i], values[i * 4 : i * 4 + 4]])
        return frames

    def get_skin(self, skin_id):
        return self.gltf["skins"][skin_id]

    def get_node(self, node_id):
        return self.gltf["nodes"][node_id]

    def get_nodes(self):
        return self.gltf["nodes"]

    def get_materials(self):
        return self.gltf["materials"]

    def get_textures(self):
        return self.gltf["textures"]

    def get_samplers(self):
        return self.gltf["samplers"]

    def get_images(self):
        return self.gltf["images"]

    def get_image(self, image_id):
        return self.gltf["images"][image_id]

    def get_image_data(self, image_id):
        image = self.get_image(image_id)
        return self.get_buffer_view_data(image["bufferView"]), image["mimeType"]

    def get_meshes(self):
        return self.gltf["meshes"]

    def get_mesh(self, mesh_id):
        return self.gltf["meshes"][mesh_id]

    def get_primitives(self, mesh_id):
        return self.gltf["meshes"][mesh_id]["primitives"]

    def clear_animations(self):
        self.gltf["animations"] = []

    def save(self, filename):
        if self.binary:
            self.gltf["buffers"] = [{"byteLength": len(self.binary)}]
        self._cleanup()
        with open(filename, "wb") as handle:
            chunk = json.dumps(self.gltf).encode()
            if len(chunk) % 4 != 0:
                chunk += b"\x20" * (4 - (len(chunk) % 4))
            if len(self.binary) % 4 != 0:
                self.binary += b"\x00" * (4 - (len(self.binary) % 4))

            handle.write(
                struct.pack(
                    "<III",
                    0x46546C67,
                    2,
                    len(chunk) + len(self.binary) + 12 + 8 + (8 if self.binary else 0),
                )
            )

            # chunk 0
            handle.write(struct.pack("<II", len(chunk), 0x4E4F534A))
            handle.write(chunk)
            # chunk 1
            if self.binary:
                handle.write(struct.pack("<II", len(self.binary), 0x004E4942))
                handle.write(self.binary)

    def save_zip(self, filename):
        name = os.path.splitext(os.path.basename(filename))[0]
        with ZipFile(filename, "w", compression=ZIP_DEFLATED) as zip:
            if self.binary:
                self.gltf["buffers"] = [
                    {"byteLength": len(self.binary), "uri": "{0}.bin".format(name)}
                ]
                zip.writestr("{0}.bin".format(name), self.binary)
            self._cleanup()
            zip.writestr("{0}.gltf".format(name), json.dumps(self.gltf).encode())

    def save_embedded(self, filename, indent=True):
        if self.binary:
            self.gltf["buffers"] = [
                {
                    "byteLength": len(self.binary),
                    "uri": "data:application/octet-stream;base64,"
                    + base64.b64encode(self.binary).decode(),
                }
            ]
        self._cleanup()
        with open(filename, "wb") as handle:
            handle.write(json.dumps(self.gltf, indent=indent).encode())
