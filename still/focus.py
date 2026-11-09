from __future__ import annotations

from datetime import datetime, timedelta, timezone
import json
import uuid

from PySide6.QtCore import QObject, Qt, QTimer, Signal
from PySide6.QtWidgets import QCheckBox, QComboBox, QHBoxLayout, QInputDialog, QSpinBox, QVBoxLayout, QWidget

from .i18n import tr
from .productivity_model import utc_now


class FocusEngine(QObject):
    changed = Signal()

    def __init__(self, workbench):
        super().__init__()
        self.workbench, self.db = workbench, workbench.db
        self.timer = QTimer(self)
        self.timer.setSingleShot(True)
        self.timer.timeout.connect(self.complete)
        self.ident = None
        self.deadline = None
        self.stage = "idle"
        self.saved_pause = None
        self.break_minutes = 5
        self.pause_on_focus = True
        workbench.controller.engine.changed.connect(self.enforce_pause)
        rows = self.db.query("SELECT * FROM focus_sessions WHERE state IN ('running','resting') ORDER BY started DESC LIMIT 1")
        if rows:
            row = rows[0]
            self.ident, self.deadline = row["id"], datetime.fromisoformat(row["deadline"])
            self.break_minutes, self.pause_on_focus = row["rest"], bool(row["pause_wallpaper"])
            self.stage = "rest" if row["state"] == "resting" else "focus"
            self.timer.start(max(100, min(2147483647, int(self.remaining()*1000))))
        self.set_background(self.active)
        self.enforce_pause()

    @property
    def active(self):
        return self.stage != "idle"

    def remaining(self):
        return max(0, (self.deadline-datetime.now(timezone.utc)).total_seconds()) if self.deadline else 0

    def set_background(self, value):
        self.workbench.controller.store.settings.focus_active = bool(value)
        self.workbench.controller.store.save()

    def start(self, minutes, rest=5, pause_wallpaper=True):
        if self.active:
            self.stop()
        self.ident = uuid.uuid4().hex
        self.deadline = datetime.now(timezone.utc)+timedelta(minutes=minutes)
        self.break_minutes, self.stage = rest, "focus"
        self.pause_on_focus = pause_wallpaper
        self.db.execute("INSERT INTO focus_sessions(id,started,duration,deadline,state,rest,pause_wallpaper) VALUES(?,?,?,?,?,?,?)",
                        (self.ident, utc_now(), int(minutes*60), self.deadline.isoformat(), "running", rest, int(pause_wallpaper)))
        engine = self.workbench.controller.engine
        if pause_wallpaper and engine.surface:
            self.saved_pause = (engine.active["id"], engine.manual_pause)
            engine.manual_pause = True
            engine.evaluate()
        self.timer.start(int(minutes*60*1000))
        self.set_background(True)
        self.changed.emit()

    def enforce_pause(self):
        engine = self.workbench.controller.engine
        if self.stage == "focus" and self.pause_on_focus and engine.surface is not None and engine.active and (self.saved_pause is None or self.saved_pause[0] != engine.active["id"]):
            self.saved_pause = (engine.active["id"], engine.manual_pause)
            engine.manual_pause = True
            engine.evaluate()

    def restore_wallpaper(self):
        engine = self.workbench.controller.engine
        if self.saved_pause and engine.active and self.saved_pause[0] == engine.active["id"]:
            engine.manual_pause = self.saved_pause[1]
            engine.evaluate()
        self.saved_pause = None

    def stop(self):
        if not self.active:
            return
        self.timer.stop()
        if self.ident:
            self.db.execute("UPDATE focus_sessions SET state=? WHERE id=?", ("completed" if self.stage == "rest" else "cancelled", self.ident))
        self.restore_wallpaper()
        self.stage, self.deadline = "idle", None
        self.set_background(False)
        self.changed.emit()

    def complete(self):
        if not self.active:
            return
        self.restore_wallpaper()
        if self.stage == "focus" and self.break_minutes:
            self.stage = "rest"
            self.deadline = datetime.now(timezone.utc)+timedelta(minutes=self.break_minutes)
            self.db.execute("UPDATE focus_sessions SET state='resting',deadline=? WHERE id=?", (self.deadline.isoformat(), self.ident))
            self.timer.start(self.break_minutes*60*1000)
            self.workbench.controller.tray.showMessage(tr("轻块桌面 · 专注完成"), tr("做得很好，休息 {0} 分钟。").format(self.break_minutes))
        else:
            self.db.execute("UPDATE focus_sessions SET state='completed' WHERE id=?", (self.ident,))
            self.stage, self.deadline = "idle", None
            self.set_background(False)
            self.workbench.controller.tray.showMessage(tr("轻块桌面 · 计时结束"), tr("休息结束，可以开始下一轮。"))
        self.changed.emit()

    def save_profile(self, name):
        controller = self.workbench.controller
        manager = controller.get_organizer()
        data = {"wallpaper": controller.store.settings.active_id, "fit": controller.store.settings.fit,
                "screen": controller.store.settings.screen, "organizer_enabled": manager.enabled,
                "group_order": [group["id"] for group in manager.collections.groups]}
        self.db.execute("INSERT INTO profiles(id,name,data,updated) VALUES(?,?,?,?)",
                        (uuid.uuid4().hex, name[:60], json.dumps(data, ensure_ascii=False), utc_now()))
        self.changed.emit()

    def apply_profile(self, ident):
        rows = self.db.query("SELECT data FROM profiles WHERE id=?", (ident,))
        if not rows:
            return
        data = json.loads(rows[0]["data"])
        controller = self.workbench.controller
        item = controller.store.find(data.get("wallpaper", ""))
        old_fit, old_screen = controller.store.settings.fit, controller.store.settings.screen
        controller.store.settings.fit = data.get("fit", "fill")
        controller.store.settings.screen = data.get("screen", "")
        try:
            if item:
                controller.engine.apply(item)
            elif not data.get("wallpaper"):
                controller.engine.stop()
        except (OSError, ValueError, RuntimeError):
            controller.store.settings.fit, controller.store.settings.screen = old_fit, old_screen
            raise
        manager = controller.get_organizer()
        order = data.get("group_order", [saved["id"] for saved in data.get("layout", [])])
        ranks = {ident: index for index, ident in enumerate(order)}
        manager.collections.groups.sort(key=lambda group: ranks.get(group["id"], len(ranks)))
        manager.set_enabled(bool(data.get("organizer_enabled")))
        controller.store.save()
        self.changed.emit()

    def cleanup(self):
        self.timer.stop()
        self.stage = "idle"
        try:
            self.workbench.controller.engine.changed.disconnect(self.enforce_pause)
        except (TypeError, RuntimeError):
            pass
        self.restore_wallpaper()


class FocusWindow(QWidget):
    def __init__(self, workbench):
        super().__init__()
        from .ui import STYLE, button, label
        self.workbench, self.engine = workbench, workbench.focus()
        self.setWindowTitle(tr("轻块桌面 · 专注工作区"))
        self.setObjectName("root")
        self.setWindowIcon(workbench.controller.icon)
        self.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        self.setStyleSheet(STYLE)
        self.resize(760, 590)
        outer = QVBoxLayout(self)
        outer.setContentsMargins(30, 27, 30, 27)
        outer.setSpacing(19)
        outer.addWidget(label(tr("把此刻，留给重要的事。"), "title"))
        outer.addWidget(label(tr("工作区快照 · 专注与休息 · 动态壁纸暂停 · 本地专注记录"), "muted", True))
        self.clock = label("25:00")
        self.clock.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.clock.setStyleSheet("font:600 68px 'Segoe UI';color:#b7e9d4;padding:20px;")
        outer.addWidget(self.clock, 1)
        row = QHBoxLayout()
        row.addWidget(label(tr("专注")))
        self.minutes = QSpinBox()
        self.minutes.setRange(1, 180)
        self.minutes.setValue(25)
        self.minutes.setSuffix(tr(" 分钟"))
        row.addWidget(self.minutes)
        row.addWidget(label(tr("休息")))
        self.rest = QSpinBox()
        self.rest.setRange(0, 60)
        self.rest.setValue(5)
        self.rest.setSuffix(tr(" 分钟"))
        row.addWidget(self.rest)
        self.pause = QCheckBox(tr("专注时暂停动态壁纸"))
        self.pause.setChecked(True)
        row.addWidget(self.pause)
        outer.addLayout(row)
        actions = QHBoxLayout()
        actions.addWidget(button(tr("开始专注"), lambda: self.engine.start(self.minutes.value(), self.rest.value(), self.pause.isChecked()), "primary"))
        actions.addWidget(button(tr("结束本轮"), self.engine.stop))
        outer.addLayout(actions)
        profiles = QHBoxLayout()
        self.profiles = QComboBox()
        profiles.addWidget(self.profiles, 1)
        profiles.addWidget(button(tr("应用工作区"), self.apply))
        profiles.addWidget(button(tr("保存当前工作区…"), self.save_profile))
        outer.addLayout(profiles)
        self.stats = label("", "muted", True)
        outer.addWidget(self.stats)
        outer.addWidget(label(tr("工作区保存壁纸和已有分组的位置；不会替换分组内容或移动文件。"), "muted", True))
        self.tick = QTimer(self)
        self.tick.setInterval(1000)
        self.tick.timeout.connect(self.update_clock)
        self.engine.changed.connect(self.refresh)
        self.refresh()

    def refresh(self):
        ident = self.profiles.currentData()
        self.profiles.clear()
        for row in self.engine.db.query("SELECT id,name FROM profiles ORDER BY updated DESC"):
            self.profiles.addItem(row["name"], row["id"])
        if ident:
            self.profiles.setCurrentIndex(max(0, self.profiles.findData(ident)))
        stats = self.engine.db.query("SELECT COUNT(*) n,COALESCE(SUM(duration),0) seconds FROM focus_sessions WHERE state IN ('completed','resting')")[0]
        self.stats.setText(tr("累计完成 {0} 轮专注 · {1} 分钟").format(stats['n'], stats['seconds'] // 60))
        self.update_clock()

    def update_clock(self):
        seconds = int(self.engine.remaining()) if self.engine.active else self.minutes.value()*60
        self.clock.setText((tr("休息  ") if self.engine.stage == "rest" else "")+f"{seconds//60:02d}:{seconds%60:02d}")

    def showEvent(self, event):
        self.tick.start()
        super().showEvent(event)

    def hideEvent(self, event):
        self.tick.stop()
        super().hideEvent(event)

    def save_profile(self):
        name, accepted = QInputDialog.getText(self, tr("保存工作区"), tr("名称，例如“深度工作”"))
        if accepted and name.strip():
            self.engine.save_profile(name.strip())

    def apply(self):
        if self.profiles.currentData():
            try:
                self.engine.apply_profile(self.profiles.currentData())
            except (OSError, ValueError, RuntimeError) as exc:
                self.workbench.error(str(exc))
