#!/usr/bin/env python3
#
#  Generate a build matrix for use with github workflows
#

import json
import os
import re

docker_run_checks = "src/test/docker/docker-run-checks.sh"

default_args = (
    "--prefix=/usr"
    " --sysconfdir=/etc"
    " --with-systemdsystemunitdir=/etc/systemd/system"
    " --localstatedir=/var"
    " --with-flux-security"
    " --enable-caliper"
)

DOCKER_REPO = "fluxrm/flux-core"


class BuildMatrix:
    def __init__(self):
        self.matrix = []
        self.branch = None
        self.tag = None

        #  Set self.branch or self.tag based on GITHUB_REF
        if "GITHUB_REF" in os.environ:
            self.ref = os.environ["GITHUB_REF"]
            match = re.search("^refs/heads/(.*)", self.ref)
            if match:
                self.branch = match.group(1)
            match = re.search("^refs/tags/(.*)", self.ref)
            if match:
                self.tag = match.group(1)

    def create_docker_tag(self, image, env, command):
        """Create docker tag string if this is master branch or a tag"""
        if self.branch == "master" or self.tag:
            tag = f"{DOCKER_REPO}:{image}"
            if self.tag:
                tag += f"-{self.tag}"
            env["DOCKER_TAG"] = tag
            command += f" --tag={tag}"
            return True

        return False

    def env_add_s3(self, args, env):
        """Add necessary environment and args to test content-s3 module"""
        env.update(
            dict(
                S3_ACCESS_KEY_ID="minioadmin",
                S3_SECRET_ACCESS_KEY="minioadmin",
                S3_HOSTNAME="127.0.0.1:9000",
                S3_BUCKET="flux-minio",
            )
        )
        args += " --enable-content-s3"
        return args

    def add_build(
        self,
        name=None,
        image="bionic",
        args=default_args,
        jobs=4,
        env=None,
        docker_tag=False,
        test_s3=False,
        coverage=False,
    ):
        """Add a build to the matrix.include array"""

        # Extra environment to add to this command:
        env = env or {}

        # The command to run:
        command = f"{docker_run_checks} -j{jobs} --image={image}"

        if docker_tag:
            #  Only export docker_tag if this is main branch or a tag:
            docker_tag = self.create_docker_tag(image, env, command)

        if test_s3:
            args = self.env_add_s3(args, env)

        if coverage:
            env["COVERAGE"] = "t"

        create_release = False
        if self.tag and "DISTCHECK" in env:
            create_release = True

        command += f" -- --enable-docs {args}"

        self.matrix.append(
            {
                "name": name,
                "env": env,
                "command": command,
                "image": image,
                "tag": self.tag,
                "branch": self.branch,
                "coverage": coverage,
                "test_s3": test_s3,
                "docker_tag": docker_tag,
                "create_release": create_release,
            }
        )

    def __str__(self):
        """Return compact JSON representation of matrix"""
        return json.dumps(
            {"include": self.matrix}, skipkeys=True, separators=(",", ":")
        )


matrix = BuildMatrix()

# Ubuntu: no args
matrix.add_build(name="bionic")

# Ubuntu: gcc-8, content-s3, distcheck
matrix.add_build(
    name="bionic - gcc-8,content-s3,distcheck",
    env=dict(
        CC="gcc-8",
        CXX="g++8",
        DISTCHECK="t",
    ),
    args="--with-flux-security --enable-caliper",
    test_s3=True,
)

# Ubuntu: py3.7,clang-6.0
matrix.add_build(
    name="bionic - py3.7,clang-6.0",
    env=dict(
        CC="clang-6.0",
        CXX="clang++-6.0",
        PYTHON_VERSION="3.7",
        chain_lint="t",
        TEST_CHECK_PREREQS="t",
    ),
    args="--with-flux-security",
)

# Ubuntu: coverage
matrix.add_build(name="coverage", coverage=True, jobs=2)

# Ubuntu: TEST_INSTALL
matrix.add_build(
    name="bionic - test-install",
    env=dict(
        TEST_INSTALL="t",
    ),
    docker_tag=True,
)

# Ubuntu 20.04: py3.8
matrix.add_build(
    name="focal - py3.8",
    image="focal",
    env=dict(PYTHON_VERSION="3.8"),
    docker_tag=True,
)

# Centos7
matrix.add_build(
    name="centos7",
    image="centos7",
    docker_tag=True,
)

# Centos8
matrix.add_build(
    name="centos8 - py3.7",
    image="centos8",
    env=dict(PYTHON_VERSION="3.6"),
    docker_tag=True,
)

print(matrix)
