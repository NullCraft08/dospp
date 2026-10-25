# dosvm 是个普通用户态程序，跟内核没关系，不用交叉编译，
# 直接拿宿主编译器编就行，也方便在 Windows 上直接跑。
#
# "." 也要进 include 路径，这样 cpu/regs.h 这种写法才找得到；
# "../include" 是给 dospp/types.h 用的。

inc = include_directories('../include', '.')

# CPU 核单独编成静态库，将来 dosvm 和内核里的用法都从这里链
cpu_lib = static_library('cpu',
  'cpu/exec.cpp',
  include_directories : inc,
)

# 虚拟机外壳：装载器 + INT 21h + 句柄表 + 宿主文件系统
vm_lib = static_library('vmcore',
  'vm.cpp',
  'dos/loader.cpp',
  'dos/int21.cpp',
  'dos/files.cpp',
  'dos/hostfs.cpp',
  'dos/psp.cpp',
  'dos/exe.cpp',
  'dos/int10.cpp',
  'dev/vga.cpp',
  include_directories : inc,
)

dosvm = executable('dosvm',
  'main.cpp',
  include_directories : inc,
  link_with : [cpu_lib, vm_lib],
)

# 测试分开编：不依赖 main.cpp，也不用读文件。
test_cpu = executable('test_cpu',
  'tests/test_cpu.cpp',
  include_directories : inc,
)

test_exec = executable('test_exec',
  'tests/test_exec.cpp',
  include_directories : inc,
  link_with : cpu_lib,
)

test_files = executable('test_files',
  'tests/test_files.cpp',
  include_directories : inc,
  link_with : [cpu_lib, vm_lib],
)

test_vm = executable('test_vm',
  'tests/test_vm.cpp',
  include_directories : inc,
  link_with : [cpu_lib, vm_lib],
)

test_int21 = executable('test_int21',
  'tests/test_int21.cpp',
  include_directories : inc,
  link_with : [cpu_lib, vm_lib],
)

test('cpu', test_cpu)
test('exec', test_exec)
test('files', test_files)
test('vm', test_vm)
test_exe = executable('test_exe',
  'tests/test_exe.cpp',
  include_directories : inc,
  link_with : [cpu_lib, vm_lib],
)

test('int21', test_int21)
test_vga = executable('test_vga',
  'tests/test_vga.cpp',
  include_directories : inc,
  link_with : [cpu_lib, vm_lib],
)

test('exe', test_exe)
test_pmode = executable('test_pmode',
  'tests/test_pmode.cpp',
  include_directories : inc,
  link_with : cpu_lib,
)

test('vga', test_vga)
test_intr = executable('test_intr',
  'tests/test_intr.cpp',
  include_directories : inc,
  link_with : cpu_lib,
)

test('pmode', test_pmode)
test_priv = executable('test_priv',
  'tests/test_priv.cpp',
  include_directories : inc,
  link_with : cpu_lib,
)

test('intr', test_intr)
test_paging = executable('test_paging',
  'tests/test_paging.cpp',
  include_directories : inc,
  link_with : cpu_lib,
)

test('priv', test_priv)
test_v86 = executable('test_v86',
  'tests/test_v86.cpp',
  include_directories : inc,
  link_with : cpu_lib,
)

test('paging', test_paging)
test('v86', test_v86)
