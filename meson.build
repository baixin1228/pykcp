project('hprd_lib', 'c', 'cpp', default_options : ['cpp_std=c++20'])

py3 = import('python').find_installation('python3')
site_packages_dir = py3.get_path('purelib')

python_dep = dependency('python3', required: true)
pybind11_dep = dependency('pybind11', required: true)

common_includes = include_directories('include')

ikcp_lib = static_library(
  'ikcp-c',
  'libs/ikcp.c',
  include_directories : common_includes,
  install : false)

pykcp_module = py3.extension_module(
  'ikcp',
  'libs/kcp_wrapper.cpp',
  link_with : ikcp_lib,
  dependencies : [pybind11_dep, python_dep],
  install : true,
  install_dir : site_packages_dir,
  include_directories : common_includes,
  override_options : ['cpp_std=c++20'])

kcp_wrapper_path = meson.current_build_dir()

subdir('test')