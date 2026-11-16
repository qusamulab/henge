#!/usr/bin/env python3
"""List DEX classes/methods/fields absent from the bundled class library and this APK.

Static references only: reflection, downloaded DEX and Android service behavior cannot be inferred.
No guest classes are initialized or executed. Accepts APKs and individual .dex files.
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import zipfile

# VM ignores these DEX metadata annotations; they are not executable class-library dependencies.
METADATA = {"Ldalvik/annotation/" + name + ";" for name in (
    "AnnotationDefault", "EnclosingClass", "EnclosingMethod", "InnerClass", "MemberClasses",
    "MethodParameters", "Signature", "SourceDebugExtension", "Throws")}


class Dex:
    def __init__(self, data):
        self.data = data
        if len(data) < 112 or data[:4] != b"dex\n" or self.u32(32) != len(data):
            raise ValueError("not a complete DEX file")
        if self.u32(36) != 112 or self.u32(40) != 0x12345678:
            raise ValueError("unsupported DEX header")
        self.strings = []
        for item in self.table(56, 4):
            pos = self.u32(item)
            _, pos = self.uleb(pos)
            end = data.find(b"\0", pos)
            if end < 0:
                raise ValueError("unterminated DEX string")
            self.strings.append(data[pos:end].decode("utf-8", errors="surrogateescape"))
        self.types = [self.strings[self.u32(p)] for p in self.table(64, 4)]
        self.protos = []
        for p in self.table(72, 12):
            args = self.type_list(self.u32(p + 8))
            self.protos.append("(" + "".join(args) + ")" + self.types[self.u32(p + 4)])
        self.fields = [(self.types[self.u16(p)], self.strings[self.u32(p + 4)], self.types[self.u16(p + 2)])
                       for p in self.table(80, 8)]
        self.methods = [(self.types[self.u16(p)], self.strings[self.u32(p + 4)], self.protos[self.u16(p + 2)])
                        for p in self.table(88, 8)]
        self.classes = {}
        for p in self.table(96, 32):
            name, super_index = self.types[self.u32(p)], self.u32(p + 8)
            parents = ([] if super_index == 0xffffffff else [self.types[super_index]]) + self.type_list(self.u32(p + 12))
            methods, fields = set(), set()
            cursor = self.u32(p + 24)
            if cursor:
                counts = []
                for _ in range(4):
                    count, cursor = self.uleb(cursor)
                    if count > len(data): raise ValueError("invalid class member count")
                    counts.append(count)
                for group, count in enumerate(counts):
                    index = 0
                    for _ in range(count):
                        delta, cursor = self.uleb(cursor)
                        index += delta
                        _, cursor = self.uleb(cursor)  # access flags
                        if group >= 2:
                            _, cursor = self.uleb(cursor)  # code offset
                            methods.add(self.methods[index][1:])
                        else:
                            fields.add(self.fields[index][1:])
            self.classes[name] = dict(parents=parents, methods=methods, fields=fields)

    def u32(self, pos):
        return struct.unpack_from("<I", self.data, pos)[0]

    def u16(self, pos):
        return struct.unpack_from("<H", self.data, pos)[0]

    def table(self, pos, width):
        count, offset = self.u32(pos), self.u32(pos + 4)
        if offset > len(self.data) or count > (len(self.data) - offset) // width:
            raise ValueError("DEX table outside file")
        return range(offset, offset + count * width, width)

    def uleb(self, pos):
        value = 0
        for shift in range(0, 35, 7):
            byte = self.data[pos]
            pos += 1
            value |= (byte & 127) << shift
            if not byte & 128: return value, pos
        raise ValueError("invalid ULEB128")

    def type_list(self, pos):
        if not pos: return []
        count = self.u32(pos)
        if count > (len(self.data) - pos - 4) // 2: raise ValueError("invalid type list")
        return [self.types[self.u16(pos + 4 + i * 2)] for i in range(count)]


def check(inputs, boot):
    definitions = {}
    for dex in boot + inputs:
        for name, definition in dex.classes.items(): definitions.setdefault(name, definition)

    def has_class(name):
        return name.lstrip("[") in definitions or name.lstrip("[") in set("VZBCSIJFD")

    def member(owner, name, signature, kind):
        todo, seen = ["Ljava/lang/Object;" if owner.startswith("[") else owner], set()
        while todo:
            cls = todo.pop(0)
            if cls in seen: continue
            seen.add(cls)
            definition = definitions.get(cls)
            if not definition: continue
            if (name, signature) in definition[kind]: return True
            if name not in ("<init>", "<clinit>"): todo.extend(definition["parents"])
        return False

    missing = set()
    for dex in inputs:
        for name in dex.types:
            if name not in METADATA and not has_class(name): missing.add(("class", name))
        for kind in ("methods", "fields"):
            for owner, name, sig in getattr(dex, kind):
                if has_class(owner) and not member(owner, name, sig, kind):
                    missing.add((kind[:-1], f"{owner}->{name}{sig}"))
    return [dict(kind=kind, name=name) for kind, name in sorted(missing)]


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", type=Path, nargs="+")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        boot = [Dex((root / p).read_bytes()) for p in ("vm/core/henge-core.dex", "vm/android/henge-android.dex")]
        inputs = []
        for path in args.inputs:
            if zipfile.is_zipfile(path):
                with zipfile.ZipFile(path) as archive:
                    for name in sorted(archive.namelist()):
                        if "/" not in name and name.startswith("classes") and name.endswith(".dex"):
                            if archive.getinfo(name).file_size > 128 * 1024 * 1024:
                                raise ValueError("DEX exceeds the 128 MiB analysis limit")
                            inputs.append(Dex(archive.read(name)))
            else:
                inputs.append(Dex(path.read_bytes()))
        if not inputs: raise ValueError("no DEX files found")
        missing = check(inputs, boot)
    except (OSError, ValueError, IndexError, struct.error, zipfile.BadZipFile) as error:
        parser.exit(2, f"check-dex: {error}\n")
    if args.json: print(json.dumps(missing, indent=2))
    else:
        for item in missing: print(f"missing {item['kind']}: {item['name']}")
        print(f"{len(missing)} missing references (static analysis)")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
