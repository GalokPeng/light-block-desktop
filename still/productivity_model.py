"""Small local database and reversible batch file operations."""
from __future__ import annotations

import ast
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import string
import uuid

from .i18n import tr


def utc_now():
    return datetime.now(timezone.utc).isoformat()


class ToolsDB:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(self.directory / "workbench.sqlite3", timeout=5)
        self.connection.row_factory = sqlite3.Row
        self.connection.executescript("""
            PRAGMA journal_mode=WAL; PRAGMA secure_delete=ON; PRAGMA wal_autocheckpoint=64;
            CREATE TABLE IF NOT EXISTS clipboard(id INTEGER PRIMARY KEY, kind TEXT NOT NULL, text TEXT, payload BLOB,
                digest TEXT UNIQUE, created TEXT, pinned INTEGER DEFAULT 0);
            CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY, title TEXT, body TEXT, color TEXT DEFAULT 'mint',
                done INTEGER DEFAULT 0, due TEXT, notified INTEGER DEFAULT 0, pinned INTEGER DEFAULT 0, updated TEXT);
            CREATE TABLE IF NOT EXISTS profiles(id TEXT PRIMARY KEY, name TEXT, data TEXT, updated TEXT);
            CREATE TABLE IF NOT EXISTS focus_sessions(id TEXT PRIMARY KEY, started TEXT, duration INTEGER, deadline TEXT, state TEXT);
        """)
        self.connection.commit()
        for table, additions in {
            "notes": {"archived": "INTEGER DEFAULT 0", "pin_x": "INTEGER", "pin_y": "INTEGER",
                      "pin_w": "INTEGER DEFAULT 290", "pin_h": "INTEGER DEFAULT 240"},
            "focus_sessions": {"rest": "INTEGER DEFAULT 5", "pause_wallpaper": "INTEGER DEFAULT 1"},
        }.items():
            columns = {row["name"] for row in self.connection.execute("PRAGMA table_info("+table+")")}
            for name, definition in additions.items():
                if name not in columns:
                    self.connection.execute("ALTER TABLE "+table+" ADD COLUMN "+name+" "+definition)
        self.connection.commit()

    def execute(self, sql, params=()):
        cursor = self.connection.execute(sql, params)
        self.connection.commit()
        return cursor

    def query(self, sql, params=()):
        return self.connection.execute(sql, params).fetchall()

    def add_clip(self, kind, text, payload=b""):
        data = payload if kind == "image" else text.encode("utf-8")
        if len(data) > 5*1024*1024:
            return False
        digest = hashlib.sha256(kind.encode()+data).hexdigest()
        self.execute("INSERT INTO clipboard(kind,text,payload,digest,created) VALUES(?,?,?,?,?) ON CONFLICT(digest) DO UPDATE SET created=excluded.created",
                     (kind, text, payload, digest, utc_now()))
        self.execute("DELETE FROM clipboard WHERE pinned=0 AND id NOT IN (SELECT id FROM clipboard ORDER BY pinned DESC,created DESC LIMIT 80)")
        while self.query("SELECT COALESCE(SUM(LENGTH(payload)+LENGTH(CAST(text AS BLOB))),0) size FROM clipboard")[0]["size"] > 32*1024*1024:
            row = self.query("SELECT id FROM clipboard WHERE pinned=0 ORDER BY created LIMIT 1")
            if not row:
                break
            self.execute("DELETE FROM clipboard WHERE id=?", (row[0]["id"],))
        return True

    def pin_clip(self, ident):
        row = self.query("SELECT pinned FROM clipboard WHERE id=?", (ident,))
        if not row:
            return
        if not row[0]["pinned"] and self.query("SELECT COUNT(*) n FROM clipboard WHERE pinned=1")[0]["n"] >= 20:
            raise ValueError(tr("最多固定 20 项，以保持历史记录容量可控。"))
        self.execute("UPDATE clipboard SET pinned=1-pinned WHERE id=?", (ident,))


def calculate(expression):
    """Arithmetic only; no eval, calls, names, attributes or unbounded powers."""
    if len(expression) > 120:
        raise ValueError(tr("表达式过长。"))
    tree = ast.parse(expression, mode="eval")
    if len(list(ast.walk(tree))) > 50:
        raise ValueError(tr("表达式过于复杂。"))
    def visit(node):
        if isinstance(node, ast.Expression):
            return visit(node.body)
        if isinstance(node, ast.Constant) and type(node.value) in (int, float):
            result = node.value
        elif isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            result = visit(node.operand) * (-1 if isinstance(node.op, ast.USub) else 1)
        elif isinstance(node, ast.BinOp):
            a, b = visit(node.left), visit(node.right)
            if isinstance(node.op, ast.Add): result = a+b
            elif isinstance(node.op, ast.Sub): result = a-b
            elif isinstance(node.op, ast.Mult): result = a*b
            elif isinstance(node.op, ast.Div): result = a/b
            elif isinstance(node.op, ast.FloorDiv): result = a//b
            elif isinstance(node.op, ast.Mod): result = a%b
            elif isinstance(node.op, ast.Pow) and abs(b) <= 16: result = a**b
            else: raise ValueError(tr("不支持该运算。"))
        else:
            raise ValueError(tr("仅支持数字和基本算术。"))
        if isinstance(result, complex) or abs(result) > 1e100:
            raise ValueError(tr("结果超出范围。"))
        return result
    return visit(tree)


def rename_preview(paths, pattern):
    parsed = list(string.Formatter().parse(pattern))
    for _, field, spec, conversion in parsed:
        if field is not None and (field not in {"name", "ext", "n"} or conversion or
                                  (spec and not (field == "n" and re.fullmatch(r"0?\d{1,2}d?", spec)))):
            raise ValueError(tr("命名模板仅支持 {name}、{ext} 和 {n:03}。"))
    sources = [Path(p).absolute() for p in paths]
    if len(set(os.path.normcase(str(p)) for p in sources)) != len(sources):
        raise ValueError(tr("输入项目重复。"))
    names, targets = [], set()
    source_keys = {os.path.normcase(str(path)) for path in sources}
    for index, source in enumerate(sources, 1):
        if not source.exists():
            raise FileNotFoundError(source)
        name = pattern.format(name=source.name if source.is_dir() else source.stem,
                              ext="" if source.is_dir() else source.suffix, n=index)
        if not name or name != name.rstrip(" .") or re.search(r'[<>:"/\\|?*\x00-\x1f]', name) or name in {".", ".."}:
            raise ValueError(tr("文件名包含无效字符或结尾空格/句点。"))
        if re.fullmatch(r"(?i)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", name):
            raise ValueError(tr("文件名是 Windows 保留名称。"))
        target = source.with_name(name)
        key = os.path.normcase(str(target))
        if key in targets or (target.exists() and key not in source_keys):
            raise FileExistsError(tr("目标重名：{0}").format(name))
        targets.add(key)
        names.append((source, target))
    return names


class RenameJournal:
    def __init__(self, directory):
        self.directory = Path(directory) / "rename_jobs"
        self.directory.mkdir(parents=True, exist_ok=True)

    @staticmethod
    def identity(path):
        info = os.stat(path, follow_symlinks=False)
        return [info.st_dev, info.st_ino]

    def records(self):
        records = []
        for file in self.directory.glob("*.json"):
            try:
                data = json.loads(file.read_text(encoding="utf-8"))
                if isinstance(data.get("records"), list):
                    records.append((file, data))
            except (OSError, ValueError, TypeError):
                continue
        return sorted(records, key=lambda pair: pair[1].get("created", ""), reverse=True)

    def pending(self):
        return [(file, data) for file,data in self.records() if data.get("state") in {"pending", "recovering", "recovery_needed"}]

    def latest(self):
        records = self.records()
        undone = {data.get("undo_of") for file,data in records if data.get("state") == "committed" and data.get("undo_of")}
        return next((file for file,data in records if data.get("state") == "committed" and not data.get("undo_of") and data.get("id") not in undone), None)

    def apply(self, pairs, undo_of=None):
        pairs = [(Path(a).absolute(), Path(b).absolute()) for a,b in pairs if str(a) != str(b)]
        source_keys = {os.path.normcase(str(a)) for a,b in pairs}
        target_keys = [os.path.normcase(str(b)) for a,b in pairs]
        if len(source_keys) != len(pairs) or len(set(target_keys)) != len(pairs):
            raise ValueError(tr("来源或目标存在重复名称。"))
        for source, target in pairs:
            if not source.exists() or source.is_symlink() or source.parent != target.parent:
                raise ValueError(tr("重命名只支持同一目录中的现有普通项目。"))
            if target.exists() and os.path.normcase(str(target)) not in source_keys:
                raise FileExistsError(target)
        ident = uuid.uuid4().hex
        file = self.directory / (ident+".json")
        records = [{"source": str(a), "target": str(b), "temp": str(a.with_name(".Still-rename-"+uuid.uuid4().hex)),
                    "identity": self.identity(a), "rescue": []} for a,b in pairs]
        if len({tuple(item["identity"]) for item in records}) != len(records):
            raise ValueError(tr("本批文件包含同一文件的多个硬链接，请分开重命名；文件保持原样。"))
        data = {"id":ident,"state":"pending","phase":"staging","created":utc_now(),"records":records}
        if undo_of:
            data["undo_of"] = undo_of
        self._save(file,data)
        try:
            for item in records:
                os.rename(item["source"],item["temp"])
            data["phase"]="finalizing"
            self._save(file,data)
            for item in records:
                os.rename(item["temp"],item["target"])
            data["state"]="committed"
            self._save(file,data)
            return file
        except OSError as exc:
            try:
                self._restore(file,data)
            except (OSError,ValueError) as recovery:
                data["state"]="recovery_needed"
                self._save(file,data)
                raise OSError(tr("重命名中断，文件已记录；请在文件工作台点击“恢复中断任务”：{0}").format(recovery)) from exc
            raise

    def _locate(self, item):
        candidates = {item["source"],item["target"],item["temp"],*item.get("rescue",[])}
        for candidate in candidates:
            path=Path(candidate)
            try:
                if path.exists() and self.identity(path)==item.get("identity"):
                    return path
            except OSError:
                pass
        raise FileNotFoundError(tr("无法定位原文件，请查看恢复记录：")+item["source"])

    def _restore(self, file, data):
        located=[(item,self._locate(item)) for item in data["records"]]
        moving={os.path.normcase(str(path)) for item,path in located}
        for item,path in located:
            original=Path(item["source"])
            if original.exists() and os.path.normcase(str(original)) not in moving:
                raise FileExistsError(tr("原名称被其他文件占用，已保留所有文件：")+str(original))
        staged=[]
        for item,path in located:
            rescue=path.with_name(".Still-restore-"+uuid.uuid4().hex)
            item.setdefault("rescue",[]).append(str(rescue))
            staged.append((item,path,rescue))
        data["state"]="recovering"
        self._save(file,data)  # Persist every possible temporary path BEFORE moving.
        try:
            for item,path,rescue in staged:
                os.rename(path,rescue)
            for item,path,rescue in staged:
                os.rename(rescue,item["source"])
        except OSError:
            data["state"]="recovery_needed"
            self._save(file,data)
            raise
        data["state"]="rolled_back"
        self._save(file,data)

    def recover(self, file):
        file=Path(file)
        if file.parent.resolve()!=self.directory.resolve():
            raise ValueError(tr("不属于当前恢复记录目录。"))
        data=json.loads(file.read_text(encoding="utf-8"))
        if data.get("state") not in {"pending","recovering","recovery_needed"}:
            raise ValueError(tr("该任务无需恢复。"))
        self._restore(file,data)

    @staticmethod
    def _save(file, data):
        temp = file.with_suffix(".tmp")
        with temp.open("w",encoding="utf-8") as output:
            json.dump(data,output,ensure_ascii=False,indent=2)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temp, file)

    def undo(self, file):
        file = Path(file)
        data = json.loads(file.read_text(encoding="utf-8"))
        if data["state"] != "committed":
            raise ValueError(tr("该记录无法撤销。"))
        if data.get("undo_of") or any(record.get("undo_of") == data["id"] and record.get("state") == "committed" for _,record in self.records()):
            raise ValueError(tr("该操作已撤销。"))
        pairs = [(Path(item["target"]), Path(item["source"])) for item in data["records"]]
        if any(self.identity(item["target"]) != item.get("identity") for item in data["records"]):
            raise ValueError(tr("目标文件已被替换，已取消撤销。"))
        target_keys = {os.path.normcase(str(a)) for a,b in pairs}
        if any(not a.exists() or (b.exists() and os.path.normcase(str(b)) not in target_keys) for a,b in pairs):
            raise ValueError(tr("文件已移动或原名称已被占用，已取消撤销，文件保留。"))
        undo_file = self.apply(pairs, undo_of=data["id"])
        data["state"] = "undone"
        self._save(file, data)
        return undo_file
