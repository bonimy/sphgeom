"""
Basic setuptools description.

This is not a complete definition.

* Version number is not correct.
* The shared library and include files are not installed.  This makes it
  unusable with other python packages that directly reference the C++
  interface.
"""

import glob

# Importing this automatically enables parallelized builds
import numpy.distutils.ccompiler  # noqa: F401
from setuptools import setup
from pybind11.setup_helpers import Pybind11Extension, build_ext

# Find the source code -- we can combine it into a single module
pybind_src = sorted(glob.glob("python/lsst/sphgeom/*.cc"))
cpp_src = sorted(glob.glob("src/*.cc"))

# Very inefficient approach since this compiles the maing sphgeom
# library code for every extension rather than building everything once
ext_modules = [Pybind11Extension("lsst.sphgeom._sphgeom",
                                 sorted(cpp_src + pybind_src),
                                 include_dirs=["include"])]

setup(
    ext_modules=ext_modules,
    cmdclass={'build_ext': build_ext},
)
