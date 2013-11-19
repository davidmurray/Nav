#!/usr/bin/python

from simplekml import Kml
import sys

in_file, out_file = sys.argv[1], sys.argv[2];
data = list()

kml = Kml(name = in_file, open = 1)

with open(in_file) as file:
	lines = file.readlines()
	for x in lines:
		elements = x.split(',')[:3]
		group = tuple(elements)
		data.append(group)

path = kml.newlinestring(name = "Flight", description = in_file, coords = data)

kml.save(out_file)

