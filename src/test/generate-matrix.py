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
            return True, command

        return False, command

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
        coverage_flags=None,
        recheck=True,
        platform=None,
        command_args="",
    ):
        """Add a build to the matrix.include array"""

        # Extra environment to add to this command:
        env = env or {}

        needs_buildx = False
        if platform:
            command_args += f"--platform={platform}"
            needs_buildx = True

        # The command to run:
        command = f"{docker_run_checks} -j{jobs} --image={image} {command_args}"

        # Add --recheck option if requested
        if recheck and "DISTCHECK" not in env:
            command += " --recheck"

        if docker_tag:
            #  Only export docker_tag if this is main branch or a tag:
            docker_tag, command = self.create_docker_tag(image, env, command)

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
                "coverage_flags": coverage_flags,
                "test_s3": test_s3,
                "docker_tag": docker_tag,
                "needs_buildx": needs_buildx,
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

# Ubuntu: 32b
matrix.add_build(
    name="bionic - 32 bit",
    platform="linux/386",
)

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
    ),
    args="--with-flux-security",
    command_args="--workdir=/usr/src/" + "workdir/" * 15,
)

# coverage
matrix.add_build(
    name="coverage",
    coverage_flags="ci-basic",
    image="fedora35",
    coverage=True,
    jobs=2,
    args="--with-flux-security --enable-caliper",
)

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

# RHEL7 clone
matrix.add_build(
    name="el7",
    image="el7",
    docker_tag=True,
)

# RHEL8 clone
matrix.add_build(
    name="el8",
    image="el8",
    env=dict(PYTHON_VERSION="3.6", LDFLAGS="-Wl,-z,relro  -Wl,-z,now"),
    docker_tag=True,
)

# RHEL8 clone
matrix.add_build(
    name="el8 - ascii",
    image="el8",
    args="--enable-broken-locale-mode",
)

# RHEL8 clone, system, coverage
matrix.add_build(
    name="el8 - system,coverage",
    coverage_flags="ci-system",
    image="el8",
    coverage=True,
    jobs=2,
    command_args="--system",
    args="--with-flux-security --enable-caliper",
)

# Fedora 33
matrix.add_build(
    name="fedora33 - gcc-10,py3.9",
    image="fedora33",
    docker_tag=True,
)

# Fedora 34
matrix.add_build(
    name="fedora34 - gcc-11.2,py3.9",
    image="fedora34",
    docker_tag=True,
)

# Fedora 35
matrix.add_build(
    name="fedora35 - gcc-11.2,py3.10",
    image="fedora35",
    docker_tag=True,
)

# Fedora 38
# Note: caliper does not compile on Fedora 38
matrix.add_build(
    name="fedora38 - gcc-13.1,py3.11",
    image="fedora38",
    args=(
        "--prefix=/usr"
        " --sysconfdir=/etc"
        " --with-systemdsystemunitdir=/etc/systemd/system"
        " --localstatedir=/var"
        " --with-flux-security"
    ),
    docker_tag=True,
)

# inception
matrix.add_build(
    name="inception",
    image="bionic",
    command_args="--inception",
)

print(matrix)
