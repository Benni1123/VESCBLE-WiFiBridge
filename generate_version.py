Import("env")
import re
import os

def after_build(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    build_dir   = env.subst("$BUILD_DIR")

    version_h = os.path.join(project_dir, "src", "version.h")

    version = "1.0.0"
    try:
        with open(version_h, "r") as f:
            content = f.read()
            match = re.search(r'FIRMWARE_VERSION\s+"(\d+)\.(\d+)\.(\d+)"', content)
            if match:
                major = int(match.group(1))
                minor = int(match.group(2))
                patch = int(match.group(3))
                version = f"{major}.{minor}.{patch}"
    except Exception as e:
        print(f">>> Warnung: version.h nicht lesbar: {e}")
        return

    # version.txt im Build-Ordner schreiben
    version_file = os.path.join(build_dir, "version.txt")
    with open(version_file, "w") as f:
        f.write(version)

    # Patch-Version in version.h automatisch erhöhen
    new_patch = patch + 1
    new_version = f"{major}.{minor}.{new_patch}"
    try:
        with open(version_h, "r") as f:
            content = f.read()
        new_content = re.sub(
            r'(FIRMWARE_VERSION\s+")(\d+\.\d+\.\d+)(")',
            f'\\g<1>{new_version}\\3',
            content
        )
        with open(version_h, "w") as f:
            f.write(new_content)
        print(f"\n>>> Build Version:   {version}")
        print(f">>> Nächste Version: {new_version} (version.h aktualisiert)")
        print(f">>> version.txt:     {version_file}\n")
    except Exception as e:
        print(f">>> Warnung: version.h nicht schreibbar: {e}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", after_build)