
#!/usr/bin/env python

from conans import ConanFile, CMake, tools
import subprocess


class MdnsCpp(ConanFile):
    name = "mdns_cpp"
    version = "0.1.0"
    license = "MIT"
    url = "https://github.com/gocarlos/mdns_cpp"
    description = "Provides a cross-platform mDNS and DNS-DS library in C++"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}
    generators = ["cmake", "cmake_find_package", "cmake_paths"]
    exports_sources = "CMakeLists.txt", "cmake/*", "src/*", "include/*", "conanfile.txt"

    def build(self):
        cmake = CMake(self)
        cmake.definitions["MDNS_CPP_BUILD_EXAMPLE"] = False
        cmake.configure(source_folder=".")
        cmake.build()

    def package(self):
        self.copy("*.hpp", src=".")
        self.copy("*mdns_cpp.lib", dst="lib", keep_path=False)
        self.copy("*.dll", dst="bin", keep_path=False)
        self.copy("*.so", dst="lib", keep_path=False)
        self.copy("*.dylib", dst="lib", keep_path=False)
        self.copy("*.a", dst="lib", keep_path=False)

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)
        if self.settings.os == "Linux":
            self.cpp_info.system_libs = ["m", "pthread"]
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["iphlpapi", "ws2_32"]
