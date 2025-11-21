Import("env")

import random
import string

def generate_random_suffix():
    """Generate a random 5-character suffix"""
    return ''.join(random.choice(string.ascii_letters + string.digits) for _ in range(5))

print("=" * 60)
print("Running generate_device_name.py pre-build script")
print("=" * 60)

# Get the current build flags
build_flags = env.get("BUILD_FLAGS", [])

# Look for DEVICE_NAME in build_flags
device_name_found = False
for i, flag in enumerate(build_flags):
    if isinstance(flag, str) and flag.startswith('-DDEVICE_NAME='):
        device_name_found = True
        # Extract the base name (remove -DDEVICE_NAME= prefix and all quotes)
        raw_value = flag.replace('-DDEVICE_NAME=', '')
        # Strip outer single quotes if present
        if raw_value.startswith("'") and raw_value.endswith("'"):
            raw_value = raw_value[1:-1]
        # Strip inner double quotes if present
        if raw_value.startswith('"') and raw_value.endswith('"'):
            raw_value = raw_value[1:-1]

        base_name = raw_value
        suffix = generate_random_suffix()
        new_name = f'{base_name}-{suffix}'

        # Update the flag with proper quoting: -DDEVICE_NAME='"NewName"'
        build_flags[i] = f"-DDEVICE_NAME='\"{new_name}\"'"

        print(f'Modified DEVICE_NAME: "{base_name}" -> "{new_name}"')
        break

if device_name_found:
    # Update the environment with modified flags
    env.Replace(BUILD_FLAGS=build_flags)
    print("Device name successfully updated!")
else:
    print("WARNING: DEVICE_NAME not found in build flags")

print("=" * 60)