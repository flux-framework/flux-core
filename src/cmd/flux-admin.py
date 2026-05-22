##############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import errno
import locale
import logging
import os
import sys
from pathlib import Path

import flux
import flux.util
from flux.conf_builtin import conf_builtin_get
from flux.rpc import RPC

# ANSI color codes
COLOR_GREEN = "\033[32m"
COLOR_RED = "\033[31m"
COLOR_YELLOW = "\033[33m"
COLOR_RESET = "\033[0m"
COLOR_DIM = "\033[2m"


def colorize(text, color, color_enabled):
    """Add ANSI color to text if color is enabled."""
    if color_enabled:
        return f"{color}{text}{COLOR_RESET}"
    return text


def print_script_list(scripts, color_enabled):
    """Print a list of scripts with executable status markers."""
    if scripts:
        for name, is_exec in scripts:
            if is_exec:
                mark = colorize("✓", COLOR_GREEN, color_enabled)
            else:
                mark = colorize("✗ not executable", COLOR_RED, color_enabled)
            print(f"    {mark} {name}")
    else:
        print(colorize("    (no scripts)", COLOR_DIM, color_enabled))


def print_script_directory(label, path, scripts, color_enabled):
    """Print script directory header and contents."""
    dir_label = colorize(f"{label}:", COLOR_DIM, color_enabled)
    dir_path = colorize(str(path), COLOR_DIM, color_enabled)
    print(f"  {dir_label} {dir_path}")
    print_script_list(scripts, color_enabled)


def print_legacy_file(legacy_path, color_enabled):
    """Print legacy file with executable status under site: label."""
    dir_label = colorize("site:", COLOR_DIM, color_enabled)
    print(f"  {dir_label}")
    is_exec = bool(legacy_path.stat().st_mode & 0o111)
    if is_exec:
        mark = colorize("✓", COLOR_GREEN, color_enabled)
    else:
        mark = colorize("✗", COLOR_RED, color_enabled)
    location = colorize("(legacy, skips site scripts)", COLOR_DIM, color_enabled)
    print(f"    {mark} {legacy_path} {location}")


def cleanup_push(args):
    """
    Add a command to run after completion of the initial program, before rc3.
    It is pushed to the front of the list of commands.

    If command was not provided as args, read one command per line from
    stdio.  Push these in reverse order to retain the order of the block of
    commands.
    """
    if args.cmdline:
        commands = [(" ".join(args.cmdline))]
    else:
        commands = [line.strip() for line in sys.stdin]

    RPC(
        flux.Flux(),
        "runat.push",
        {"name": "cleanup", "commands": commands[::-1]},
    ).get()


def get_broker_builtin_config(handle, keys):
    """Get builtin configuration from broker or fall back to local.

    Args:
        handle: Optional Flux handle to query broker
        keys: List of config keys to fetch

    Returns:
        dict mapping keys to values
    """
    # Default to local config
    config = {key: conf_builtin_get(key) for key in keys}

    # Override with broker config if handle available
    if handle:
        try:
            result = handle.rpc("broker.conf-builtin", {"keys": keys}).get()
            config.update(result["values"])
        except (OSError, KeyError):
            pass  # Keep defaults

    return config


def get_script_paths(prog_type, builtin_config=None, imp_path=None):
    """Get the directory paths where system scripts are stored.

    For testing purposes, paths can be overridden via environment variables:
    - FLUX_SYSTEM_SCRIPTS_LIBEXECDIR: Override libexecdir
    - FLUX_SYSTEM_SCRIPTS_CONFDIR: Override confdir

    Args:
        prog_type: One of "prolog", "epilog", or "housekeeping"
        builtin_config: Optional dict with libexecdir and confdir keys.
            If None, uses local conf_builtin_get()
        imp_path: Optional path to flux-imp from running instance
            (for validation)

    Returns:
        dict with keys:
            - system_path: Package-provided scripts directory
            - sysconf_path: Site configuration scripts directory
            - legacy_path: Legacy single-file path (for backwards compat)
            - imp_path_mismatch: True if imp_path doesn't match libexecdir
    """
    # Get config, defaulting to local if not provided
    if builtin_config is None:
        builtin_config = {
            "libexecdir": conf_builtin_get("libexecdir"),
            "confdir": conf_builtin_get("confdir"),
        }

    # Allow override via environment variables for testing
    libexecdir = os.environ.get(
        "FLUX_SYSTEM_SCRIPTS_LIBEXECDIR", builtin_config.get("libexecdir")
    )
    confdir = os.environ.get(
        "FLUX_SYSTEM_SCRIPTS_CONFDIR", builtin_config.get("confdir")
    )

    # libexecdir already includes /flux (e.g., /usr/libexec/flux)
    system_path = Path(libexecdir) / f"{prog_type}.d"
    flux_sysconfdir = Path(confdir) / "flux" / "system"
    legacy_path = flux_sysconfdir / prog_type
    sysconf_path = flux_sysconfdir / f"{prog_type}.d"

    # Check if imp_path matches our detected libexecdir
    imp_path_mismatch = False
    if imp_path:
        path = Path(imp_path)
        if path.name == "flux-imp" and path.parent.name == "flux":
            expected_libexec = path.parent
            if str(expected_libexec) != str(libexecdir):
                imp_path_mismatch = True

    return {
        "system_path": system_path,
        "sysconf_path": sysconf_path,
        "legacy_path": legacy_path,
        "imp_path_mismatch": imp_path_mismatch,
    }


def scan_scripts(directory):
    """Scan a directory for executable script files.

    Scripts are sorted using locale collation order to match shell behavior.

    Args:
        directory: Path object to scan

    Returns:
        List of tuples (filename, is_executable)
    """
    scripts = []
    try:
        # Sort using locale collation to match shell/flux-run-system-scripts
        entries = sorted(directory.iterdir(), key=lambda p: locale.strxfrm(p.name))
        for entry in entries:
            if entry.is_file():
                is_executable = bool(entry.stat().st_mode & 0o111)
                scripts.append((entry.name, is_executable))
    except (FileNotFoundError, NotADirectoryError, PermissionError):
        # Directory doesn't exist or not accessible, return empty list
        pass

    return scripts


def uses_script_directories(prog_type, config):
    """Check if configured command will execute scripts from directories.

    Scripts in {libexecdir}/flux/{prog}.d and {sysconfdir}/flux/system/{prog}.d
    are only executed when the command is the default flux-imp command.
    Custom commands bypass these directories entirely.

    When perilog is configured without an explicit command, it defaults to
    ["{imp_path}", "run", "{prog_type}"] if exec.imp is set.

    Args:
        prog_type: One of "prolog", "epilog", or "housekeeping"
        config: Configuration dictionary (may be None)

    Returns:
        bool: True if scripts from directories will be executed
    """
    if not config:
        return False

    command = config.get("command")

    # If no command is explicitly set in config, perilog uses the default
    # flux-imp command (assuming exec.imp is set), which executes scripts
    if not command:
        return True

    # Check if command is the default flux-imp command
    # Command may be ["flux-imp", "run", "prolog"] or
    # ["/full/path/to/flux-imp", "run", "prolog"]
    if len(command) < 2:
        return False

    # Check if first arg ends with flux-imp and second arg is "run"
    if command[0].endswith("flux-imp") and command[1] == "run":
        # Third arg should match prog_type (or be empty string for legacy)
        if len(command) >= 3:
            return command[2] == prog_type or command[2] == ""
        return False

    return False


def show_prog_config(
    prog_type,
    config,
    verbose=False,
    color_enabled=True,
    builtin_config=None,
    imp_path=None,
    warn_mismatch=False,
    plugin_loaded=True,
    status_callback=None,
):
    """Show configuration and scripts for a system script type.

    Args:
        prog_type: "prolog", "epilog", or "housekeeping"
        config: Configuration dictionary from flux config
        verbose: Show scripts even when not configured
        color_enabled: Use ANSI color codes
        builtin_config: Optional dict with broker builtin config (libexecdir, confdir)
        imp_path: Path to flux-imp from running instance (to derive libexecdir)
        warn_mismatch: Whether to show warning if imp_path doesn't match
        plugin_loaded: Whether perilog plugin is loaded (perilog only)
        status_callback: Optional function(config, color_enabled) -> str
            that returns the status info string (e.g., "per-rank=true")
    """
    # Get and scan script paths first
    paths = get_script_paths(
        prog_type, builtin_config=builtin_config, imp_path=imp_path
    )

    # Warn if there's a mismatch between running instance and detected paths
    if warn_mismatch and paths.get("imp_path_mismatch"):
        warning = colorize(
            "Warning: flux-imp path mismatch - scripts may not be found",
            COLOR_YELLOW,
            color_enabled,
        )
        print(warning, file=sys.stderr)

    # Check for legacy single-file
    legacy_path = paths["legacy_path"]
    has_legacy = legacy_path.exists() and legacy_path.is_file()

    # Scan directories
    system_scripts = scan_scripts(paths["system_path"])
    sysconf_scripts = scan_scripts(paths["sysconf_path"])

    has_scripts = has_legacy or system_scripts or sysconf_scripts
    is_configured = config and len(config) > 0

    # In non-verbose mode, don't show anything if not configured
    if not is_configured and not verbose:
        status = colorize("not configured", COLOR_DIM, color_enabled)
        print(f"▸ {prog_type}: {status}")
        print()
        return

    # Warn if configured but plugin not loaded (perilog only)
    if is_configured and not plugin_loaded:
        status = colorize("configured but inactive", COLOR_YELLOW, color_enabled)
        print(f"▸ {prog_type}: {status}")
        print(colorize("  (perilog plugin not loaded)", COLOR_YELLOW, color_enabled))
        return

    # Show configuration status
    if is_configured:
        status = colorize("enabled", COLOR_GREEN, color_enabled)
        info = status_callback(config, color_enabled) if status_callback else ""
        status_line = f"▸ {prog_type}: {status}"
        if info:
            status_line += f" ({info})"
        print(status_line)

        # If using custom command, show what will actually execute
        if not uses_script_directories(prog_type, config):
            command = config.get("command", [])
            cmd_str = " ".join(command)
            print(f"  command: {cmd_str}")
    else:
        status = colorize("not configured", COLOR_YELLOW, color_enabled)
        print(f"▸ {prog_type}: {status}")

    # Show scripts/paths if they will be executed OR in verbose mode
    # when not configured
    uses_scripts = uses_script_directories(prog_type, config)
    show_scripts = (is_configured and uses_scripts) or (
        not is_configured and verbose and has_scripts
    )

    if show_scripts:
        # When using flux-imp command, always show paths (even if empty)
        # so users can verify
        # In verbose mode when not configured, show all scripts that would
        # be run
        if system_scripts or uses_scripts:
            print_script_directory(
                "system", paths["system_path"], system_scripts, color_enabled
            )

        # Show legacy file (if present, it skips sysconf_path)
        # or sysconf scripts:
        if has_legacy:
            print_legacy_file(legacy_path, color_enabled)
        elif sysconf_scripts or uses_scripts:
            print_script_directory(
                "site", paths["sysconf_path"], sysconf_scripts, color_enabled
            )

    print()


def show_perilog_config(
    prog_type,
    config,
    verbose=False,
    color_enabled=True,
    plugin_loaded=True,
    builtin_config=None,
    imp_path=None,
    warn_mismatch=True,
):
    """Show configuration and scripts for prolog or epilog.

    Args:
        prog_type: "prolog" or "epilog"
        config: Configuration dictionary from flux config
        verbose: Show scripts even when not configured
        color_enabled: Use ANSI color codes
        plugin_loaded: Whether perilog plugin is loaded
        builtin_config: Optional dict with broker builtin config
        imp_path: Path to flux-imp from running instance (to derive libexecdir)
        warn_mismatch: Whether to show warning if imp_path doesn't match
    """

    def perilog_status_info(cfg, color_enabled):
        """Format perilog-specific status info (per-rank setting)."""
        per_rank = cfg.get("per-rank", cfg.get("per_rank", False))
        return f"per-rank={str(per_rank).lower()}"

    show_prog_config(
        prog_type,
        config,
        verbose,
        color_enabled,
        builtin_config,
        imp_path,
        warn_mismatch,
        plugin_loaded,
        perilog_status_info,
    )


def show_housekeeping_config(
    config, verbose=False, color_enabled=True, builtin_config=None, imp_path=None
):
    """Show configuration and scripts for housekeeping.

    Args:
        config: Configuration dictionary from flux config
        verbose: Show scripts even when not configured
        color_enabled: Use ANSI color codes
        builtin_config: Optional dict with broker builtin config
        imp_path: Path to flux-imp from running instance (to derive libexecdir)
    """

    def housekeeping_status_info(cfg, color_enabled):
        """Format housekeeping-specific status info (release-after setting)."""
        release_after = cfg.get("release-after")
        if release_after:
            return f"release after {release_after}"
        else:
            return "batch release"

    show_prog_config(
        "housekeeping",
        config,
        verbose,
        color_enabled,
        builtin_config,
        imp_path,
        warn_mismatch=False,
        plugin_loaded=True,
        status_callback=housekeeping_status_info,
    )


def system_scripts(args):
    """
    Show system script configuration for prolog, epilog, and housekeeping.
    """
    verbose = args.verbose
    color_enabled = args.color.enabled

    try:
        h = flux.Flux()
    except OSError:
        # If we can't connect to flux, we can still show what scripts would be
        # run based on the filesystem (in verbose mode)
        h = None

    # Try to get configuration from flux
    prolog_config = None
    epilog_config = None
    housekeeping_config = None
    perilog_plugin_loaded = False
    imp_path = None

    if h:
        # Try to get exec.imp path to derive libexecdir from running instance
        try:
            imp_path = h.conf_get("exec.imp")
        except (OSError, FileNotFoundError):
            # exec.imp not configured, use default pathsfrom conf_builtin_get()
            pass

        # Check if perilog plugin is loaded
        got_eperm = False
        try:
            result = h.rpc("job-manager.jobtap-query", {"name": "perilog.so"}).get()
            if result and "conf" in result:
                perilog_plugin_loaded = True
                prolog_config = result["conf"].get("prolog", {})
                epilog_config = result["conf"].get("epilog", {})
        except OSError as exc:
            if exc.errno == errno.EPERM:
                # Guest user can't query jobtap, need to get config from broker
                warning = colorize(
                    "Warning: only instance owner can query perilog.so status",
                    COLOR_YELLOW,
                    color_enabled,
                )
                print(warning, file=sys.stderr)
                got_eperm = True
            # Otherwise plugin not loaded (ENOENT) or other error,
            # fall through to conf_get()

        # If plugin not loaded (or got EPERM), get configs from broker
        # Note: conf_get() caches config, so subsequent calls are fast
        if not perilog_plugin_loaded:
            try:
                prolog_config = h.conf_get("job-manager.prolog")
                epilog_config = h.conf_get("job-manager.epilog")
                # If we got EPERM and config exists, assume plugin is loaded
                if got_eperm and (prolog_config or epilog_config):
                    perilog_plugin_loaded = True
            except (OSError, FileNotFoundError):
                # Config not set or not accessible, leave as None
                pass

        # Get housekeeping config (uses cached config if available)
        try:
            housekeeping_config = h.conf_get("job-manager.housekeeping")
        except (OSError, FileNotFoundError):
            # Config not set or not accessible, leave as None
            pass

    # Get broker builtin config once to avoid redundant RPCs
    builtin_config = (
        get_broker_builtin_config(h, ["libexecdir", "confdir"]) if h else None
    )

    # Show configurations (warn about path mismatch only once)
    show_perilog_config(
        "prolog",
        prolog_config,
        verbose,
        color_enabled,
        perilog_plugin_loaded,
        builtin_config,
        imp_path,
        warn_mismatch=True,
    )
    show_perilog_config(
        "epilog",
        epilog_config,
        verbose,
        color_enabled,
        perilog_plugin_loaded,
        builtin_config,
        imp_path,
        warn_mismatch=False,
    )
    show_housekeeping_config(
        housekeeping_config, verbose, color_enabled, builtin_config, imp_path
    )


LOGGER = logging.getLogger("flux-admin")


@flux.util.CLIMain(LOGGER)
def main():
    sys.stdout = open(sys.stdout.fileno(), "w", encoding="utf8")

    parser = argparse.ArgumentParser(prog="flux-admin")
    subparsers = parser.add_subparsers(
        title="subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    cleanup_push_parser = subparsers.add_parser(
        "cleanup-push", formatter_class=flux.util.help_formatter()
    )
    cleanup_push_parser.add_argument(
        "cmdline", help="Command line", nargs=argparse.REMAINDER
    )
    cleanup_push_parser.set_defaults(func=cleanup_push)

    system_scripts_parser = subparsers.add_parser(
        "system-scripts",
        formatter_class=flux.util.help_formatter(),
        help="Show configured system scripts (prolog, epilog, housekeeping)",
    )
    system_scripts_parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Show scripts even when not configured",
    )
    system_scripts_parser.add_argument(
        "--color",
        action=flux.util.ColorAction,
    )
    system_scripts_parser.set_defaults(func=system_scripts)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
