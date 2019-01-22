from distutils.core import setup
from Cython.Build import cythonize

setup(name='compute stats app', ext_modules=cythonize('compute_stats.pyx'))
