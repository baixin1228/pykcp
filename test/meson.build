# 找到测试脚本
test_script = find_program('net_test.sh', dirs: [meson.current_source_dir()], required: true)

run_command('chmod', '+x', test_script)

tcp_client_test = executable('tcp_client_test', 'tcp_client_test.c')
tcp_server_test = executable('tcp_server_test', 'tcp_server_test.c')
kcp_client_test = executable(
    'kcp_client_test',
    'kcp_client_test.c',
    link_with : ikcp_lib,
    include_directories : common_includes)
kcp_server_test = executable(
    'kcp_server_test',
    'kcp_server_test.c',
    link_with : ikcp_lib,
    include_directories : common_includes)

# 修改测试脚本，将服务端和客户端可执行文件的路径作为参数传递
tcp_nodelay_args = [
    test_script.path(),
    tcp_client_test.full_path(),
    '-i 192.168.45.1',
    tcp_server_test.full_path(),
    '',
    'nodelay'
]
test(
    'tcp_test',
    find_program('bash'),
    args: tcp_nodelay_args,
    depends: [tcp_client_test, tcp_server_test],
    timeout: 15,
    is_parallel: false
)
kcp_nodelay_args = [
    test_script.path(),
    kcp_client_test.full_path(),
    '-i 192.168.45.1',
    kcp_server_test.full_path(),
    '',
    'nodelay'
]
test(
    'kcp_test',
    find_program('bash'),
    args: kcp_nodelay_args,
    depends: [kcp_client_test, kcp_server_test],
    timeout: 15,
    is_parallel: false
)
tcp_delay_args = [
    test_script.path(),
    tcp_client_test.full_path(),
    '-i 192.168.45.1',
    tcp_server_test.full_path(),
    '',
    'delay'
]
test(
    'tcp_test_delay',
    find_program('bash'),
    args: tcp_delay_args,
    depends: [tcp_client_test, tcp_server_test],
    timeout: 15,
    is_parallel: false
)
kcp_delay_args = [
    test_script.path(),
    kcp_client_test.full_path(),
    '-i 192.168.45.1',
    kcp_server_test.full_path(),
    '',
    'delay'
]
test(
    'kcp_test_delay',
    find_program('bash'),
    args: kcp_delay_args,
    depends: [kcp_client_test, kcp_server_test],
    timeout: 15,
    is_parallel: false
)
kcp_epoll_delay_args = [
    test_script.path(),
    kcp_client_test.full_path(),
    '-e  -i 192.168.45.1',
    kcp_server_test.full_path(),
    '-e ',
    'delay'
]
test(
    'kcp_test_epoll_delay',
    find_program('bash'),
    args: kcp_epoll_delay_args,
    depends: [kcp_client_test, kcp_server_test],
    timeout: 15,
    is_parallel: false
)

# 定义环境变量字典
env_vars = {
    'PYTHONPATH': kcp_wrapper_path
}
pykcp_batch_args = [
    test_script.path(),
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/batch_client.py 192.168.45.1',
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/batch_server.py',
    'nodelay'
]
test(
    'pykcp_batch_test',
    find_program('bash'),
    args: pykcp_batch_args,
    depends: [pykcp_module],
    timeout: 15,
    env: env_vars,
    is_parallel: false
)
pykcp_stress_args = [
    test_script.path(),
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/stress_client.py 192.168.45.1',
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/stress_server.py',
    'nodelay'
]
test(
    'pykcp_stress_test',
    find_program('bash'),
    args: pykcp_stress_args,
    depends: [pykcp_module],
    timeout: 15,
    env: env_vars,
    is_parallel: false
)
pykcp_echo_args = [
    test_script.path(),
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/echo_client.py 192.168.45.1',
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/echo_server.py',
    'nodelay'
]
test(
    'pykcp_echo_test',
    find_program('bash'),
    args: pykcp_echo_args,
    depends: [pykcp_module],
    timeout: 15,
    env: env_vars,
    is_parallel: false
)
pykcp_echo_atomic_args = [
    test_script.path(),
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/echo_atomic_client.py 192.168.45.1',
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/echo_atomic_server.py',
    'nodelay'
]
test(
    'pykcp_echo_atomic_test',
    find_program('bash'),
    args: pykcp_echo_atomic_args,
    depends: [pykcp_module],
    timeout: 15,
    env: env_vars,
    is_parallel: false
)
pykcp_echo_callback_args = [
    test_script.path(),
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/echo_callback_client.py 192.168.45.1',
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/echo_callback_server.py',
    'nodelay'
]
test(
    'pykcp_echo_callback_test',
    find_program('bash'),
    args: pykcp_echo_callback_args,
    depends: [pykcp_module],
    timeout: 15,
    env: env_vars,
    is_parallel: false
)
pykcp_simple_args = [
    test_script.path(),
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/simple_client.py 192.168.45.1',
    '/usr/bin/python3',
    meson.current_source_dir() + '/python/simple_server.py',
    'nodelay'
]
test(
    'pykcp_simple_test',
    find_program('bash'),
    args: pykcp_simple_args,
    depends: [pykcp_module],
    timeout: 15,
    env: env_vars,
    is_parallel: false
)