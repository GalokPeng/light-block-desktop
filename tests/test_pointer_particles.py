import unittest
from still.pointer_particles import ParticleField, LIFETIMES, LIMITS


class ParticleTests(unittest.TestCase):
    def test_all_modes_are_bounded_and_settle_after_movement_stops(self):
        for style in LIFETIMES:
            field=ParticleField(style)
            for i in range(300):
                field.step(i/30,(i*7,i%30))
                self.assertLessEqual(len(field.points),LIMITS[style])
            end=299/30
            for i in range(1,50):
                field.step(end+i/30,(299*7,299%30))
            self.assertFalse(field.points,style)
            self.assertFalse(field.step(end+2,(299*7,299%30)))
            self.assertTrue(field.step(end+2.03,(299*7+1,299%30)))

    def test_falling_particles_keep_moving_under_gravity_after_mouse_stops(self):
        field=ParticleField("fall")
        field.step(0.,(100,100))
        first=field.points[0]
        start=first.y
        for i in range(1,25): field.step(i/30,(100,100))
        self.assertEqual(len(field.points),1)
        self.assertGreater(first.y,start+20)
        self.assertGreater(first.vy,60)

    def test_teleports_suspend_and_disable_cannot_leave_stale_particles(self):
        for style in LIFETIMES:
            field=ParticleField(style)
            field.step(0.,(0,0));field.step(.03,(2000,-1000))
            self.assertTrue(all(abs(p.x-2000)<1 for p in field.points))
            self.assertFalse(field.step(30.,(2000,-1000)))
            field.clear()
            self.assertIsNone(field.last)
            self.assertFalse(field.points)
