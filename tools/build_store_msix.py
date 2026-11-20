"""Package a clean, tested Windows release payload for manual Partner Center upload."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
from urllib.parse import unquote
from xml.etree import ElementTree as ET
import zipfile

from release import ROOT, digest, prefix, read_version, verify_identity, verify_ui, windows_version


MANIFEST_DIR = ROOT / "installer/store"
FOUNDATION = "http://schemas.microsoft.com/appx/manifest/foundation/windows10"
UAP = "http://schemas.microsoft.com/appx/manifest/uap/windows10"
RESCAP = "http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"


def sdk_tool():
    kits = Path(os.environ["ProgramFiles(x86)"]) / "Windows Kits/10/bin"
    tools = list(kits.glob("10.*.*.*/x64/makeappx.exe"))
    if not tools:
        raise RuntimeError("Install the Windows 10/11 SDK or pass --makeappx.")
    return max(tools, key=lambda path: tuple(int(n) for n in path.parents[1].name.split(".")))


def manifest(version):
    fields = {}
    for key in ("STORE_IDENTITY_NAME", "STORE_IDENTITY_PUBLISHER", "STORE_PUBLISHER_DISPLAY_NAME", "STORE_DISPLAY_NAME"):
        fields[key] = os.environ.get(key, "").strip()
        if not fields[key]:
            raise ValueError(f"Missing {key}")
    for ns, uri in (("", FOUNDATION), ("uap", UAP), ("rescap", RESCAP)):
        ET.register_namespace(ns, uri)
    tree = ET.parse(MANIFEST_DIR / "AppxManifest.xml")
    root = tree.getroot()
    identity = root.find(f"{{{FOUNDATION}}}Identity")
    identity.set("Name", fields["STORE_IDENTITY_NAME"])
    identity.set("Publisher", fields["STORE_IDENTITY_PUBLISHER"])
    identity.set("Version", windows_version(version))
    props = root.find(f"{{{FOUNDATION}}}Properties")
    props.find(f"{{{FOUNDATION}}}DisplayName").text = fields["STORE_DISPLAY_NAME"]
    props.find(f"{{{FOUNDATION}}}PublisherDisplayName").text = fields["STORE_PUBLISHER_DISPLAY_NAME"]
    root.find(f".//{{{UAP}}}VisualElements").set("DisplayName", fields["STORE_DISPLAY_NAME"])
    return tree


def build(tool, output):
    version = read_version()
    app = ROOT / "dist" / (prefix(version) + "-Windows-x64")
    verify_identity(app, version)
    verify_ui(app)
    output.mkdir(parents=True, exist_ok=True)
    final = output / (prefix(version) + "-x64.msix")
    with tempfile.TemporaryDirectory(prefix="msix-", dir=output) as tmp:
        work = Path(tmp)
        spec = work / "AppxManifest.xml"
        manifest(version).write(spec, encoding="utf-8", xml_declaration=True)
        files = {"AppxManifest.xml": spec}
        files.update({"StoreAssets/" + p.name: p for p in (MANIFEST_DIR / "assets").glob("*.png")})
        for p in app.rglob("*"):
            relative = p.relative_to(app)
            if relative.parts[0].casefold() in {"data", "data-preview", ".local", ".vercel"} or p.is_symlink():
                raise ValueError("Unexpected user data or link in the Store payload")
            if p.is_file() and relative.as_posix() != "runtime/QingkuaiUpdate.exe":
                files["App/" + relative.as_posix()] = p
        mapping = work / "mapping.txt"
        mapping.write_text("[Files]\n" + "".join(f'"{path}" "{Path(name)}"\n' for name, path in sorted(files.items())), encoding="utf-8-sig")
        packed = work / final.name
        result = subprocess.run([str(tool), "pack", "/f", str(mapping), "/p", str(packed), "/h", "SHA256"],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            detail = result.stdout.decode(errors="replace").splitlines()[-12:]
            raise RuntimeError("MakeAppx failed:\n" + "\n".join(detail))
        with zipfile.ZipFile(packed) as archive:
            if archive.testzip():
                raise ValueError("Damaged MSIX")
            # OPC escapes non-ASCII package part names in the underlying ZIP.
            parts = {unquote(name): name for name in archive.namelist()}
            if len(parts) != len(archive.namelist()):
                raise ValueError("Ambiguous package part names")
            actual = set(parts)
            payload = set(files)
            unexpected = actual - payload - {"AppxBlockMap.xml", "[Content_Types].xml", "AppxMetadata/CodeIntegrity.cat"}
            if not {"AppxBlockMap.xml", "[Content_Types].xml"} <= actual:
                raise ValueError("MSIX packaging metadata is incomplete")
            if unexpected or payload - actual:
                raise ValueError(f"Unexpected parts: {sorted(unexpected)[:3]}; missing: {sorted(payload - actual)[:3]}")
            for name, path in files.items():
                with archive.open(parts[name]) as stream:
                    if hashlib.file_digest(stream, "sha256").hexdigest() != digest(path):
                        raise ValueError(f"Package bytes changed for {name}")
        os.replace(packed, final)
    print(f"Microsoft Store upload: {final.name} ({digest(final)})")
    return final


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--makeappx", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "build/store-package")
    args = parser.parse_args()
    build(args.makeappx.resolve() if args.makeappx else sdk_tool(), args.output.resolve())
