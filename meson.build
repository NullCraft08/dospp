# 顶层构建。目前就一个目标：vm/ 下的 dosvm。
# 内核那些目录（boot、mm、arch……）等真开始写代码再加，
# 现在建只会摆一堆空目录占地儿。

project('dospp', ['c', 'cpp'],
  version : '0.0.1',
  meson_version : '>= 1.1',
  default_options : [
    'c_std=c17',
    'cpp_std=c++20',
    'warning_level=3',
  ])

subdir('vm')
