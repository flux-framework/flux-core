#!/usr/bin/env python3
#
#  Generate a build matrix for use with github workflows
#

from copy import deepcopy
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
)

DOCKER_REPO = "fluxrm/flux-core"


def on_master_or_tag(matrix):
    return matrix.branch == "master" or matrix.tag


DEFAULT_MULTIARCH_PLATFORMS = {
    "linux/arm64": {
        "when": on_master_or_tag,
        "suffix": " - arm64",
        "command_args": "--install-only ",
        "timeout_minutes": 90,
    },
    "linux/amd64": {"when": lambda _: True},
}


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
        image=None,
        args=default_args,
        jobs=6,
        env=None,
        docker_tag=False,
        test_s3=False,
        coverage=False,
        coverage_flags=None,
        recheck=True,
        platform=None,
        command_args="",
        timeout_minutes=60,
    ):
        """Add a build to the matrix.include array"""

        # Extra environment to add to this command:
        # NOTE: ensure we copy the dict rather than modify, re-used dicts can cause
        #       overwriting
        env = dict(env) if env is not None else {}

        # hwloc tries to look for opengl devices  by connecting to a port that might
        # sometimes be an x11 port, but more often for us is munge, turn it off
        env["HWLOC_COMPONENTS"] = "-gl"
        # the psm3 connector added to libfabrics in ~1.12 causes errors when allowed to
        # use non-local connectors on a system with virtual NICs, since we're in a
        # docker container, prevent this
        env["PSM3_HAL"] = "loopback"

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
                "timeout_minutes": timeout_minutes,
            }
        )

    def add_multiarch_build(
        self,
        name: str,
        platforms=DEFAULT_MULTIARCH_PLATFORMS,
        default_suffix="",
        image=None,
        docker_tag=True,
        **kwargs,
    ):
        for p, args in platforms.items():
            if args["when"](self):
                suffix = args.get("suffix", default_suffix)
                self.add_build(
                    name + suffix,
                    platform=p,
                    docker_tag=docker_tag,
                    image=image if image is not None else name,
                    command_args=args.get("command_args", ""),
                    timeout_minutes=args.get("timeout_minutes", 30),
                    **kwargs,
                )

    def __str__(self):
        """Return compact JSON representation of matrix"""
        return json.dumps(
            {"include": self.matrix}, skipkeys=True, separators=(",", ":")
        )


matrix = BuildMatrix()

# Multi-arch builds, arm only builds on
bookworm_platforms = deepcopy(DEFAULT_MULTIARCH_PLATFORMS)
bookworm_platforms["linux/386"] = {"when": lambda _: True, "suffix": " - 32 bit"}
common_args = (
    "--prefix=/usr"
    " --sysconfdir=/etc"
    " --with-systemdsystemunitdir=/etc/systemd/system"
    " --localstatedir=/var"
    " --with-flux-security"
)
matrix.add_multiarch_build(
    name="bookworm",
    default_suffix=" - test-install",
    platforms=bookworm_platforms,
    args=common_args,
    env=dict(
        TEST_INSTALL="t",
    ),
)

matrix.add_multiarch_build(
    name="noble",
    default_suffix=" - test-install",
    args=common_args,
    env=dict(
        TEST_INSTALL="t",
    ),
)
matrix.add_multiarch_build(
    name="el9",
    default_suffix=" - test-install",
    args=common_args,
    env=dict(
        TEST_INSTALL="t",
    ),
)
matrix.add_multiarch_build(
    name="alpine",
    default_suffix=" - test-install",
    args=common_args,
    env=dict(
        TEST_INSTALL="t",
    ),
)
# single arch builds that still produce a container
matrix.add_build(
    name="fedora40 - test-install",
    image="fedora40",
    args=common_args,
    env=dict(
        TEST_INSTALL="t",
    ),
    docker_tag=True,
)

# Ubuntu: TEST_INSTALL
matrix.add_build(
    name="jammy - test-install",
    image="jammy",
    env=dict(
        TEST_INSTALL="t",
    ),
    args="--with-flux-security",
    docker_tag=True,
)

# Ubuntu 20.04: py3.8, deprecated
matrix.add_build(
    name="focal - py3.8",
    image="focal",
    env=dict(PYTHON_VERSION="3.8"),
    docker_tag=True,
)


# Debian: gcc-12, content-s3, distcheck
matrix.add_build(
    name="bookworm - gcc-12,content-s3,distcheck",
    image="bookworm",
    env=dict(
        CC="gcc-12",
        CXX="g++12",
        DISTCHECK="t",
    ),
    args="--with-flux-security",
    test_s3=True,
)

# fedora40: clang-18
matrix.add_build(
    name="fedora40 - clang-18",
    image="fedora40",
    env=dict(
        CC="clang-18",
        CXX="clang++-18",
        CFLAGS="-O2 -gdwarf-4",
        chain_lint="t",
    ),
    args="--with-flux-security",
    command_args="--workdir=/usr/src/" + "workdir/" * 15,
)

# coverage
matrix.add_build(
    name="coverage",
    image="bookworm",
    coverage_flags="ci-basic",
    coverage=True,
    args="--with-flux-security",
)

# RHEL8 clone
matrix.add_build(
    name="el8 - ascii",
    image="el8",
    env=dict(PYTHON_VERSION="3.6", LDFLAGS="-Wl,-z,relro  -Wl,-z,now"),
    args="--enable-broken-locale-mode",
)

# el8 - test install
matrix.add_build(
    name="el8 - test-install",
    image="el8",
    env=dict(
        TEST_INSTALL="t",
    ),
    args="--with-flux-security",
    docker_tag=True,
)

# RHEL8 clone, system, coverage
matrix.add_build(
    name="el8 - system,coverage",
    coverage_flags="ci-system",
    image="el8",
    coverage=True,
    command_args="--system",
    args="--with-flux-security",
)

# inception
matrix.add_build(
    name="inception",
    image="fedora40",
    command_args="--inception",
)

print(matrix)
