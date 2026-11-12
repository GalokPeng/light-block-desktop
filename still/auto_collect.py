"""One deadline timer for new desktop references; no periodic desktop scan."""
import logging
from time import monotonic

from PySide6.QtCore import QObject, QTimer
from PySide6.QtWidgets import QApplication


class AutoCollector(QObject):
    DELAY = 60.

    def __init__(self, manager, clock=monotonic):
        super().__init__(manager)
        self.manager, self.clock = manager, clock
        self.deadlines = {}
        self.timer = QTimer(self, singleShot=True)
        self.timer.timeout.connect(self.collect)

    def sync(self):
        manager = self.manager
        if not manager.enabled or not manager.store.settings.organizer_auto_collect:
            self.stop()
            return
        entries = {entry["id"] for entry in manager.collections.loose}
        self.deadlines = {key: value for key, value in self.deadlines.items() if key in entries}
        now = self.clock()
        for ident in entries:
            self.deadlines.setdefault(ident, now + self.DELAY)
        self.schedule()

    def schedule(self):
        self.timer.stop()
        if self.deadlines:
            self.timer.start(max(1, min(2147483647, int((min(self.deadlines.values()) - self.clock()) * 1000) + 1)))

    def collect(self):
        manager, now = self.manager, self.clock()
        if not manager.enabled or not manager.store.settings.organizer_auto_collect:
            self.stop()
            return
        due = []
        for entry in manager.collections.loose:
            if self.deadlines.get(entry["id"], now + self.DELAY) > now:
                continue
            icon = manager.loose_icons.get(entry["id"])
            if QApplication.activePopupWidget() or (icon and (icon.press is not None or icon.underMouse())):
                self.deadlines[entry["id"]] = now + 5
            elif len(due) < 64:
                due.append(entry)
        if due:
            try:
                manager.collections.put_away_many(due)
            except (OSError, ValueError) as exc:
                logging.warning("Automatic desktop collection deferred: %s", exc)
                for entry in due:
                    self.deadlines[entry["id"]] = now + self.DELAY
            else:
                manager.refresh()
                return
        self.sync()

    def stop(self):
        self.timer.stop()
        self.deadlines.clear()
