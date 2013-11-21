#!/usr/bin/python

from simplekml import Kml
import sys

in_file, out_file = sys.argv[1], sys.argv[2];
data = list()

kml = Kml(name = in_file, open = 1)

with open(in_file) as file:
	lines = file.readlines()
	for x in lines:
		# Ignore lines that start with "//".
		if x.startswith("//"):
			continue

		elements = x.split(",")[:3]
		group = tuple(elements)
		data.append(group)

path = kml.newlinestring(name = "Flight", description = in_file, coords = data)
path.altitudemode = "absolute"
path.extrude = 1
path.style.polystyle.color = "7fff575c"

kml.save(out_file)

