# Save / Load

Five binary formats for simulation and progression data:

| Format | Extension | Contents | Access |
|---|---|---|---|
| **Simulation** | `.ppsg` | Full state: config, particles, types, force objects, UI state | Ctrl+S / Ctrl+L, bottom bar, pause menu |
| **Element** | `.ppel` | Single element: Z, N, electrons, relative positions | Element Detail Card > Export; Menu > Tools > Import |
| **Molecule** | `.ppmol` | Molecule: formula, atoms, bond list, relative positions | Molecule Detail Card > Export; Menu > Tools > Import |
| **Achievements** | `.ppach` | 256-bit unlock bitfield, session counters, discovery state | Automatic (v6, backward-compatible with v1&ndash;v5) |
| **Lifetime Stats** | `.ppstats` | Career totals, per-type/element stats, chirality, all-time peaks | Automatic (auto-saved every 30s and on exit) |

Simulation saves capture a 320&times;180 PNG thumbnail. The load dialog displays saves as a
card grid with preview images, names, dates, and file sizes. Element and molecule files store
positions as offsets from centroid for portability. Current save format version is **6** (expanded
from 74 to 282 particle types for meson support). Saves from earlier versions (v5 and below) are
loaded with automatic zero-padding of the force/color matrices to 282 types.

**Auto-save**: configurable interval (Off / 2 / 5 / 10 minutes) saves automatically during
simulation. Interval is set in **Settings > Performance**.

**Data directory**: saves, settings, and achievements are stored in platform-standard locations:
- **Linux**: `~/.local/share/particle_playground/`
- **Windows**: `%APPDATA%\ParticlePlayground\`
- **Portable**: `saves/` relative to executable (build with `-DPORTABLE_PATHS=ON`)

Existing saves in the old `saves/` directory are automatically migrated on first run.

## Molecule Tools

The `ppmol_gen` standalone tool and `ppmol_gen.py` Python script generate `.ppmol` molecule files
for import into the simulation.

**C++ tool** (`tools/ppmol/ppmol_gen.cpp`):

```bash
./build.sh tools                                    # build all tools
./build/ppmol_gen gen water.ppmol H2O               # generate from formula
./build/ppmol_gen gen glucose.ppmol C6H12O6          # complex molecules
./build/ppmol_gen gen salt.ppmol NaCl               # ionic compounds
./build/ppmol_gen dump water.ppmol                   # inspect file contents
./build/ppmol_gen validate water.ppmol               # check file integrity
./build/ppmol_gen render water.ppmol water.png       # render to PNG
```

Supports chemical formulas with parentheses (`Ca(OH)2`), generates proper nuclear structure
(protons, neutrons, electrons in shells), and creates valence-aware covalent bonds.

**Python script** (`tools/ppmol/ppmol_gen.py`) &mdash; for real molecular geometry via RDKit:

```bash
# Download from PubChem by CID
python3 tools/ppmol/ppmol_gen.py --pubchem-cid 2519 --out caffeine.ppmol --normalize

# Download by compound name
python3 tools/ppmol/ppmol_gen.py --pubchem-name "aspirin" --out aspirin.ppmol --normalize

# Convert local SDF file
python3 tools/ppmol/ppmol_gen.py --sdf molecule.sdf --out molecule.ppmol --normalize

# From SMILES string
python3 tools/ppmol/ppmol_gen.py --smiles "CCO" --out ethanol.ppmol --formula C2H6O --normalize

# Formula-only mode (no RDKit needed)
python3 tools/ppmol/ppmol_gen.py --formula-only "H2O" --out water.ppmol
```

Requires RDKit (`python3-rdkit`) for SDF/MOL/SMILES modes. PubChem download and formula-only
mode work without RDKit.

## Online Repository

The simulation connects to an online repository
([Night-Traders-Dev/ParticleRepository](https://github.com/Night-Traders-Dev/ParticleRepository))
for downloading and uploading `.ppel` and `.ppmol` files. HTTP requests use libcurl and JSON
responses are parsed with cJSON.

Downloaded files are cached locally so subsequent loads are instant. The repository browser
(accessible from **Menu > Tools > Repository**) provides:

- **Search** bar for filtering by name or formula
- **Elements / Molecules** tabs
- **Filter** toggle: All, Cached, New
- **Download / Import** workflow: preview metadata, download to cache, then import into the
  active simulation

Downloaded molecules also appear in the **Spawn Picker (F3)** molecule list with a `[repo]` tag.

For full details on the repository protocol, caching strategy, and upload workflow, see
[docs/online-repository.md](online-repository.md).
