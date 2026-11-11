"""Build the pinned Windows FFmpeg CLI and its corresponding source archive."""
from hashlib import file_digest, sha256
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
from tempfile import TemporaryDirectory
from zipfile import ZipFile

SCRIPT = Path(__file__).resolve()
RECIPE = SCRIPT.with_name("build_ffmpeg.sh")
ROOT = SCRIPT.parents[1]
CACHE = ROOT / "build" / "recording-runtime"
DOWNLOADS = CACHE / "downloads"

DEVKIT = (
    "w64devkit-x64-2.6.0.7z.exe",
    "https://github.com/skeeto/w64devkit/releases/download/v2.6.0/",
    "bb212f0012a9324b8cf4104a76c07199d9c17607b4c5755632108d0432359beb",
)
NASM = (
    "nasm-3.01-win64.zip",
    "https://www.nasm.us/pub/nasm/releasebuilds/3.01/win64/",
    "e0ba5157007abc7b1a65118a96657a961ddf55f7e3f632ee035366dfce039ca4",
)
X264 = "b35605ace3ddf7c1a5d67a2eb553f034aef41d55"
SOURCES = (
    ("ffmpeg-8.1.3.tar.xz", "https://www.ffmpeg.org/releases/",
     "7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3"),
    (f"x264-{X264}.tar.gz", f"https://code.videolan.org/videolan/x264/-/archive/{X264}/",
     "cd71a7515b0e9a012e1ac9b1f8415bebcaf6fc97d4db32286642ac4c0fbe24f9"),
    ("zlib-1.3.2.tar.gz", "https://github.com/madler/zlib/releases/download/v1.3.2/",
     "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"),
)
# Exact upstream archives are also preserved in our published corresponding
# source bundle. Never execute its scripts; recover only a pinned source file.
SOURCE_BUNDLE_URL = "https://github.com/GalokPeng/light-block-desktop/releases/download/v0.8.1/Qingkuai-v0.8.1-ffmpeg-source.zip"
SOURCE_BUNDLE_SHA256 = "b16c13f3109f9be3442f307712fdc1e9375817fedf0101478a92ddd5a6b5c1a8"


def digest(path):
    with path.open("rb") as stream:
        return file_digest(stream, "sha256").hexdigest()


def fetch_file(url, target):
    subprocess.run(("curl.exe", "-fsSL", "--retry", "2", "--connect-timeout", "15",
                    "--max-time", "240", url, "-o", str(target)), check=True)


def source_bundle():
    path = DOWNLOADS / "published-v0.8.1-ffmpeg-source.zip"
    if path.is_file() and digest(path) == SOURCE_BUNDLE_SHA256:
        return path
    with TemporaryDirectory(dir=DOWNLOADS) as folder:
        temporary = Path(folder) / path.name
        fetch_file(SOURCE_BUNDLE_URL, temporary)
        if digest(temporary) != SOURCE_BUNDLE_SHA256:
            raise ValueError("SHA-256 mismatch for the published source bundle")
        temporary.replace(path)
    return path


def recover_source(asset, target):
    name, _base, expected = asset
    with ZipFile(source_bundle()) as archive:
        member = archive.getinfo("build/recording-runtime/downloads/" + name)
        if member.file_size > 64 * 1024 * 1024:
            raise ValueError("Unexpected source archive size")
        with archive.open(member) as source, target.open("wb") as output:
            shutil.copyfileobj(source, output, 1024 * 1024)
    if digest(target) != expected:
        raise ValueError(f"SHA-256 mismatch for recovered {name}")


def download(asset):
    name, base, expected = asset
    path = DOWNLOADS / name
    if path.is_file() and digest(path) == expected:
        return path
    with TemporaryDirectory(dir=DOWNLOADS) as folder:
        temporary = Path(folder) / name
        try:
            fetch_file(base + name, temporary)
            if digest(temporary) != expected:
                raise ValueError(f"SHA-256 mismatch for {name}")
        except (subprocess.CalledProcessError, ValueError):
            if asset not in SOURCES:
                raise
            print(f"Upstream unavailable: recovering pinned {name} from the published source bundle", file=sys.stderr)
            recover_source(asset, temporary)
        temporary.replace(path)
    return path


def git_bash():
    git = shutil.which("git")
    if git:
        for directory in Path(git).parents:
            bash = directory / "usr" / "bin" / "bash.exe"
            if bash.is_file():
                return bash
    raise FileNotFoundError("Building the recording runtime requires Git for Windows with Git Bash.")


def build():
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    fingerprint = sha256(SCRIPT.read_bytes() + RECIPE.read_bytes()).hexdigest()[:16]
    output = CACHE / fingerprint
    executable = output / "ffmpeg.exe"
    source_zip = output / "ffmpeg-source.zip"
    if executable.is_file() and source_zip.is_file():
        return executable

    sources = [download(asset) for asset in SOURCES]
    devkit, nasm = download(DEVKIT), download(NASM)
    seven_zip = Path(os.environ["ProgramFiles"]) / "7-Zip" / "7z.exe"
    if not seven_zip.is_file():
        raise FileNotFoundError("Building the recording runtime requires 7-Zip.")
    bash = git_bash()
    with TemporaryDirectory(prefix="ffmpeg-build-", dir=CACHE) as folder:
        work = Path(folder)
        with ZipFile(nasm) as archive:
            assembler = Path(archive.extract(f"{nasm.name.split('-win64')[0]}/nasm.exe", work))
        subprocess.run((str(seven_zip), "x", "-y", "-bd", "-bso0", f"-o{work}", str(devkit)), check=True)
        tools = work / "w64devkit" / "bin"
        for source in sources:
            with tarfile.open(source) as archive:
                archive.extractall(work, filter="data")
        library_rules, = work.glob("ffmpeg-*/ffbuild/library.mak")
        rules = library_rules.read_text(encoding="utf-8")
        echo_objects = "\t$(Q)echo $^ > $@.objs"
        if rules.count(echo_objects) != 1:
            raise ValueError("Unexpected FFmpeg response-file recipe.")
        # Let make write the response file without passing the object list through Windows' command line.
        library_rules.write_text(rules.replace(echo_objects, "\t$(file >$@.objs,$^)"), encoding="utf-8", newline="\n")
        windows = Path(os.environ["SystemRoot"])
        env = dict(os.environ, PATH=os.pathsep.join(map(str, (bash.parent, tools, assembler.parent,
                                                              windows / "System32", windows))))
        jobs = str(min(os.cpu_count() or 2, 12))
        try:
            subprocess.run((str(bash), RECIPE.as_posix(), jobs), cwd=work, env=env,
                           stdout=sys.stderr, check=True)
        except subprocess.CalledProcessError:
            for log in work.glob("ffmpeg-*/ffbuild/config.log"):
                shutil.copyfile(log, CACHE / "configure.log")
            raise

        with ZipFile(work / source_zip.name, "w") as archive:
            for source in sources:
                archive.write(source, "build/recording-runtime/downloads/" + source.name)
            for script in (SCRIPT, RECIPE):
                archive.write(script, "tools/" + script.name)
            archive.writestr("README.txt", "FFmpeg CLI corresponding source for Light Block Desktop.\n"
                             "Extract this archive on Windows x64 with Python 3.11, Git and 7-Zip, then run:\n"
                             "    py -3.11 tools/build_recording_runtime.py\n"
                             "The build downloads SHA-256-pinned compiler and assembler packages.\n"
                             "License notices are included in the FFmpeg, x264 and zlib archives.\n")
        output.mkdir(exist_ok=True)
        (work / executable.name).replace(executable)
        (work / source_zip.name).replace(source_zip)
    return executable


if __name__ == "__main__":
    print(build())
