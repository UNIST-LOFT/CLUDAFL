#!/usr/bin/env python3

import numpy as np
from sys import argv

args=argv[1:]
candidates=[]
for i in range(len(args)/2):
    candidates.append((int(args[2*i]),int(args[2*i+1]),))

rng=np.random.default_rng()
samples=[]
for cand in candidates:
    samples.append(rng.beta(cand[0],cand[1]))

print(samples.index(max(samples)))