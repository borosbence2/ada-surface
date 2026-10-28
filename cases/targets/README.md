# Fit targets

## drivaer-hood.ply

The right half (y >= 0) of the hood of the DrivAer car model: the part
`BodyHood` of `run_1/drivaer_1.stl` from the DrivAerML dataset, converted to
millimetres. 744 vertices and 1350 triangles of the CAD tessellation; the
vertices lie on the CAD surface. `scripts/drivaer_hood.py` downloads the
source and writes this file and `cases/L3-hood-from-target.ada.json`.

**Source:** N. Ashton, C. Mockett, M. Fuchs, L. Fliessbach, H. Hetmann,
T. Knacke, N. Schonwald, V. Skaperdas, G. Fotiadis, A. Walle, B. Hupertz,
D. Maddix: *DrivAerML: High-Fidelity Computational Fluid Dynamics Dataset for
Road-Car External Aerodynamics*, arXiv:2408.11969 (2024).
<https://huggingface.co/datasets/neashton/drivaerml>

**Licence:** the dataset is licensed under
[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/), and so is
this file, a derivative of it (extracted, halved, converted to mm). The licence
covers this data file only, not the rest of the repository.
