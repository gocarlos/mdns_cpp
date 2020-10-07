#!/usr/bin/env python

import os.path
from conans import ConanFile, CMake, tools


class MdnsCppCppTestConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = ["cmake_find_package", "cmake", "cmake_paths"]

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if not tools.cross_building(self.settings):
            bin_path = os.path.join("bin", "mdns_cpp_app")
            self.run(bin_path, run_environment=True)
