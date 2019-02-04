from distutils.core import setup
from distutils.extension import Extension
from Cython.Build import cythonize

EXT_MODULES = [
    Extension("compute_stats",
              sources=["compute_stats.pyx"],
              libraries=["gsl", "gslcblas", "curl"]
              )
]

setup(name='compute stats app', ext_modules=cythonize(EXT_MODULES))
