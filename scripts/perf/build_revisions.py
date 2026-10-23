# Builds the tests of git revisions, each in a tree of its own, for run_benchmark_rounds.py: <out>/bin/<label>.
# A label is the revision's short hash, with the patch's file name appended where --patch is given.
# A tree is the revision's `git archive`, with cpputils, cpp-template-utils and qtutils linked beside it at their current state.
# This checkout's run_tests script builds every tree, the revision's own .pro: the Qt kit and toolchain come as run_tests finds them.
# A tree keeps its build: a listing reads the executable there, its .pdb beside it.
# A failed build is reported with its log and the other revisions go on.
# Example, the commits that changed the resizer since 963b54b, oldest first:
#   python build_revisions.py --out ../scan $(git log --reverse --format=%h 963b54b^..HEAD -- resize)
import argparse
import io
import platform
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[2]
DEPENDENCIES = ("cpputils", "cpp-template-utils", "qtutils")
EXECUTABLE_NAME = "image-processing-tests"


def git(*arguments):
    return subprocess.run(["git", "-C", str(REPOSITORY), *arguments], capture_output=True, check=True).stdout


def link_directory(link, target):
    if platform.system() == "Windows":
        # A symlink needs a privilege there; a junction does not
        subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], check=True, stdout=subprocess.DEVNULL)
    else:
        link.symlink_to(target, target_is_directory=True)


def build_command(tree):
    if platform.system() == "Windows":
        return ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(tree / "scripts" / "run_tests.ps1"), "--build-only"]
    return ["bash", str(tree / "scripts" / "run_tests.sh"), "--build-only"]


def built_executable(tests):
    binaries = tests / "bin" / "release"
    # macOS builds an app bundle
    for candidate in (binaries / f"{EXECUTABLE_NAME}.exe", binaries / f"{EXECUTABLE_NAME}.app" / "Contents" / "MacOS" / EXECUTABLE_NAME, binaries / EXECUTABLE_NAME):
        if candidate.is_file():
            return candidate
    return None


# Returns the log's path on a failure, None on success
def build_revision(commit, label, patch, out, dependencies):
    root = out / "trees" / label
    log = out / "logs" / f"{label}.log"
    if root.exists():
        shutil.rmtree(root)
    tree = root / "image-processing"
    tree.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(git("archive", "--format=tar", commit))) as archive:
        archive.extractall(tree)
    # The runner's git-ignored local-env files go with it
    (tree / "scripts").mkdir(exist_ok=True)
    for script in [*(REPOSITORY / "scripts").glob("run_tests.*"), *(REPOSITORY / "scripts").glob("local-env.*")]:
        shutil.copy2(script, tree / "scripts" / script.name)
    for dependency in DEPENDENCIES:
        link_directory(root / dependency, dependencies / dependency)

    with open(log, "w") as output:
        if patch and subprocess.run(["git", "apply", str(patch)], cwd=tree, stdout=output, stderr=subprocess.STDOUT).returncode != 0:
            return log
        succeeded = subprocess.run(build_command(tree), cwd=tree, stdout=output, stderr=subprocess.STDOUT).returncode == 0
    # A wrapper's exit code can hide a failed compile
    if not succeeded or re.search(r"\berror\b", log.read_text(errors="replace"), re.IGNORECASE):
        return log

    executable = built_executable(tree / "tests")
    if executable is None:
        return log
    shutil.copy2(executable, out / "bin" / (label + executable.suffix))
    return None


def main():
    parser = argparse.ArgumentParser(description="Builds the tests of git revisions, each in a tree of its own.")
    parser.add_argument("revisions", nargs="+", help="commits, oldest first by convention: the labels' order for find_benchmark_steps.py")
    parser.add_argument("--out", type=Path, required=True, help="directory for bin/<label>, trees/<label> and logs/<label>.log")
    parser.add_argument("--patch", type=Path, help="a diff applied to every tree, as git diff writes it; its file name joins the label")
    parser.add_argument("--dependencies", type=Path, default=REPOSITORY.parent,
        help="directory holding cpputils, cpp-template-utils and qtutils (default: %(default)s)")
    parser.add_argument("--rebuild", action="store_true", help="build labels whose executable is already in <out>/bin")
    arguments = parser.parse_args()

    for dependency in DEPENDENCIES:
        if not (arguments.dependencies / dependency).is_dir():
            raise SystemExit(f"Not found: {arguments.dependencies / dependency}")
    if arguments.patch and not arguments.patch.is_file():
        raise SystemExit(f"Not found: {arguments.patch}")
    out = arguments.out.resolve()
    for directory in ("bin", "trees", "logs"):
        (out / directory).mkdir(parents=True, exist_ok=True)

    failed = []
    for revision in arguments.revisions:
        commit = git("rev-parse", "--short", revision).decode().strip()
        label = commit + (f"-{arguments.patch.stem}" if arguments.patch else "")
        if not arguments.rebuild and ((out / "bin" / label).is_file() or (out / "bin" / f"{label}.exe").is_file()):
            print(f"{label}: already built", flush=True)
            continue
        patch = arguments.patch.resolve() if arguments.patch else None
        log = build_revision(commit, label, patch, out, arguments.dependencies.resolve())
        print(f"{label}: {'FAILED, see ' + str(log) if log else 'built'}", flush=True)
        if log:
            failed.append(label)

    if failed:
        print(f"Failed: {' '.join(failed)}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
