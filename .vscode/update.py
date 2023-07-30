#!/usr/bin/env python3

import json, os, subprocess

dir_path = os.path.dirname(os.path.realpath(__file__))

with open(dir_path + "/c_cpp_properties.json", "r+") as f:
    data = json.load(f)

    data["env"]["uname"] = os.popen("uname -r").read().strip()

    p1 = subprocess.Popen("gcc -dumpversion".split(), stdout=subprocess.PIPE)
    p2 = subprocess.Popen(
        "cut -f1 -d.".split(), stdin=p1.stdout, stdout=subprocess.PIPE
    )
    out, err = p2.communicate()

    data["env"]["gcc-dumpversion"] = out.decode("ascii").strip()

    f.seek(0)
    f.truncate()
    json.dump(data, f, indent=2)
