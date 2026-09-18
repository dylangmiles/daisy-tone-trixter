import sys; sys.path.insert(0,'.')
from kisch import Sch, Sym
s = Sch("rottest")
# R1 vertical (rot 0): pin1 should be at y-3.81 (top), pin2 at y+3.81
r1 = s.add(Sym("Device","R","R1","1k",(50.8,50.8),0))
# R2 rotated 90: pins should lie horizontally
r2 = s.add(Sym("Device","R","R2","2k",(76.2,50.8),90))
print("R1 pins", r1.pin(1), r1.pin(2)); print("R2 pins", r2.pin(1), r2.pin(2))
s.wire(r1.pin(2), r2.pin(1), via='v')       # net between them
s.glabel("NET_A", r1.pin(1), 90)
s.glabel("NET_B", r2.pin(2), 0)
s.write("/tmp/rottest.kicad_sch") if False else s.write("rottest.kicad_sch")
