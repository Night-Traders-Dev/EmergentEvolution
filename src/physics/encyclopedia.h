#pragma once

#include "physics/phys_particles.h"

struct EncyclopediaEntry {
    const char* name;
    const char* symbol;
    const char* category;
    const char* description;
    float mass_mev;
    float charge;
    float spin;
    const char* discovered;
};

// Indexed by phys_particles type index (0..PHYS_PARTICLE_TYPES-1)
inline const EncyclopediaEntry ENCYCLOPEDIA[PHYS_PARTICLE_TYPES] = {
    // 0: Proton
    { "Proton", "p", "Baryon",
      "Stable composite baryon made of two up quarks and one down quark (uud). "
      "The nucleus of hydrogen and a building block of all atomic matter.",
      938.272f, +1.0f, 0.5f, "1917 (Rutherford)" },
    // 1: Neutron
    { "Neutron", "n", "Baryon",
      "Neutral composite baryon (udd). Stable inside nuclei but decays with a ~10 min "
      "half-life when free, via beta decay to a proton, electron, and antineutrino.",
      939.565f, 0.0f, 0.5f, "1932 (Chadwick)" },
    // 2: Electron
    { "Electron", "e-", "Lepton",
      "The lightest charged lepton. Orbits atomic nuclei to form atoms and mediates "
      "chemical bonds. Fundamental particle with no known substructure.",
      0.511f, -1.0f, 0.5f, "1897 (J.J. Thomson)" },
    // 3: Photon
    { "Photon", "g", "Boson",
      "Massless gauge boson that mediates the electromagnetic force. Carries energy "
      "and momentum as quanta of light. Travels at the speed of light.",
      0.0f, 0.0f, 1.0f, "1905 (Einstein)" },
    // 4: Positron
    { "Positron", "e+", "Lepton (anti)",
      "The antimatter counterpart of the electron. Same mass but opposite charge. "
      "Annihilates on contact with electrons, producing gamma photons.",
      0.511f, +1.0f, 0.5f, "1932 (Anderson)" },
    // 5: Antiproton
    { "Antiproton", "p-bar", "Baryon (anti)",
      "Antimatter counterpart of the proton. Composed of two anti-up and one anti-down quark. "
      "Annihilates violently with protons.",
      938.272f, -1.0f, 0.5f, "1955 (Segre & Chamberlain)" },
    // 6: Electron Neutrino
    { "Electron Neutrino", "ve", "Lepton",
      "Nearly massless neutral lepton produced in beta decay. Interacts only via the "
      "weak force, making it extremely difficult to detect.",
      0.0000022f, 0.0f, 0.5f, "1956 (Cowan & Reines)" },
    // 7: Muon
    { "Muon", "u-", "Lepton",
      "Second-generation charged lepton, ~207 times heavier than the electron. "
      "Decays to an electron and two neutrinos with a 2.2 us lifetime.",
      105.658f, -1.0f, 0.5f, "1936 (Anderson & Neddermeyer)" },
    // 8: Antimuon
    { "Antimuon", "u+", "Lepton (anti)",
      "Antimatter counterpart of the muon. Same mass, opposite charge. "
      "Produced in pion decays and cosmic ray showers.",
      105.658f, +1.0f, 0.5f, "1936" },
    // 9: Tau
    { "Tau", "t-", "Lepton",
      "Third-generation charged lepton, ~3477 times heavier than the electron. "
      "Extremely short-lived, decaying to lighter leptons or hadrons.",
      1776.86f, -1.0f, 0.5f, "1975 (Perl)" },
    // 10: Antitau
    { "Antitau", "t+", "Lepton (anti)",
      "Antimatter counterpart of the tau lepton.",
      1776.86f, +1.0f, 0.5f, "1975" },
    // 11: Muon Neutrino
    { "Muon Neutrino", "vu", "Lepton",
      "Second-generation neutrino. Produced alongside muons in weak decays. "
      "Can oscillate into other neutrino flavors.",
      0.17f, 0.0f, 0.5f, "1962 (Lederman, Schwartz, Steinberger)" },
    // 12: Tau Neutrino
    { "Tau Neutrino", "vt", "Lepton",
      "Third-generation neutrino. The last Standard Model fermion to be directly observed. "
      "Associated with tau lepton production and decay.",
      15.5f, 0.0f, 0.5f, "2000 (DONUT collaboration)" },
    // 13: Up Quark
    { "Up Quark", "u", "Quark",
      "Lightest quark with charge +2/3. Two ups and one down make a proton. "
      "Never observed in isolation due to color confinement.",
      2.16f, +0.667f, 0.5f, "1968 (SLAC, Friedman, Kendall, Taylor)" },
    // 14: Down Quark
    { "Down Quark", "d", "Quark",
      "Second-lightest quark with charge -1/3. One up and two downs make a neutron. "
      "Slightly heavier than the up quark.",
      4.67f, -0.333f, 0.5f, "1968 (SLAC)" },
    // 15: Strange Quark
    { "Strange Quark", "s", "Quark",
      "Second-generation quark with charge -1/3. Found in kaons and hyperons. "
      "Its unexpected properties led to the concept of 'strangeness'.",
      93.4f, -0.333f, 0.5f, "1947 (Rochester & Butler)" },
    // 16: Charm Quark
    { "Charm Quark", "c", "Quark",
      "Second-generation quark with charge +2/3. Discovered via the J/psi meson. "
      "Its discovery confirmed the GIM mechanism.",
      1270.0f, +0.667f, 0.5f, "1974 (Richter & Ting)" },
    // 17: Top Quark
    { "Top Quark", "t", "Quark",
      "Heaviest known elementary particle at ~173 GeV. Decays before hadronizing, "
      "making it unique among quarks. Mass close to a gold atom.",
      172760.0f, +0.667f, 0.5f, "1995 (CDF & D0 at Fermilab)" },
    // 18: Bottom Quark
    { "Bottom Quark", "b", "Quark",
      "Third-generation quark with charge -1/3. Forms B mesons used to study CP violation. "
      "Important for understanding matter-antimatter asymmetry.",
      4180.0f, -0.333f, 0.5f, "1977 (Lederman at Fermilab)" },
    // 19-24: Antiquarks
    { "Anti-Up", "u-bar", "Quark (anti)", "Antimatter partner of the up quark.", 2.16f, -0.667f, 0.5f, "1968" },
    { "Anti-Down", "d-bar", "Quark (anti)", "Antimatter partner of the down quark.", 4.67f, +0.333f, 0.5f, "1968" },
    { "Anti-Strange", "s-bar", "Quark (anti)", "Antimatter partner of the strange quark.", 93.4f, +0.333f, 0.5f, "1968" },
    { "Anti-Charm", "c-bar", "Quark (anti)", "Antimatter partner of the charm quark.", 1270.0f, -0.667f, 0.5f, "1974" },
    { "Anti-Top", "t-bar", "Quark (anti)", "Antimatter partner of the top quark.", 172760.0f, -0.667f, 0.5f, "1995" },
    { "Anti-Bottom", "b-bar", "Quark (anti)", "Antimatter partner of the bottom quark.", 4180.0f, +0.333f, 0.5f, "1977" },
    // 25: Gluon
    { "Gluon", "g", "Boson",
      "Massless gauge boson mediating the strong force between quarks. Carries color charge "
      "itself, leading to gluon self-interaction and confinement.",
      0.0f, 0.0f, 1.0f, "1979 (PETRA at DESY)" },
    // 26: W+
    { "W+ Boson", "W+", "Boson",
      "Massive gauge boson mediating charged-current weak interactions. Responsible for "
      "beta decay and flavor-changing processes.",
      80379.0f, +1.0f, 1.0f, "1983 (UA1 & UA2 at CERN)" },
    // 27: W-
    { "W- Boson", "W-", "Boson",
      "Antiparticle of the W+. Same mass, opposite charge. Mediates weak decay processes "
      "where charge decreases.",
      80379.0f, -1.0f, 1.0f, "1983 (UA1 & UA2 at CERN)" },
    // 28: Z0
    { "Z Boson", "Z0", "Boson",
      "Massive neutral gauge boson mediating neutral-current weak interactions. "
      "Discovered alongside the W bosons at CERN.",
      91188.0f, 0.0f, 1.0f, "1983 (UA1 & UA2 at CERN)" },
    // 29: Higgs
    { "Higgs Boson", "H0", "Boson",
      "Scalar boson responsible for giving mass to W/Z bosons and fermions via the "
      "Higgs mechanism. The last Standard Model particle discovered.",
      125100.0f, 0.0f, 0.0f, "2012 (ATLAS & CMS at LHC)" },
    // 30: Graviton
    { "Graviton", "G", "Hypothetical",
      "Theoretical massless spin-2 boson that would mediate gravity in a quantum theory. "
      "Predicted but not yet observed.",
      0.0f, 0.0f, 2.0f, "Predicted" },
    // 31: Dark Matter
    { "Dark Matter", "DM", "Hypothetical",
      "Weakly Interacting Massive Particle (WIMP). Interacts via gravity and possibly "
      "the weak force. Makes up ~27% of the universe's energy content.",
      100000.0f, 0.0f, 0.5f, "Inferred (1930s, Zwicky)" },
    // 32: Dark Energy
    { "Dark Energy", "DE", "Hypothetical",
      "Quintessence field quantum. Produces a universal repulsive force that grows with "
      "distance, driving the accelerating expansion of the universe.",
      0.001f, 0.0f, 0.0f, "Inferred (1998, Riess & Perlmutter)" },
    // 33: Axino
    { "Axino", "a~", "Hypothetical",
      "Supersymmetric partner of the axion. A fermion dark matter candidate.",
      0.001f, 0.0f, 0.5f, "Predicted" },
    // 34: WIMPzilla
    { "WIMPzilla", "WZ", "Hypothetical",
      "Super-heavy dark matter particle with mass ~10^12 GeV. Could have been produced "
      "gravitationally during inflation.",
      1e12f, 0.0f, 0.0f, "Predicted" },
    // 35: SIMP
    { "SIMP", "S", "Hypothetical",
      "Strongly Interacting Massive Particle. Dark matter candidate that interacts via "
      "a dark strong force, forming self-interacting dark matter halos.",
      1000.0f, 0.0f, 0.0f, "Predicted" },
    // 36: Sterile Neutrino
    { "Sterile Neutrino", "vs", "Hypothetical",
      "Right-handed neutrino that doesn't interact via any Standard Model force except gravity. "
      "Could explain neutrino masses via the seesaw mechanism.",
      7000.0f, 0.0f, 0.5f, "Predicted" },
    // 37: Dark Photon
    { "Dark Photon", "A'", "Hypothetical",
      "Gauge boson of a hypothetical dark electromagnetism. Could kinetically mix with "
      "the ordinary photon, providing a portal to the dark sector.",
      10.0f, 0.0f, 1.0f, "Predicted" },
    // 38: Q-ball
    { "Q-ball", "Q", "Hypothetical",
      "Non-topological soliton — a stable lump of scalar field energy carrying a conserved "
      "global charge. Could form in SUSY flat directions.",
      1e6f, 0.0f, 0.0f, "1985 (Coleman)" },
    // 39-49: SUSY sparticles
    { "Selectron", "e~", "SUSY", "Scalar partner of the electron.", 200000.0f, -1.0f, 0.0f, "Predicted" },
    { "Smuon", "u~", "SUSY", "Scalar partner of the muon.", 300000.0f, -1.0f, 0.0f, "Predicted" },
    { "Stau", "t~", "SUSY", "Scalar partner of the tau.", 250000.0f, -1.0f, 0.0f, "Predicted" },
    { "Squark", "q~", "SUSY", "Scalar partner of a quark. Carries both color and electric charge.", 500000.0f, 0.667f, 0.0f, "Predicted" },
    { "Gluino", "g~", "SUSY", "Fermionic partner of the gluon. Carries color charge like a gluon but has spin-1/2.", 1000000.0f, 0.0f, 0.5f, "Predicted" },
    { "Photino", "y~", "SUSY", "Fermionic partner of the photon. Mixes with other neutralinos.", 100000.0f, 0.0f, 0.5f, "Predicted" },
    { "Wino", "W~", "SUSY", "Fermionic partner of the W boson.", 200000.0f, 0.0f, 0.5f, "Predicted" },
    { "Zino", "Z~", "SUSY", "Fermionic partner of the Z boson.", 300000.0f, 0.0f, 0.5f, "Predicted" },
    { "Higgsino", "H~", "SUSY", "Fermionic partner of the Higgs boson.", 200000.0f, 0.0f, 0.5f, "Predicted" },
    { "Neutralino", "X0", "SUSY",
      "Lightest Supersymmetric Particle (LSP). A leading dark matter candidate, "
      "it's a mixture of photino, zino, and higgsino states.",
      100000.0f, 0.0f, 0.5f, "Predicted" },
    { "Sneutrino", "v~", "SUSY", "Scalar partner of the neutrino.", 150000.0f, 0.0f, 0.0f, "Predicted" },
    // 50-55: Force carriers
    { "Gravitino", "G~", "SUSY", "Spin-3/2 partner of the graviton in supergravity theories.", 1000.0f, 0.0f, 1.5f, "Predicted" },
    { "X Boson", "X", "GUT", "Grand Unified Theory leptoquark. Would mediate proton decay.", 1e15f, 0.0f, 1.0f, "Predicted" },
    { "Y Boson", "Y", "GUT", "Grand Unified Theory leptoquark. Partner of the X boson.", 1e15f, 0.0f, 1.0f, "Predicted" },
    { "Monopole", "M", "Hypothetical",
      "Magnetic monopole — a particle carrying isolated magnetic charge. Predicted by "
      "Dirac (1931) and by GUTs. Would explain charge quantization.",
      1e16f, 0.0f, 0.0f, "1931 (Dirac, predicted)" },
    { "Radion", "r", "Extra Dimensions", "Scalar field controlling the size of extra dimensions in Randall-Sundrum models.", 1000.0f, 0.0f, 0.0f, "Predicted" },
    { "Dilaton", "D", "String Theory", "Massless scalar from string theory controlling the string coupling constant.", 0.0f, 0.0f, 0.0f, "Predicted" },
    // 56-64: Theoretical extremes
    { "Tachyon", "T", "Hypothetical",
      "Theoretical particle with imaginary mass that always travels faster than light. "
      "Signals instability in the vacuum rather than faster-than-light communication.",
      0.0f, 0.0f, 0.0f, "1967 (Feinberg)" },
    { "Preon", "P", "Hypothetical", "Hypothetical sub-quark constituent, implying quarks and leptons are composite.", 0.0f, 0.0f, 0.5f, "Predicted" },
    { "Inflaton", "I", "Cosmology",
      "Scalar field driving cosmic inflation — the exponential expansion of the early universe. "
      "Its potential energy dominated the universe's energy budget for ~10^-32 seconds.",
      1e13f, 0.0f, 0.0f, "1981 (Guth)" },
    { "Majoron", "J", "Hypothetical", "Goldstone boson from spontaneous lepton number violation. Related to Majorana neutrino masses.", 0.0f, 0.0f, 0.0f, "Predicted" },
    { "Odderon", "O", "QCD", "C-odd partner of the Pomeron — a bound state of an odd number of gluons.", 0.0f, 0.0f, 0.0f, "2021 (TOTEM & D0)" },
    { "Glueball", "gg", "QCD", "Bound state of pure gluons with no valence quarks. Predicted by QCD but difficult to identify experimentally.", 1500.0f, 0.0f, 0.0f, "Predicted (QCD)" },
    { "Skyrmion", "Sk", "Topological", "Topological soliton in the pion field that models baryons. A non-perturbative description of protons/neutrons.", 938.0f, 0.0f, 0.5f, "1961 (Skyrme)" },
    { "X17", "X17", "Hypothetical",
      "Light boson at ~17 MeV observed in anomalous nuclear transitions at ATOMKI. "
      "Could indicate a fifth fundamental force if confirmed.",
      17.0f, 0.0f, 1.0f, "2015 (ATOMKI, Krasznahorkay)" },
    { "Chameleon", "Ch", "Dark Energy", "Scalar field with environment-dependent mass. Light in empty space, heavy near matter. A dark energy candidate.", 0.001f, 0.0f, 0.0f, "2004 (Khoury & Weltman)" },
    // 65-66: New class
    { "Paraparticle", "Pp", "Exotic", "Particle obeying parastatistics — neither purely bosonic nor fermionic. Theoretical framework for exotic quantum statistics.", 0.0f, 0.0f, 0.0f, "Predicted" },
    { "Dynamical Axion QP", "aQP", "Condensed Matter", "Dynamical axion quasiparticle arising in topological insulators. A condensed-matter analog of the axion.", 0.0f, 0.0f, 0.0f, "2019" },
};
