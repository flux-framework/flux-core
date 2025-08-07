#!/usr/bin/env python3
#
#  Generate docker manifests when necessary based on set of docker tags
#  provided by src/test/generate-matrix.py

import json
import subprocess
from collections import defaultdict

matrix = json.loads(
    subprocess.run(
        ["src/test/generate-matrix.py"], capture_output=True, text=True
    ).stdout
)

tags = defaultdict(list)
for entry in matrix["include"]:
    if "DOCKER_TAG" in entry["env"]:
        tags[entry["image"]].append(entry["env"]["DOCKER_TAG"])

# Collect only those images with multiple tags:
tags = {k: v for k, v in tags.items() if len(v) > 1}

for image in tags.keys():
    tag = f"fluxrm/flux-core:{image}"
    print(f"docker manifest create {tag} ", *tags[image])
    subprocess.run(["docker", "manifest", "create", tag, *tags[image]])
    print(f"docker manifest push {tag} ")
    subprocess.run(["docker", "manifest", "push", tag])
