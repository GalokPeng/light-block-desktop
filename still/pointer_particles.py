"""Bounded, display-independent particle physics, with no idle work."""
from dataclasses import dataclass
import math

STYLES = ("dots", "fall", "glow")
LIFETIMES = {"dots": .42, "fall": 1.25, "glow": 1.05}
LIMITS = {"dots": 20, "fall": 64, "glow": 1}


@dataclass
class Particle:
    x: float
    y: float
    born: float
    vx: float = 0.
    vy: float = 0.
    phase: float = 0.


class ParticleField:
    def __init__(self, style="dots"):
        if style not in STYLES:
            raise ValueError("unknown particle style")
        self.style = style
        self.points = []
        self.last = self.time = None
        self.serial = 0

    def clear(self):
        self.points.clear()
        self.last = self.time = None

    def step(self, now, point):
        dt = max(0., now-self.time) if self.time is not None else 0.
        if dt > .3:  # Wake/suspend or blocked event loop: discard stale motion.
            self.points.clear()
        dt = min(dt, .05)
        old = self.last
        moved = old is None or old != point
        if old is not None and math.dist(point,old) > 240:
            self.points.clear()
            old = None
        life = LIFETIMES[self.style]
        self.points[:] = [p for p in self.points if 0 <= now-p.born < life]
        if self.style == "fall":
            for p in self.points:
                # Horizontal air resistance; gravity remains continuous after release.
                drag = math.exp(-2.6*dt)
                p.x += p.vx*(1-drag)/2.6
                p.vx *= drag
                p.y += p.vy*dt + .5*110*dt*dt
                p.vy += 110*dt
        if moved:
            x,y = point
            if self.style == "glow":
                self.points[:] = [Particle(x,y,now)]
            elif self.style == "dots":
                self.points.append(Particle(x,y,now))
            else:
                dx,dy = (x-old[0],y-old[1]) if old else (0,0)
                count = min(2,max(1,round(math.hypot(dx,dy)/14)))
                for n in range(count):
                    self.serial += 1
                    phase = self.serial*2.39996323
                    fraction = (n+1)/count
                    self.points.append(Particle(x-dx*(1-fraction),y-dy*(1-fraction),now,
                        max(-42,min(42,dx/max(dt,.016)*.07))+math.sin(phase)*10,
                        max(-25,min(15,dy/max(dt,.016)*.04))-10,phase))
            self.last = point
        # Bound the native translucent window even during fast movement.
        if self.last:
            x,y = self.last
            extent = 190 if self.style == "dots" else 270
            self.points[:] = [p for p in self.points if abs(p.x-x)<extent and abs(p.y-y)<extent]
        del self.points[:-LIMITS[self.style]]
        self.time = now
        return bool(self.points)
