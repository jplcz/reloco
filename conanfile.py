# SPDX-FileCopyrightText: 2026 Jarosław Pelczar <jarek@jpelczar.com>
#
# SPDX-License-Identifier: BSD-2-Clause

import os

from conan import ConanFile
from conan.tools.files import copy


class JplczRelocoConan(ConanFile):
    name = "jplcz_reloco"
    version = "0.1.0"
    package_type = "header-library"
    license = "BSD-2-Clause"
    url = "https://github.com/jplcz/reloco"
    homepage = "https://github.com/jplcz/reloco"
    description = (
        "Header-only Safety by Default alternatives to the C++ Standard "
        "Library: hardened containers and lifetime-safety primitives"
    )
    topics = ("safety", "embedded", "containers", "header-only")

    exports_sources = "include/**", "LICENSE"
    no_copy_source = True

    def package(self):
        copy(
            self,
            "*",
            src=os.path.join(self.source_folder, "include"),
            dst=os.path.join(self.package_folder, "include"),
        )
        copy(
            self,
            "LICENSE",
            src=self.source_folder,
            dst=os.path.join(self.package_folder, "licenses"),
        )

    def package_id(self):
        self.info.clear()

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.set_property("cmake_file_name", "jplcz_reloco")
        self.cpp_info.set_property(
            "cmake_target_name", "jplcz_reloco::reloco"
        )
