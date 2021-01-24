import sys
import os
import useless
from useless.recipe import Recipe

if __name__ == '__main__':
    with Recipe('Release', '.') as r:
        assert r
        r.require('openexr')
        r.require('glm')
        r.require('cereal')
        r.require('spdlog')
        r.require('stb')
        r.require('cxxopts')
        r.require('assimp')
        r.require('pybind11')
        r.require('openvdb')
        if 'embree' in sys.argv[1:]:
            r.require('embree')
