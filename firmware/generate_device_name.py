import random
import string

def generate_random_suffix():
    return ''.join(random.choice(string.ascii_letters + string.digits) for _ in range(5))

def pre_build(env):
    build_flags = env.get('BUILD_FLAGS', [])
    device_name_match = None
    for flag in build_flags:
        if flag.startswith('-DDEVICE_NAME='):
            device_name_match = flag
            break
    if device_name_match:
        base_name = device_name_match.split('=', 1)[1].strip('"')
        suffix = generate_random_suffix()
        new_name = f'"{base_name}-{suffix}"'
        idx = build_flags.index(device_name_match)
        build_flags[idx] = f'-DDEVICE_NAME={new_name}'
        env['BUILD_FLAGS'] = build_flags