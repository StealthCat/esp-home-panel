#!/usr/bin/env python3
"""Render the production settings handler using the existing host fixture."""
import os
from pathlib import Path
import subprocess
import tempfile
import test_color_settings as fixture

with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory)
    (tmp/'stubs.h').write_text(fixture.STUBS)
    for name in ('esp_err.h','esp_heap_caps.h','esp_log.h','esp_mac.h','nvs.h','sdkconfig.h','esp_http_server.h','esp_system.h','freertos/FreeRTOS.h','freertos/task.h'):
        p=tmp/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('#include "stubs.h"\n')
    source=fixture.HARNESS.split('int main(void)')[0]+'''int main(void) {
        assert(settings_init()==ESP_OK);
        httpd_req_t request={0};
        assert(root_handler(&request)==ESP_OK);
        puts(response);
        return 0;
    }'''
    (tmp/'render.c').write_text(source)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-I'+str(tmp),'-I'+str(fixture.ROOT/'main'),str(tmp/'render.c'),'-o',str(tmp/'render')],check=True)
    subprocess.run([str(tmp/'render')],check=True)
