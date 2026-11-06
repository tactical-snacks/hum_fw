# PlatformIO pre-build script: passes the git revision to the firmware as FW_REV
# (short hash, or tag + commits + hash once tagged; "-dirty" with uncommitted changes)
import subprocess

Import("env")  # noqa: F821 (provided by PlatformIO)


def git_rev():
    try:
        return subprocess.check_output(["git", "describe", "--always", "--dirty", "--tags"],
                                       cwd=env["PROJECT_DIR"], text=True).strip()  # noqa: F821
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


env.Append(CPPDEFINES=[("FW_REV", env.StringifyMacro(git_rev()))])  # noqa: F821
