# Online Repository

Particle Playground includes a built-in repository browser for downloading and uploading community-shared element and molecule files from the [ParticleRepository](https://github.com/Night-Traders-Dev/ParticleRepository) on GitHub.

## Requirements

The repository system requires **libcurl** at build time. If libcurl is not found, the Repository menu items are hidden and the feature is gracefully disabled.

## Accessing the Repository

Open the repository browser via:
- **Tools menu** (wrench icon) → **Online Repository**
- **Pause Menu** (Escape) → **Repository**

## Repository Browser

The browser has two tabs: **Elements** and **Molecules**.

### Search & Filter

- **Search bar**: Type to filter by chemical formula or common name (case-insensitive substring match)
- **Filter buttons**:
  - **All** — show all files
  - **Cached** — only show locally downloaded files
  - **New** — only show files not yet downloaded
  - **Chiral** — only show chiral molecules (Molecules tab only, requires cached v3+ files)
  - **Achiral** — only show achiral molecules (Molecules tab only, requires cached v3+ files)

### Downloading Files

1. Browse or search for a file
2. Click **Download** — a progress bar shows download status
3. Once downloaded, the file is cached locally and the button changes to **Import**
4. Click **Import** to spawn the element/molecule in your simulation

Downloaded files are cached in:
- Linux: `~/.local/share/particle_playground/repository/{elements,molecules}/`
- Windows: `%APPDATA%\ParticlePlayground\repository\{elements,molecules}\`

### Spawning Cached Molecules

Downloaded molecules also appear in the **Spawn Menu** molecule search. Type a formula or name and cached repository files show up with a `[repo]` tag.

### Uploading Files

Uploading requires a GitHub Personal Access Token with write access to the repository.

1. Expand **GitHub Token** section
2. Paste your token and click **Save Token**
3. Expand **Upload to Repository**
4. Enter the full path to a `.ppel` or `.ppmol` file
5. Click **Upload**

## Repository Contents

The repository ships with:
- **140+ element files** (.ppel) — all 118 elements plus common isotopes (Deuterium, Tritium, C-14, U-235, Pu-239, etc.)
- **200+ molecule files** (.ppmol) — covering:
  - Simple molecules (H2O, CO2, NH3, etc.)
  - Organic compounds (alcohols, acids, bases)
  - Amino acids and DNA/RNA building blocks
  - Pharmaceuticals (aspirin, ibuprofen, acetaminophen, etc.)
  - Psychoactive compounds (caffeine, nicotine, serotonin, dopamine, etc.)
  - Semiconductors and advanced materials (GaAs, SiC, BN, TiO2, etc.)
  - Superconducting materials (MgB2, Nb3Sn, NbTi, etc.)
  - Fullerenes (C60 Buckminsterfullerene, C24, C20)
  - Industrial chemicals (ammonium nitrate, sodium carbonate, etc.)
  - Energy molecules (ATP, ADP, AMP)

## File Formats

- **`.ppel`** — Binary element format (magic `0x4C455050`, version 1). Contains protons, neutrons, and electrons with positions and genome data.
- **`.ppmol`** — Binary molecule format (magic `0x4D505050`, version 2). Contains multiple atoms with bond connectivity, formula, and name metadata.
