Import("env")

from pathlib import Path
import json


def _resolve_flash_file(build_dir: Path, name: str) -> Path:
    direct = build_dir / name
    if direct.exists():
        return direct

    basename = Path(name).name
    root_match = build_dir / basename
    if root_match.exists():
        return root_match

    matches = list(build_dir.rglob(basename))
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise FileNotFoundError(f"Unable to locate flash image {name!r} under {build_dir}")
    raise RuntimeError(f"Ambiguous flash image {name!r}: {matches}")


def merge_wokwi_flash(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    args_path = build_dir / "flasher_args.json"
    if not args_path.exists():
        raise FileNotFoundError(f"ESP-IDF did not generate {args_path}")

    args = json.loads(args_path.read_text(encoding="utf-8"))
    flash_files = args.get("flash_files", {})
    if not flash_files:
        raise RuntimeError(f"No flash_files found in {args_path}")

    sections = []
    end = 0
    for offset_text, filename in flash_files.items():
        offset = int(offset_text, 0)
        path = _resolve_flash_file(build_dir, filename)
        data = path.read_bytes()
        sections.append((offset, path, data))
        end = max(end, offset + len(data))

    image = bytearray(b"\xff" * end)
    for offset, path, data in sections:
        image[offset:offset + len(data)] = data
        print(f"[wokwi] merge 0x{offset:08x} {path.name} ({len(data)} bytes)")

    output = build_dir / "wokwi.bin"
    output.write_bytes(image)
    print(f"[wokwi] wrote merged flash image {output} ({len(image)} bytes)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_wokwi_flash)
