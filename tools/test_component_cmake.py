#!/usr/bin/env python3
"""Exercise component discovery and normal CMake configuration without ESP hardware."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(cmake):
    with tempfile.TemporaryDirectory(prefix='panel cmake test ') as directory:
        root = Path(directory)
        (root/'main').mkdir()
        (root/'tools').mkdir()
        (root/'assets/theme-sprites').mkdir(parents=True)
        shutil.copy2(ROOT/'main/CMakeLists.txt', root/'main/CMakeLists.txt')
        # This fixture records generation without needing firmware dependencies.
        (root/'tools/check_theme_assets.py').write_text(
            "from pathlib import Path\n"
            "p=Path(__file__).resolve().parents[1]/'generated-marker'\n"
            "p.write_text(p.read_text()+'x' if p.exists() else 'x')\n")
        (root/'tools/generate_theme_art.py').touch()
        (root/'assets/theme-sprites/manifest.json').write_text('[]')
        (root/'main/ui.c').touch()
        (root/'main/theme_art.h').touch()
        register = '''macro(idf_component_register)
    cmake_parse_arguments(COMP "" "" "SRCS;INCLUDE_DIRS;REQUIRES" ${ARGN})
    if(NOT "theme_art.c" IN_LIST COMP_SRCS OR NOT "esp_lcd" IN_LIST COMP_REQUIRES)
        message(FATAL_ERROR "Component source/requirements were lost")
    endif()
    set(REGISTERED TRUE)
endmacro()
'''
        # Deliberately invalid Python: discovery must not invoke it or find Python.
        for early in ('TRUE', 'FALSE'):
            script = root/'early.cmake'
            script.write_text('cmake_minimum_required(VERSION 3.18)\n'
                f'set(CMAKE_BUILD_EARLY_EXPANSION {early})\n'
                'set(PYTHON "nonexistent-python-must-not-run")\n'+register+
                'include("${CMAKE_CURRENT_LIST_DIR}/main/CMakeLists.txt")\n'
                'if(NOT REGISTERED)\n message(FATAL_ERROR "No component registration")\nendif()\n')
            subprocess.run([cmake, '-P', str(script)], check=True)
            assert not (root/'generated-marker').exists(), 'Generation ran in script mode'
        (root/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.18)\n'
            'project(component_test NONE)\n'+register+
            'add_subdirectory(main)\n'
            'get_property(WATCH DIRECTORY main PROPERTY CMAKE_CONFIGURE_DEPENDS)\n'
            'if(NOT WATCH MATCHES "manifest.json")\n'
            ' message(FATAL_ERROR "Asset watches are missing")\nendif()\n')
        build = root/'build'
        subprocess.run([cmake, '-S', str(root), '-B', str(build),
                        '-DPYTHON='+sys.executable], check=True)
        assert (root/'generated-marker').read_text() == 'x', 'Normal configure did not generate'
        # Use a future mtime to avoid filesystem timestamp resolution races.
        import os
        manifest = root/'assets/theme-sprites/manifest.json'
        manifest.write_text('[{}]')
        stamp = max(manifest.stat().st_mtime, (root/'generated-marker').stat().st_mtime)+2
        os.utime(manifest, (stamp, stamp))
        subprocess.run([cmake, '--build', str(build)], check=True)
        assert len((root/'generated-marker').read_text()) > 1, 'Asset edit did not reconfigure'
        print('CMake regression checks passed: discovery, normal generation, asset-change regeneration')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--cmake', default='cmake')
    run(parser.parse_args().cmake)
