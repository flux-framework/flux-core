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

    def create_docker_tag(self, image, env, command, platform):
        """Create docker tag string if this is master branch or a tag"""
        if self.branch == "master" or self.tag:
            tag = f"{DOCKER_REPO}:{image}"
            if self.tag:
                tag += f"-{self.tag}"
            if platform is not None:
                tag += "-" + platform.split("/")[1]
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
        image="bookworm",
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
            docker_tag, command = self.create_docker_tag(image, env, command, platform)

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

# Debian: no args
matrix.add_build(name="bookworm")

# Debian: 32b
matrix.add_build(
    name="bookworm - 32 bit",
    image="bookworm",
    platform="linux/386",
    docker_tag=True,
)

# Debian: arm64, expensive, only on master and tags, only install
if matrix.branch == "master" or matrix.tag:
    matrix.add_build(
        name="bookworm - arm64",
        image="bookworm",
        platform="linux/arm64",
        docker_tag=True,
        command_args="--install-only ",
    )

# Debian: gcc-12, content-s3, distcheck
matrix.add_build(
    name="bookworm - gcc-12,content-s3,distcheck",
    env=dict(
        CC="gcc-12",
        CXX="g++12",
        DISTCHECK="t",
    ),
    args="--with-flux-security --enable-caliper",
    test_s3=True,
)

# Ubuntu: py3.11,clang-15
matrix.add_build(
    name="bookworm - py3.11,clang-15",
    env=dict(
        CC="clang-15",
        CXX="clang++-15",
        CFLAGS="-O2 -gdwarf-4",
        PYTHON_VERSION="3.11",
        chain_lint="t",
    ),
    args="--with-flux-security",
    command_args="--workdir=/usr/src/" + "workdir/" * 15,
)

# coverage
matrix.add_build(
    name="coverage",
    coverage_flags="ci-basic",
    coverage=True,
    jobs=4,
    args="--with-flux-security --enable-caliper",
)

# Ubuntu: TEST_INSTALL
matrix.add_build(
    name="jammy - test-install",
    image="jammy",
    env=dict(
        TEST_INSTALL="t",
    ),
    docker_tag=True,
)

# Debian: TEST_INSTALL
matrix.add_build(
    name="bookworm - test-install",
    env=dict(
        TEST_INSTALL="t",
    ),
    platform="linux/amd64",
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
    jobs=4,
    command_args="--system",
    args="--with-flux-security --enable-caliper",
)

# Fedora 34
matrix.add_build(
    name="fedora34 - gcc-11.2,py3.9",
    image="fedora34",
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

# Fedora 39
# Note: caliper does not compile on Fedora 38
matrix.add_build(
    name="fedora39 - gcc-13.2,py3.12",
    image="fedora39",
    args=(
        "--prefix=/usr"
        " --sysconfdir=/etc"
        " --with-systemdsystemunitdir=/etc/systemd/system"
        " --localstatedir=/var"
        " --with-flux-security"
    ),
    env=dict(PSM3_HAL="loopback"),
    docker_tag=True,
)

matrix.add_build(
    name="alpine",
    image="alpine",
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
    image="bookworm",
    command_args="--inception",
)

print(matrix)
