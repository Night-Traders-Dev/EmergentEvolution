#pragma once

#include <cstdint>
#include <glm/glm.hpp>

// ── Meson type range ─────────────────────────────────────────────────────────
// Types 74–261: all known PDG meson states (188 entries)
// Types 262–281: reserved for future discoveries

static constexpr uint32_t MESON_TYPE_FIRST = 74;
static constexpr uint32_t MESON_TYPE_LAST  = 261;
static constexpr uint32_t MESON_TYPE_COUNT = MESON_TYPE_LAST - MESON_TYPE_FIRST + 1; // 188

inline bool is_meson_type(uint32_t t) { return t >= MESON_TYPE_FIRST && t <= MESON_TYPE_LAST; }

// ── Light Unflavored Mesons (types 74–145) ───────────────────────────────────
// Pseudoscalar (J^P = 0^-)
static constexpr uint32_t PION_PLUS_MESON       = 74;   // π⁺ (ud̄)
static constexpr uint32_t PION_ZERO_MESON       = 75;   // π⁰ ((uū−dd̄)/√2)
static constexpr uint32_t PION_MINUS_MESON      = 76;   // π⁻ (dū)
static constexpr uint32_t ETA_MESON             = 77;   // η
static constexpr uint32_t ETA_PRIME_MESON       = 78;   // η'(958)
static constexpr uint32_t PION_1300_PLUS        = 79;   // π(1300)⁺
static constexpr uint32_t PION_1300_ZERO        = 80;   // π(1300)⁰
static constexpr uint32_t PION_1300_MINUS       = 81;   // π(1300)⁻
static constexpr uint32_t ETA_1295_MESON        = 82;   // η(1295)
static constexpr uint32_t ETA_1475_MESON        = 83;   // η(1475)
static constexpr uint32_t PION_1800_PLUS        = 84;   // π(1800)⁺
static constexpr uint32_t PION_1800_ZERO        = 85;   // π(1800)⁰
static constexpr uint32_t PION_1800_MINUS       = 86;   // π(1800)⁻
// Vector (J^P = 1^-)
static constexpr uint32_t RHO_770_PLUS          = 87;   // ρ(770)⁺
static constexpr uint32_t RHO_770_ZERO          = 88;   // ρ(770)⁰
static constexpr uint32_t RHO_770_MINUS         = 89;   // ρ(770)⁻
static constexpr uint32_t OMEGA_782_MESON       = 90;   // ω(782)
static constexpr uint32_t PHI_1020_MESON        = 91;   // φ(1020) (ss̄)
static constexpr uint32_t RHO_1450_PLUS         = 92;   // ρ(1450)⁺
static constexpr uint32_t RHO_1450_ZERO         = 93;   // ρ(1450)⁰
static constexpr uint32_t RHO_1450_MINUS        = 94;   // ρ(1450)⁻
static constexpr uint32_t OMEGA_1420_MESON      = 95;   // ω(1420)
static constexpr uint32_t PHI_1680_MESON        = 96;   // φ(1680)
static constexpr uint32_t RHO_1700_PLUS         = 97;   // ρ(1700)⁺
static constexpr uint32_t RHO_1700_ZERO         = 98;   // ρ(1700)⁰
static constexpr uint32_t RHO_1700_MINUS        = 99;   // ρ(1700)⁻
static constexpr uint32_t OMEGA_1650_MESON      = 100;  // ω(1650)
// Scalar (J^P = 0^+)
static constexpr uint32_t F0_500_MESON          = 101;  // f₀(500)/σ
static constexpr uint32_t A0_980_PLUS           = 102;  // a₀(980)⁺
static constexpr uint32_t A0_980_ZERO           = 103;  // a₀(980)⁰
static constexpr uint32_t A0_980_MINUS          = 104;  // a₀(980)⁻
static constexpr uint32_t F0_980_MESON          = 105;  // f₀(980)
static constexpr uint32_t F0_1370_MESON         = 106;  // f₀(1370)
static constexpr uint32_t A0_1450_PLUS          = 107;  // a₀(1450)⁺
static constexpr uint32_t A0_1450_ZERO          = 108;  // a₀(1450)⁰
static constexpr uint32_t A0_1450_MINUS         = 109;  // a₀(1450)⁻
static constexpr uint32_t F0_1500_MESON         = 110;  // f₀(1500)
static constexpr uint32_t F0_1710_MESON         = 111;  // f₀(1710)
// Axial-vector (J^P = 1^+)
static constexpr uint32_t B1_1235_PLUS          = 112;  // b₁(1235)⁺
static constexpr uint32_t B1_1235_ZERO          = 113;  // b₁(1235)⁰
static constexpr uint32_t B1_1235_MINUS         = 114;  // b₁(1235)⁻
static constexpr uint32_t H1_1170_MESON         = 115;  // h₁(1170)
static constexpr uint32_t A1_1260_PLUS          = 116;  // a₁(1260)⁺
static constexpr uint32_t A1_1260_ZERO          = 117;  // a₁(1260)⁰
static constexpr uint32_t A1_1260_MINUS         = 118;  // a₁(1260)⁻
static constexpr uint32_t F1_1285_MESON         = 119;  // f₁(1285)
static constexpr uint32_t F1_1420_MESON         = 120;  // f₁(1420)
static constexpr uint32_t H1_1415_MESON         = 121;  // h₁(1415)
static constexpr uint32_t A1_1640_PLUS          = 122;  // a₁(1640)⁺
static constexpr uint32_t A1_1640_ZERO          = 123;  // a₁(1640)⁰
static constexpr uint32_t A1_1640_MINUS         = 124;  // a₁(1640)⁻
// Tensor (J^P = 2^+)
static constexpr uint32_t A2_1320_PLUS          = 125;  // a₂(1320)⁺
static constexpr uint32_t A2_1320_ZERO          = 126;  // a₂(1320)⁰
static constexpr uint32_t A2_1320_MINUS         = 127;  // a₂(1320)⁻
static constexpr uint32_t F2_1270_MESON         = 128;  // f₂(1270)
static constexpr uint32_t F2P_1525_MESON        = 129;  // f₂'(1525) (ss̄)
static constexpr uint32_t A2_1700_PLUS          = 130;  // a₂(1700)⁺
static constexpr uint32_t A2_1700_ZERO          = 131;  // a₂(1700)⁰
static constexpr uint32_t A2_1700_MINUS         = 132;  // a₂(1700)⁻
static constexpr uint32_t F2_1950_MESON         = 133;  // f₂(1950)
static constexpr uint32_t F2_2010_MESON         = 134;  // f₂(2010)
// Pseudotensor (J^P = 2^-)
static constexpr uint32_t PI2_1670_PLUS         = 135;  // π₂(1670)⁺
static constexpr uint32_t PI2_1670_ZERO         = 136;  // π₂(1670)⁰
static constexpr uint32_t PI2_1670_MINUS        = 137;  // π₂(1670)⁻
static constexpr uint32_t ETA2_1645_MESON       = 138;  // η₂(1645)
static constexpr uint32_t ETA2_1870_MESON       = 139;  // η₂(1870)
// Higher spin (J ≥ 3)
static constexpr uint32_t RHO3_1690_PLUS        = 140;  // ρ₃(1690)⁺
static constexpr uint32_t RHO3_1690_ZERO        = 141;  // ρ₃(1690)⁰
static constexpr uint32_t RHO3_1690_MINUS       = 142;  // ρ₃(1690)⁻
static constexpr uint32_t OMEGA3_1670_MESON     = 143;  // ω₃(1670)
static constexpr uint32_t PHI3_1850_MESON       = 144;  // φ₃(1850)
static constexpr uint32_t A4_2040_PLUS          = 145;  // a₄(2040)⁺

// ── Light unflavored continued (146–149 overflow from 72-block) ──────────────
// Actually, using 146-149 for the last light mesons and 150+ for strange
static constexpr uint32_t A4_2040_ZERO          = 146;  // a₄(2040)⁰
static constexpr uint32_t A4_2040_MINUS         = 147;  // a₄(2040)⁻
static constexpr uint32_t F4_2050_MESON         = 148;  // f₄(2050)

// ── Strange Mesons (types 149–174) ───────────────────────────────────────────
static constexpr uint32_t KAON_PLUS_MESON       = 149;  // K⁺ (us̄)
static constexpr uint32_t KAON_ZERO_MESON       = 150;  // K⁰ (ds̄)
static constexpr uint32_t KAON_MINUS_MESON      = 151;  // K⁻ (sū)
static constexpr uint32_t KAON_ZERO_BAR_MESON   = 152;  // K̄⁰ (sd̄)
static constexpr uint32_t KSTAR_892_PLUS        = 153;  // K*(892)⁺
static constexpr uint32_t KSTAR_892_ZERO        = 154;  // K*(892)⁰
static constexpr uint32_t KSTAR_892_MINUS       = 155;  // K*(892)⁻
static constexpr uint32_t KSTAR_892_ZERO_BAR    = 156;  // K̄*(892)⁰
static constexpr uint32_t K0STAR_700_PLUS       = 157;  // K₀*(700)⁺/κ⁺
static constexpr uint32_t K0STAR_700_ZERO       = 158;  // K₀*(700)⁰/κ⁰
static constexpr uint32_t K1_1270_PLUS          = 159;  // K₁(1270)⁺
static constexpr uint32_t K1_1270_ZERO          = 160;  // K₁(1270)⁰
static constexpr uint32_t K1_1400_PLUS          = 161;  // K₁(1400)⁺
static constexpr uint32_t K1_1400_ZERO          = 162;  // K₁(1400)⁰
static constexpr uint32_t KSTAR_1410_PLUS       = 163;  // K*(1410)⁺
static constexpr uint32_t KSTAR_1410_ZERO       = 164;  // K*(1410)⁰
static constexpr uint32_t K0STAR_1430_PLUS      = 165;  // K₀*(1430)⁺
static constexpr uint32_t K0STAR_1430_ZERO      = 166;  // K₀*(1430)⁰
static constexpr uint32_t K2STAR_1430_PLUS      = 167;  // K₂*(1430)⁺
static constexpr uint32_t K2STAR_1430_ZERO      = 168;  // K₂*(1430)⁰
static constexpr uint32_t KSTAR_1680_PLUS       = 169;  // K*(1680)⁺
static constexpr uint32_t KSTAR_1680_ZERO       = 170;  // K*(1680)⁰
static constexpr uint32_t K3STAR_1780_PLUS      = 171;  // K₃*(1780)⁺
static constexpr uint32_t K3STAR_1780_ZERO      = 172;  // K₃*(1780)⁰
static constexpr uint32_t K4STAR_2045_PLUS      = 173;  // K₄*(2045)⁺
static constexpr uint32_t K4STAR_2045_ZERO      = 174;  // K₄*(2045)⁰

// ── Charmed Mesons (types 175–196) ───────────────────────────────────────────
static constexpr uint32_t D_PLUS_MESON          = 175;  // D⁺ (cd̄)
static constexpr uint32_t D_ZERO_MESON          = 176;  // D⁰ (cū)
static constexpr uint32_t D_MINUS_MESON         = 177;  // D⁻ (dc̄)
static constexpr uint32_t D_ZERO_BAR_MESON      = 178;  // D̄⁰ (uc̄)
static constexpr uint32_t DS_PLUS_MESON         = 179;  // Ds⁺ (cs̄)
static constexpr uint32_t DS_MINUS_MESON        = 180;  // Ds⁻ (sc̄)
static constexpr uint32_t DSTAR_2007_ZERO       = 181;  // D*(2007)⁰
static constexpr uint32_t DSTAR_2010_PLUS       = 182;  // D*(2010)⁺
static constexpr uint32_t DSTAR_2010_MINUS      = 183;  // D*(2010)⁻
static constexpr uint32_t DSTAR_2007_ZERO_BAR   = 184;  // D̄*(2007)⁰
static constexpr uint32_t DS_STAR_PLUS          = 185;  // Ds*⁺
static constexpr uint32_t DS_STAR_MINUS         = 186;  // Ds*⁻
static constexpr uint32_t D0STAR_2300_ZERO      = 187;  // D₀*(2300)⁰
static constexpr uint32_t D0STAR_2300_PLUS      = 188;  // D₀*(2300)⁺
static constexpr uint32_t DS0STAR_2317          = 189;  // Ds₀*(2317)⁺
static constexpr uint32_t D1_2420_ZERO          = 190;  // D₁(2420)⁰
static constexpr uint32_t D1_2420_PLUS          = 191;  // D₁(2420)⁺
static constexpr uint32_t DS1_2460              = 192;  // Ds₁(2460)⁺
static constexpr uint32_t DS1_2536              = 193;  // Ds₁(2536)⁺
static constexpr uint32_t D2STAR_2460_ZERO      = 194;  // D₂*(2460)⁰
static constexpr uint32_t D2STAR_2460_PLUS      = 195;  // D₂*(2460)⁺
static constexpr uint32_t DS2STAR_2573          = 196;  // Ds₂*(2573)⁺

// ── Bottom Mesons (types 197–216) ────────────────────────────────────────────
static constexpr uint32_t B_PLUS_MESON          = 197;  // B⁺ (ub̄)
static constexpr uint32_t B_ZERO_MESON          = 198;  // B⁰ (db̄)
static constexpr uint32_t B_MINUS_MESON         = 199;  // B⁻ (bū)
static constexpr uint32_t B_ZERO_BAR_MESON      = 200;  // B̄⁰ (bd̄)
static constexpr uint32_t BS_ZERO_MESON         = 201;  // Bs⁰ (sb̄)
static constexpr uint32_t BS_ZERO_BAR_MESON     = 202;  // B̄s⁰ (bs̄)
static constexpr uint32_t BC_PLUS_MESON         = 203;  // Bc⁺ (cb̄)
static constexpr uint32_t BC_MINUS_MESON        = 204;  // Bc⁻ (bc̄)
static constexpr uint32_t BSTAR_PLUS            = 205;  // B*⁺
static constexpr uint32_t BSTAR_ZERO            = 206;  // B*⁰
static constexpr uint32_t BSTAR_MINUS           = 207;  // B*⁻
static constexpr uint32_t BSTAR_ZERO_BAR        = 208;  // B̄*⁰
static constexpr uint32_t BS_STAR_ZERO          = 209;  // Bs*⁰
static constexpr uint32_t BS_STAR_ZERO_BAR      = 210;  // B̄s*⁰
static constexpr uint32_t B1_5721_ZERO          = 211;  // B₁(5721)⁰
static constexpr uint32_t B1_5721_PLUS          = 212;  // B₁(5721)⁺
static constexpr uint32_t B2STAR_5747_ZERO      = 213;  // B₂*(5747)⁰
static constexpr uint32_t B2STAR_5747_PLUS      = 214;  // B₂*(5747)⁺
static constexpr uint32_t BS1_5830              = 215;  // Bs₁(5830)⁰
static constexpr uint32_t BS2STAR_5840          = 216;  // Bs₂*(5840)⁰

// ── Charmonium cc̄ (types 217–234) ───────────────────────────────────────────
static constexpr uint32_t ETA_C_1S              = 217;  // ηc(1S)
static constexpr uint32_t JPSI_MESON            = 218;  // J/ψ(1S)
static constexpr uint32_t CHI_C0_1P             = 219;  // χc0(1P)
static constexpr uint32_t CHI_C1_1P             = 220;  // χc1(1P)
static constexpr uint32_t H_C_1P               = 221;  // hc(1P)
static constexpr uint32_t CHI_C2_1P             = 222;  // χc2(1P)
static constexpr uint32_t ETA_C_2S              = 223;  // ηc(2S)
static constexpr uint32_t PSI_2S                = 224;  // ψ(2S)
static constexpr uint32_t PSI_3770              = 225;  // ψ(3770)
static constexpr uint32_t X_3872                = 226;  // X(3872)/χc1(3872)
static constexpr uint32_t CHI_C0_2P             = 227;  // χc0(3860)
static constexpr uint32_t CHI_C2_3930           = 228;  // χc2(3930)
static constexpr uint32_t PSI_4040              = 229;  // ψ(4040)
static constexpr uint32_t PSI_4160              = 230;  // ψ(4160)
static constexpr uint32_t PSI_4230              = 231;  // ψ(4230)/Y(4230)
static constexpr uint32_t PSI_4360              = 232;  // ψ(4360)
static constexpr uint32_t PSI_4415              = 233;  // ψ(4415)
static constexpr uint32_t PSI_4660              = 234;  // ψ(4660)

// ── Bottomonium bb̄ (types 235–252) ──────────────────────────────────────────
static constexpr uint32_t ETA_B_1S              = 235;  // ηb(1S)
static constexpr uint32_t UPSILON_1S            = 236;  // Υ(1S)
static constexpr uint32_t CHI_B0_1P             = 237;  // χb0(1P)
static constexpr uint32_t CHI_B1_1P             = 238;  // χb1(1P)
static constexpr uint32_t H_B_1P               = 239;  // hb(1P)
static constexpr uint32_t CHI_B2_1P             = 240;  // χb2(1P)
static constexpr uint32_t ETA_B_2S              = 241;  // ηb(2S)
static constexpr uint32_t UPSILON_2S            = 242;  // Υ(2S)
static constexpr uint32_t CHI_B0_2P             = 243;  // χb0(2P)
static constexpr uint32_t CHI_B1_2P             = 244;  // χb1(2P)
static constexpr uint32_t H_B_2P               = 245;  // hb(2P)
static constexpr uint32_t CHI_B2_2P             = 246;  // χb2(2P)
static constexpr uint32_t UPSILON_3S            = 247;  // Υ(3S)
static constexpr uint32_t CHI_B1_3P             = 248;  // χb1(3P)
static constexpr uint32_t CHI_B2_3P             = 249;  // χb2(3P)
static constexpr uint32_t UPSILON_4S            = 250;  // Υ(4S)
static constexpr uint32_t UPSILON_10860         = 251;  // Υ(10860)
static constexpr uint32_t UPSILON_11020         = 252;  // Υ(11020)

// ── Exotic Meson Candidates (types 253–261) ──────────────────────────────────
static constexpr uint32_t ZC_3900_PLUS          = 253;  // Zc(3900)⁺
static constexpr uint32_t ZC_3900_MINUS         = 254;  // Zc(3900)⁻
static constexpr uint32_t ZC_4020_PLUS          = 255;  // Zc(4020)⁺
static constexpr uint32_t ZC_4020_MINUS         = 256;  // Zc(4020)⁻
static constexpr uint32_t Z_4430_PLUS           = 257;  // Z(4430)⁺
static constexpr uint32_t Z_4430_MINUS          = 258;  // Z(4430)⁻
static constexpr uint32_t X_4140_MESON          = 259;  // X(4140)
static constexpr uint32_t ZB_10610_PLUS         = 260;  // Zb(10610)⁺
static constexpr uint32_t ZB_10610_MINUS        = 261;  // Zb(10610)⁻

// ── PDG Data Arrays ──────────────────────────────────────────────────────────
// All indexed [type - MESON_TYPE_FIRST], i.e., [0..187]

// Electric charge
static constexpr float MESON_CHARGE[MESON_TYPE_COUNT] = {
    // 74-78: π⁺ π⁰ π⁻ η η'
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,
    // 79-86: π(1300)±⁰, η(1295), η(1475), π(1800)±⁰
     1.0f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f, -1.0f,
    // 87-89: ρ(770)±⁰
     1.0f,  0.0f, -1.0f,
    // 90-91: ω(782) φ(1020)
     0.0f,  0.0f,
    // 92-94: ρ(1450)±⁰
     1.0f,  0.0f, -1.0f,
    // 95-96: ω(1420) φ(1680)
     0.0f,  0.0f,
    // 97-99: ρ(1700)±⁰
     1.0f,  0.0f, -1.0f,
    // 100: ω(1650)
     0.0f,
    // 101: f₀(500)
     0.0f,
    // 102-104: a₀(980)±⁰
     1.0f,  0.0f, -1.0f,
    // 105: f₀(980)
     0.0f,
    // 106: f₀(1370)
     0.0f,
    // 107-109: a₀(1450)±⁰
     1.0f,  0.0f, -1.0f,
    // 110-111: f₀(1500) f₀(1710)
     0.0f,  0.0f,
    // 112-114: b₁(1235)±⁰
     1.0f,  0.0f, -1.0f,
    // 115: h₁(1170)
     0.0f,
    // 116-118: a₁(1260)±⁰
     1.0f,  0.0f, -1.0f,
    // 119-121: f₁(1285) f₁(1420) h₁(1415)
     0.0f,  0.0f,  0.0f,
    // 122-124: a₁(1640)±⁰
     1.0f,  0.0f, -1.0f,
    // 125-127: a₂(1320)±⁰
     1.0f,  0.0f, -1.0f,
    // 128-129: f₂(1270) f₂'(1525)
     0.0f,  0.0f,
    // 130-132: a₂(1700)±⁰
     1.0f,  0.0f, -1.0f,
    // 133-134: f₂(1950) f₂(2010)
     0.0f,  0.0f,
    // 135-137: π₂(1670)±⁰
     1.0f,  0.0f, -1.0f,
    // 138-139: η₂(1645) η₂(1870)
     0.0f,  0.0f,
    // 140-142: ρ₃(1690)±⁰
     1.0f,  0.0f, -1.0f,
    // 143-144: ω₃(1670) φ₃(1850)
     0.0f,  0.0f,
    // 145: a₄(2040)⁺
     1.0f,
    // 146-148: a₄(2040)⁰⁻ f₄(2050)
     0.0f, -1.0f,  0.0f,
    // 149-152: K⁺ K⁰ K⁻ K̄⁰
     1.0f,  0.0f, -1.0f,  0.0f,
    // 153-156: K*(892)⁺⁰⁻ K̄*⁰
     1.0f,  0.0f, -1.0f,  0.0f,
    // 157-158: K₀*(700)⁺⁰
     1.0f,  0.0f,
    // 159-162: K₁(1270)⁺⁰ K₁(1400)⁺⁰
     1.0f,  0.0f,  1.0f,  0.0f,
    // 163-166: K*(1410)⁺⁰ K₀*(1430)⁺⁰
     1.0f,  0.0f,  1.0f,  0.0f,
    // 167-170: K₂*(1430)⁺⁰ K*(1680)⁺⁰
     1.0f,  0.0f,  1.0f,  0.0f,
    // 171-174: K₃*(1780)⁺⁰ K₄*(2045)⁺⁰
     1.0f,  0.0f,  1.0f,  0.0f,
    // 175-178: D⁺ D⁰ D⁻ D̄⁰
     1.0f,  0.0f, -1.0f,  0.0f,
    // 179-180: Ds⁺ Ds⁻
     1.0f, -1.0f,
    // 181-184: D*(2007)⁰ D*(2010)⁺⁻ D̄*(2007)⁰
     0.0f,  1.0f, -1.0f,  0.0f,
    // 185-186: Ds*⁺ Ds*⁻
     1.0f, -1.0f,
    // 187-188: D₀*(2300)⁰⁺
     0.0f,  1.0f,
    // 189: Ds₀*(2317)⁺
     1.0f,
    // 190-191: D₁(2420)⁰⁺
     0.0f,  1.0f,
    // 192-193: Ds₁(2460)⁺ Ds₁(2536)⁺
     1.0f,  1.0f,
    // 194-195: D₂*(2460)⁰⁺
     0.0f,  1.0f,
    // 196: Ds₂*(2573)⁺
     1.0f,
    // 197-200: B⁺ B⁰ B⁻ B̄⁰
     1.0f,  0.0f, -1.0f,  0.0f,
    // 201-202: Bs⁰ B̄s⁰
     0.0f,  0.0f,
    // 203-204: Bc⁺ Bc⁻
     1.0f, -1.0f,
    // 205-208: B*⁺⁰⁻ B̄*⁰
     1.0f,  0.0f, -1.0f,  0.0f,
    // 209-210: Bs*⁰ B̄s*⁰
     0.0f,  0.0f,
    // 211-214: B₁(5721)⁰⁺ B₂*(5747)⁰⁺
     0.0f,  1.0f,  0.0f,  1.0f,
    // 215-216: Bs₁(5830)⁰ Bs₂*(5840)⁰
     0.0f,  0.0f,
    // 217-218: ηc(1S) J/ψ
     0.0f,  0.0f,
    // 219-222: χc0(1P) χc1(1P) hc(1P) χc2(1P)
     0.0f,  0.0f,  0.0f,  0.0f,
    // 223-225: ηc(2S) ψ(2S) ψ(3770)
     0.0f,  0.0f,  0.0f,
    // 226-228: X(3872) χc0(2P) χc2(3930)
     0.0f,  0.0f,  0.0f,
    // 229-234: ψ(4040) ψ(4160) ψ(4230) ψ(4360) ψ(4415) ψ(4660)
     0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,
    // 235-236: ηb(1S) Υ(1S)
     0.0f,  0.0f,
    // 237-240: χb0(1P) χb1(1P) hb(1P) χb2(1P)
     0.0f,  0.0f,  0.0f,  0.0f,
    // 241-242: ηb(2S) Υ(2S)
     0.0f,  0.0f,
    // 243-246: χb0(2P) χb1(2P) hb(2P) χb2(2P)
     0.0f,  0.0f,  0.0f,  0.0f,
    // 247: Υ(3S)
     0.0f,
    // 248-249: χb1(3P) χb2(3P)
     0.0f,  0.0f,
    // 250-252: Υ(4S) Υ(10860) Υ(11020)
     0.0f,  0.0f,  0.0f,
    // 253-254: Zc(3900)±
     1.0f, -1.0f,
    // 255-256: Zc(4020)±
     1.0f, -1.0f,
    // 257-258: Z(4430)±
     1.0f, -1.0f,
    // 259: X(4140)
     0.0f,
    // 260-261: Zb(10610)±
     1.0f, -1.0f,
};

// Spin J quantum number
static constexpr float MESON_SPIN_J[MESON_TYPE_COUNT] = {
    // 74-86: pseudoscalar (J=0)
    0,0,0,0,0, 0,0,0,0,0, 0,0,0,
    // 87-100: vector (J=1)
    1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,
    // 101-111: scalar (J=0)
    0,0,0,0,0, 0,0,0,0,0, 0,
    // 112-124: axial-vector (J=1)
    1,1,1,1,1, 1,1,1,1,1, 1,1,1,
    // 125-134: tensor (J=2)
    2,2,2,2,2, 2,2,2,2,2,
    // 135-139: pseudotensor (J=2)
    2,2,2,2,2,
    // 140-145: higher spin (J=3,4)
    3,3,3,3,3,4,
    // 146-148: a₄⁰⁻(J=4) f₄(J=4)
    4,4,4,
    // 149-152: K pseudoscalar (J=0)
    0,0,0,0,
    // 153-156: K* vector (J=1)
    1,1,1,1,
    // 157-158: K₀* scalar (J=0)
    0,0,
    // 159-162: K₁ axial (J=1)
    1,1,1,1,
    // 163-166: K*(1410) vector, K₀*(1430) scalar
    1,1,0,0,
    // 167-170: K₂*(J=2), K*(1680)(J=1)
    2,2,1,1,
    // 171-174: K₃*(J=3), K₄*(J=4)
    3,3,4,4,
    // 175-180: D pseudoscalar(J=0), Ds(J=0)
    0,0,0,0,0,0,
    // 181-186: D* vector(J=1), Ds*(J=1)
    1,1,1,1,1,1,
    // 187-189: D₀* scalar(J=0), Ds₀*(J=0)
    0,0,0,
    // 190-193: D₁ axial(J=1), Ds₁(J=1)
    1,1,1,1,
    // 194-196: D₂* tensor(J=2), Ds₂*(J=2)
    2,2,2,
    // 197-202: B pseudoscalar(J=0), Bs(J=0)
    0,0,0,0,0,0,
    // 203-204: Bc(J=0)
    0,0,
    // 205-210: B* vector(J=1), Bs*(J=1)
    1,1,1,1,1,1,
    // 211-216: B₁ axial(J=1), B₂* tensor(J=2), Bs₁(J=1), Bs₂*(J=2)
    1,1,2,2,1,2,
    // 217: ηc(1S) J=0
    0,
    // 218: J/ψ J=1
    1,
    // 219-222: χc0(J=0) χc1(J=1) hc(J=1) χc2(J=2)
    0,1,1,2,
    // 223: ηc(2S) J=0
    0,
    // 224-225: ψ(2S,3770) J=1
    1,1,
    // 226-228: X(3872)(J=1) χc0(2P)(J=0) χc2(3930)(J=2)
    1,0,2,
    // 229-234: ψ(4040-4660) J=1
    1,1,1,1,1,1,
    // 235: ηb(1S) J=0
    0,
    // 236: Υ(1S) J=1
    1,
    // 237-240: χb0(J=0) χb1(J=1) hb(J=1) χb2(J=2)
    0,1,1,2,
    // 241: ηb(2S) J=0
    0,
    // 242: Υ(2S) J=1
    1,
    // 243-246: χb0(2P)(J=0) χb1(2P)(J=1) hb(2P)(J=1) χb2(2P)(J=2)
    0,1,1,2,
    // 247: Υ(3S) J=1
    1,
    // 248-249: χb1(3P)(J=1) χb2(3P)(J=2)
    1,2,
    // 250-252: Υ(4S,10860,11020) J=1
    1,1,1,
    // 253-261: exotics — Zc(J=1), Z(4430)(J=1), X(4140)(J=1), Zb(J=1)
    1,1,1,1,1,1,1,1,1,
};

// PDG rest mass in MeV/c² (2024 Review of Particle Physics)
static constexpr float MESON_MASS_MEV[MESON_TYPE_COUNT] = {
    // 74-78: π⁺ π⁰ π⁻ η η'
    139.570f, 134.977f, 139.570f, 547.862f, 957.780f,
    // 79-86: π(1300)±⁰ η(1295) η(1475) π(1800)±⁰
    1300.0f, 1300.0f, 1300.0f, 1294.0f, 1476.0f, 1812.0f, 1812.0f, 1812.0f,
    // 87-89: ρ(770)±⁰
    775.26f, 775.26f, 775.26f,
    // 90-91: ω(782) φ(1020)
    782.66f, 1019.46f,
    // 92-94: ρ(1450)±⁰
    1465.0f, 1465.0f, 1465.0f,
    // 95-96: ω(1420) φ(1680)
    1410.0f, 1680.0f,
    // 97-99: ρ(1700)±⁰
    1720.0f, 1720.0f, 1720.0f,
    // 100: ω(1650)
    1670.0f,
    // 101: f₀(500)
    475.0f,
    // 102-104: a₀(980)±⁰
    980.0f, 980.0f, 980.0f,
    // 105: f₀(980)
    990.0f,
    // 106: f₀(1370)
    1350.0f,
    // 107-109: a₀(1450)±⁰
    1474.0f, 1474.0f, 1474.0f,
    // 110-111: f₀(1500) f₀(1710)
    1506.0f, 1704.0f,
    // 112-114: b₁(1235)±⁰
    1229.5f, 1229.5f, 1229.5f,
    // 115: h₁(1170)
    1166.0f,
    // 116-118: a₁(1260)±⁰
    1230.0f, 1230.0f, 1230.0f,
    // 119-121: f₁(1285) f₁(1420) h₁(1415)
    1281.9f, 1426.3f, 1416.0f,
    // 122-124: a₁(1640)±⁰
    1655.0f, 1655.0f, 1655.0f,
    // 125-127: a₂(1320)±⁰
    1318.2f, 1318.2f, 1318.2f,
    // 128-129: f₂(1270) f₂'(1525)
    1275.5f, 1525.0f,
    // 130-132: a₂(1700)±⁰
    1732.0f, 1732.0f, 1732.0f,
    // 133-134: f₂(1950) f₂(2010)
    1936.0f, 2011.0f,
    // 135-137: π₂(1670)±⁰
    1670.6f, 1670.6f, 1670.6f,
    // 138-139: η₂(1645) η₂(1870)
    1617.0f, 1842.0f,
    // 140-142: ρ₃(1690)±⁰
    1688.8f, 1688.8f, 1688.8f,
    // 143-144: ω₃(1670) φ₃(1850)
    1667.0f, 1854.0f,
    // 145-148: a₄(2040)±⁰ f₄(2050)
    1996.0f, 1996.0f, 1996.0f, 2018.0f,
    // 149-152: K⁺ K⁰ K⁻ K̄⁰
    493.677f, 497.611f, 493.677f, 497.611f,
    // 153-156: K*(892)⁺⁰⁻ K̄*⁰
    891.67f, 895.55f, 891.67f, 895.55f,
    // 157-158: K₀*(700)
    845.0f, 845.0f,
    // 159-162: K₁(1270)⁺⁰ K₁(1400)⁺⁰
    1253.0f, 1253.0f, 1403.0f, 1403.0f,
    // 163-166: K*(1410)⁺⁰ K₀*(1430)⁺⁰
    1414.0f, 1414.0f, 1425.0f, 1425.0f,
    // 167-170: K₂*(1430)⁺⁰ K*(1680)⁺⁰
    1425.6f, 1432.4f, 1718.0f, 1718.0f,
    // 171-174: K₃*(1780)⁺⁰ K₄*(2045)⁺⁰
    1776.0f, 1776.0f, 2045.0f, 2045.0f,
    // 175-178: D⁺ D⁰ D⁻ D̄⁰
    1869.66f, 1864.84f, 1869.66f, 1864.84f,
    // 179-180: Ds⁺ Ds⁻
    1968.35f, 1968.35f,
    // 181-184: D*(2007)⁰ D*(2010)⁺⁻ D̄*⁰
    2006.85f, 2010.26f, 2010.26f, 2006.85f,
    // 185-186: Ds*⁺ Ds*⁻
    2112.2f, 2112.2f,
    // 187-188: D₀*(2300)⁰⁺
    2343.0f, 2349.0f,
    // 189: Ds₀*(2317)⁺
    2317.8f,
    // 190-191: D₁(2420)⁰⁺
    2420.8f, 2423.2f,
    // 192-193: Ds₁(2460)⁺ Ds₁(2536)⁺
    2459.5f, 2535.1f,
    // 194-195: D₂*(2460)⁰⁺
    2461.1f, 2465.4f,
    // 196: Ds₂*(2573)⁺
    2569.1f,
    // 197-200: B⁺ B⁰ B⁻ B̄⁰
    5279.34f, 5279.66f, 5279.34f, 5279.66f,
    // 201-202: Bs⁰ B̄s⁰
    5366.88f, 5366.88f,
    // 203-204: Bc⁺ Bc⁻
    6274.9f, 6274.9f,
    // 205-208: B*⁺⁰⁻ B̄*⁰
    5324.70f, 5324.65f, 5324.70f, 5324.65f,
    // 209-210: Bs*⁰ B̄s*⁰
    5415.4f, 5415.4f,
    // 211-214: B₁(5721)⁰⁺ B₂*(5747)⁰⁺
    5726.0f, 5725.9f, 5739.5f, 5737.2f,
    // 215-216: Bs₁(5830)⁰ Bs₂*(5840)⁰
    5828.7f, 5839.85f,
    // 217-218: ηc(1S) J/ψ
    2983.9f, 3096.9f,
    // 219-222: χc0(1P) χc1(1P) hc(1P) χc2(1P)
    3414.71f, 3510.67f, 3525.38f, 3556.17f,
    // 223-225: ηc(2S) ψ(2S) ψ(3770)
    3637.5f, 3686.10f, 3773.7f,
    // 226-228: X(3872) χc0(2P) χc2(3930)
    3871.65f, 3862.0f, 3922.5f,
    // 229-234: ψ(4040) ψ(4160) ψ(4230) ψ(4360) ψ(4415) ψ(4660)
    4039.0f, 4191.0f, 4222.5f, 4374.0f, 4421.0f, 4630.0f,
    // 235-236: ηb(1S) Υ(1S)
    9399.0f, 9460.30f,
    // 237-240: χb0(1P) χb1(1P) hb(1P) χb2(1P)
    9859.44f, 9892.78f, 9899.3f, 9912.21f,
    // 241-242: ηb(2S) Υ(2S)
    9999.0f, 10023.26f,
    // 243-246: χb0(2P) χb1(2P) hb(2P) χb2(2P)
    10232.5f, 10255.46f, 10259.8f, 10268.65f,
    // 247: Υ(3S)
    10355.2f,
    // 248-249: χb1(3P) χb2(3P)
    10513.42f, 10524.02f,
    // 250-252: Υ(4S) Υ(10860) Υ(11020)
    10579.4f, 10885.2f, 11000.0f,
    // 253-254: Zc(3900)±
    3887.1f, 3887.1f,
    // 255-256: Zc(4020)±
    4024.1f, 4024.1f,
    // 257-258: Z(4430)±
    4478.0f, 4478.0f,
    // 259: X(4140)
    4146.5f,
    // 260-261: Zb(10610)±
    10607.2f, 10607.2f,
};

// Simulation decay rate (scaled from PDG width Γ)
// Long-lived (π±,K±,D,B): 0.001; Medium (η,ω,φ): 0.01-0.05; Broad (ρ,f₀): 0.10-0.30; Instant: 0.40-0.50
static constexpr float MESON_DECAY_RATE[MESON_TYPE_COUNT] = {
    // 74-78: π⁺(long) π⁰(fast γγ) π⁻(long) η(medium) η'(medium)
    0.001f, 0.05f, 0.001f, 0.03f, 0.04f,
    // 79-86: π(1300)(broad) η(1295)(broad) η(1475)(broad) π(1800)(broad)
    0.20f, 0.20f, 0.20f, 0.15f, 0.15f, 0.25f, 0.25f, 0.25f,
    // 87-89: ρ(770) (very broad, Γ≈150MeV)
    0.25f, 0.25f, 0.25f,
    // 90-91: ω(782)(narrow) φ(1020)(narrow)
    0.02f, 0.02f,
    // 92-94: ρ(1450)(broad)
    0.20f, 0.20f, 0.20f,
    // 95-96: ω(1420)(broad) φ(1680)(broad)
    0.15f, 0.15f,
    // 97-99: ρ(1700)(broad)
    0.20f, 0.20f, 0.20f,
    // 100: ω(1650)(broad)
    0.15f,
    // 101: f₀(500)(very broad)
    0.40f,
    // 102-104: a₀(980)(medium-broad)
    0.10f, 0.10f, 0.10f,
    // 105: f₀(980)(medium)
    0.08f,
    // 106: f₀(1370)(broad)
    0.20f,
    // 107-109: a₀(1450)(broad)
    0.15f, 0.15f, 0.15f,
    // 110-111: f₀(1500)(medium) f₀(1710)(medium)
    0.10f, 0.10f,
    // 112-114: b₁(1235)(medium)
    0.10f, 0.10f, 0.10f,
    // 115: h₁(1170)(broad)
    0.20f,
    // 116-118: a₁(1260)(very broad)
    0.30f, 0.30f, 0.30f,
    // 119-121: f₁(1285)(narrow) f₁(1420)(narrow) h₁(1415)(medium)
    0.03f, 0.04f, 0.10f,
    // 122-124: a₁(1640)(broad)
    0.20f, 0.20f, 0.20f,
    // 125-127: a₂(1320)(medium)
    0.08f, 0.08f, 0.08f,
    // 128-129: f₂(1270)(medium) f₂'(1525)(narrow)
    0.08f, 0.04f,
    // 130-132: a₂(1700)(broad)
    0.15f, 0.15f, 0.15f,
    // 133-134: f₂(1950)(broad) f₂(2010)(broad)
    0.20f, 0.20f,
    // 135-137: π₂(1670)(medium)
    0.10f, 0.10f, 0.10f,
    // 138-139: η₂(1645)(medium) η₂(1870)(medium)
    0.10f, 0.10f,
    // 140-142: ρ₃(1690)(medium)
    0.10f, 0.10f, 0.10f,
    // 143-144: ω₃(1670)(medium) φ₃(1850)(medium)
    0.08f, 0.06f,
    // 145-148: a₄(2040)(medium) f₄(2050)(medium)
    0.10f, 0.10f, 0.10f, 0.08f,
    // 149-152: K±(long-lived) K⁰(medium via KS/KL)
    0.001f, 0.005f, 0.001f, 0.005f,
    // 153-156: K*(892)(broad, Γ≈50MeV)
    0.15f, 0.15f, 0.15f, 0.15f,
    // 157-158: K₀*(700)(very broad)
    0.35f, 0.35f,
    // 159-162: K₁(1270)(medium) K₁(1400)(medium)
    0.08f, 0.08f, 0.12f, 0.12f,
    // 163-166: K*(1410)(broad) K₀*(1430)(broad)
    0.15f, 0.15f, 0.18f, 0.18f,
    // 167-170: K₂*(1430)(medium) K*(1680)(broad)
    0.08f, 0.08f, 0.15f, 0.15f,
    // 171-174: K₃*(1780)(medium) K₄*(2045)(medium)
    0.10f, 0.10f, 0.10f, 0.10f,
    // 175-178: D±(long) D⁰(long) D⁻ D̄⁰
    0.001f, 0.001f, 0.001f, 0.001f,
    // 179-180: Ds±(long)
    0.001f, 0.001f,
    // 181-186: D* (narrow, but strong decay allowed)
    0.05f, 0.05f, 0.05f, 0.05f, 0.04f, 0.04f,
    // 187-189: D₀*(broad) Ds₀*(narrow)
    0.20f, 0.20f, 0.05f,
    // 190-193: D₁(medium) Ds₁(narrow)
    0.10f, 0.10f, 0.08f, 0.02f,
    // 194-196: D₂*(medium) Ds₂*(narrow)
    0.08f, 0.08f, 0.03f,
    // 197-200: B±(long) B⁰(long)
    0.001f, 0.001f, 0.001f, 0.001f,
    // 201-202: Bs⁰(long)
    0.001f, 0.001f,
    // 203-204: Bc±(long)
    0.002f, 0.002f,
    // 205-210: B*(EM decay, very narrow)
    0.03f, 0.03f, 0.03f, 0.03f, 0.03f, 0.03f,
    // 211-216: B₁/B₂*/Bs₁/Bs₂*(medium)
    0.08f, 0.08f, 0.06f, 0.06f, 0.04f, 0.04f,
    // 217-218: ηc(1S)(medium Γ≈32MeV) J/ψ(narrow Γ≈93keV)
    0.05f, 0.01f,
    // 219-222: χc states(medium)
    0.05f, 0.03f, 0.04f, 0.04f,
    // 223-225: ηc(2S)(medium) ψ(2S)(narrow) ψ(3770)(medium)
    0.06f, 0.01f, 0.08f,
    // 226-228: X(3872)(very narrow) χc0(2P) χc2(3930)
    0.005f, 0.08f, 0.06f,
    // 229-234: ψ(4040-4660)(medium-broad)
    0.10f, 0.08f, 0.08f, 0.10f, 0.08f, 0.10f,
    // 235-236: ηb(1S)(medium) Υ(1S)(narrow Γ≈54keV)
    0.05f, 0.01f,
    // 237-240: χb states(medium)
    0.04f, 0.03f, 0.04f, 0.04f,
    // 241-242: ηb(2S) Υ(2S)(narrow)
    0.05f, 0.01f,
    // 243-246: χb(2P) states
    0.04f, 0.03f, 0.04f, 0.04f,
    // 247: Υ(3S)(narrow)
    0.01f,
    // 248-249: χb(3P)
    0.04f, 0.04f,
    // 250-252: Υ(4S)(broad) Υ(10860)(broad) Υ(11020)(broad)
    0.15f, 0.12f, 0.12f,
    // 253-261: exotic states (medium-broad)
    0.10f, 0.10f, 0.08f, 0.08f, 0.15f, 0.15f, 0.06f, 0.08f, 0.08f,
};

// Display names (UTF-8)
static const char* const MESON_NAMES[MESON_TYPE_COUNT] = {
    // Light pseudoscalar (74-86)
    "Pion+", "Pion0", "Pion-", "Eta", "Eta'(958)",
    "Pi(1300)+", "Pi(1300)0", "Pi(1300)-", "Eta(1295)", "Eta(1475)",
    "Pi(1800)+", "Pi(1800)0", "Pi(1800)-",
    // Light vector (87-100)
    "Rho(770)+", "Rho(770)0", "Rho(770)-", "Omega(782)", "Phi(1020)",
    "Rho(1450)+", "Rho(1450)0", "Rho(1450)-", "Omega(1420)", "Phi(1680)",
    "Rho(1700)+", "Rho(1700)0", "Rho(1700)-", "Omega(1650)",
    // Light scalar (101-111)
    "f0(500)", "a0(980)+", "a0(980)0", "a0(980)-", "f0(980)",
    "f0(1370)", "a0(1450)+", "a0(1450)0", "a0(1450)-", "f0(1500)", "f0(1710)",
    // Light axial-vector (112-124)
    "b1(1235)+", "b1(1235)0", "b1(1235)-", "h1(1170)",
    "a1(1260)+", "a1(1260)0", "a1(1260)-", "f1(1285)", "f1(1420)", "h1(1415)",
    "a1(1640)+", "a1(1640)0", "a1(1640)-",
    // Light tensor (125-134)
    "a2(1320)+", "a2(1320)0", "a2(1320)-", "f2(1270)", "f2'(1525)",
    "a2(1700)+", "a2(1700)0", "a2(1700)-", "f2(1950)", "f2(2010)",
    // Light pseudotensor (135-139)
    "Pi2(1670)+", "Pi2(1670)0", "Pi2(1670)-", "Eta2(1645)", "Eta2(1870)",
    // Light higher spin (140-148)
    "Rho3(1690)+", "Rho3(1690)0", "Rho3(1690)-", "Omega3(1670)", "Phi3(1850)",
    "a4(2040)+", "a4(2040)0", "a4(2040)-", "f4(2050)",
    // Strange (149-174)
    "K+", "K0", "K-", "K0bar",
    "K*(892)+", "K*(892)0", "K*(892)-", "K*(892)0bar",
    "K0*(700)+", "K0*(700)0",
    "K1(1270)+", "K1(1270)0", "K1(1400)+", "K1(1400)0",
    "K*(1410)+", "K*(1410)0", "K0*(1430)+", "K0*(1430)0",
    "K2*(1430)+", "K2*(1430)0", "K*(1680)+", "K*(1680)0",
    "K3*(1780)+", "K3*(1780)0", "K4*(2045)+", "K4*(2045)0",
    // Charmed (175-196)
    "D+", "D0", "D-", "D0bar", "Ds+", "Ds-",
    "D*(2007)0", "D*(2010)+", "D*(2010)-", "D*(2007)0bar", "Ds*+", "Ds*-",
    "D0*(2300)0", "D0*(2300)+", "Ds0*(2317)+",
    "D1(2420)0", "D1(2420)+", "Ds1(2460)+", "Ds1(2536)+",
    "D2*(2460)0", "D2*(2460)+", "Ds2*(2573)+",
    // Bottom (197-216)
    "B+", "B0", "B-", "B0bar", "Bs0", "Bs0bar", "Bc+", "Bc-",
    "B*+", "B*0", "B*-", "B*0bar", "Bs*0", "Bs*0bar",
    "B1(5721)0", "B1(5721)+", "B2*(5747)0", "B2*(5747)+",
    "Bs1(5830)0", "Bs2*(5840)0",
    // Charmonium (217-234)
    "Eta_c(1S)", "J/Psi",
    "Chi_c0(1P)", "Chi_c1(1P)", "h_c(1P)", "Chi_c2(1P)",
    "Eta_c(2S)", "Psi(2S)", "Psi(3770)",
    "X(3872)", "Chi_c0(2P)", "Chi_c2(3930)",
    "Psi(4040)", "Psi(4160)", "Psi(4230)", "Psi(4360)", "Psi(4415)", "Psi(4660)",
    // Bottomonium (235-252)
    "Eta_b(1S)", "Upsilon(1S)",
    "Chi_b0(1P)", "Chi_b1(1P)", "h_b(1P)", "Chi_b2(1P)",
    "Eta_b(2S)", "Upsilon(2S)",
    "Chi_b0(2P)", "Chi_b1(2P)", "h_b(2P)", "Chi_b2(2P)",
    "Upsilon(3S)", "Chi_b1(3P)", "Chi_b2(3P)",
    "Upsilon(4S)", "Upsilon(10860)", "Upsilon(11020)",
    // Exotic (253-261)
    "Zc(3900)+", "Zc(3900)-", "Zc(4020)+", "Zc(4020)-",
    "Z(4430)+", "Z(4430)-", "X(4140)",
    "Zb(10610)+", "Zb(10610)-",
};

// Short labels for UI (max 4 chars)
static const char* const MESON_LABELS[MESON_TYPE_COUNT] = {
    // Light pseudoscalar (74-86)
    "pi+", "pi0", "pi-", "eta", "et'",
    "P13+", "P130", "P13-", "e129", "e147", "P18+", "P180", "P18-",
    // Light vector (87-100)
    "rh+", "rh0", "rh-", "omg", "phi",
    "R14+", "R140", "R14-", "o142", "p168", "R17+", "R170", "R17-", "o165",
    // Light scalar (101-111)
    "f050", "a0+", "a00", "a0-", "f098",
    "f137", "A14+", "A140", "A14-", "f150", "f171",
    // Light axial-vector (112-124)
    "b1+", "b10", "b1-", "h117",
    "a1+", "a10", "a1-", "f128", "f142", "h141",
    "A16+", "A160", "A16-",
    // Light tensor (125-134)
    "a2+", "a20", "a2-", "f127", "f152",
    "A17+", "A170", "A17-", "f195", "f201",
    // Light pseudotensor (135-139)
    "P2+", "P20", "P2-", "e164", "e187",
    // Light higher spin (140-148)
    "R3+", "R30", "R3-", "o3", "p3", "a4+", "a40", "a4-", "f4",
    // Strange (149-174)
    "K+", "K0", "K-", "Kb0",
    "K*+", "K*0", "K*-", "Kb*",
    "Ks+", "Ks0",
    "K1+", "K10", "K14+", "K140",
    "KA+", "KA0", "KB+", "KB0",
    "K2+", "K20", "KD+", "KD0",
    "K3+", "K30", "K4+", "K40",
    // Charmed (175-196)
    "D+", "D0", "D-", "Db0", "Ds+", "Ds-",
    "D*0", "D*+", "D*-", "Db*", "Ds*+", "Ds*-",
    "D00", "D0+", "Ds0+",
    "D10", "D1+", "Ds1", "Ds15",
    "D20", "D2+", "Ds2",
    // Bottom (197-216)
    "B+", "B0", "B-", "Bb0", "Bs", "Bsb", "Bc+", "Bc-",
    "B*+", "B*0", "B*-", "Bb*", "Bs*", "Bsb*",
    "B10", "B1+", "B20", "B2+", "Bs1", "Bs2",
    // Charmonium (217-234)
    "etc", "J/P",
    "Xc0", "Xc1", "hc", "Xc2",
    "et2", "P2S", "P37",
    "X38", "Xc02", "Xc23",
    "P40", "P41", "P42", "P43", "P44", "P46",
    // Bottomonium (235-252)
    "etb", "Y1S",
    "Xb0", "Xb1", "hb", "Xb2",
    "eb2", "Y2S",
    "XB0", "XB1", "hB2", "XB2",
    "Y3S", "Xb13", "Xb23",
    "Y4S", "Y10", "Y11",
    // Exotic (253-261)
    "Zc+", "Zc-", "Z4+", "Z4-", "Z44+", "Z44-", "X41", "Zb+", "Zb-",
};

// Quark content: {quark_type, antiquark_type} using phys_particles.h constants
// UP_QUARK_TYPE=13, DOWN=14, STRANGE=15, CHARM=16, TOP=17, BOTTOM=18
// ANTI_UP=19, ANTI_DOWN=20, ANTI_STRANGE=21, ANTI_CHARM=22, ANTI_TOP=23, ANTI_BOTTOM=24
static constexpr uint32_t MESON_QUARK_CONTENT[MESON_TYPE_COUNT][2] = {
    // Light pseudoscalar (74-86)
    {13,20}, {13,19}, {14,19}, {13,19}, {15,21},  // π⁺(ud̄) π⁰(uū) π⁻(dū) η(uū-mix) η'(ss̄-mix)
    {13,20}, {13,19}, {14,19}, {13,19}, {15,21},  // π(1300)±⁰ η(1295) η(1475)
    {13,20}, {13,19}, {14,19},                     // π(1800)±⁰
    // Light vector (87-100)
    {13,20}, {13,19}, {14,19}, {13,19}, {15,21},  // ρ±⁰ ω φ
    {13,20}, {13,19}, {14,19}, {13,19}, {15,21},  // ρ(1450) ω(1420) φ(1680)
    {13,20}, {13,19}, {14,19}, {13,19},            // ρ(1700) ω(1650)
    // Light scalar (101-111)
    {13,19}, {13,20}, {13,19}, {14,19}, {13,19},  // f₀(500) a₀(980)±⁰ f₀(980)
    {13,19}, {13,20}, {13,19}, {14,19}, {13,19}, {15,21}, // f₀(1370) a₀(1450) f₀(1500) f₀(1710)
    // Light axial-vector (112-124)
    {13,20}, {13,19}, {14,19}, {13,19},            // b₁(1235) h₁(1170)
    {13,20}, {13,19}, {14,19}, {13,19}, {15,21}, {15,21}, // a₁(1260) f₁(1285) f₁(1420) h₁(1415)
    {13,20}, {13,19}, {14,19},                     // a₁(1640)
    // Light tensor (125-134)
    {13,20}, {13,19}, {14,19}, {13,19}, {15,21},  // a₂(1320) f₂(1270) f₂'(1525)
    {13,20}, {13,19}, {14,19}, {13,19}, {13,19},  // a₂(1700) f₂(1950) f₂(2010)
    // Light pseudotensor (135-139)
    {13,20}, {13,19}, {14,19}, {13,19}, {13,19},  // π₂(1670) η₂(1645) η₂(1870)
    // Light higher spin (140-148)
    {13,20}, {13,19}, {14,19}, {13,19}, {15,21},  // ρ₃(1690) ω₃(1670) φ₃(1850)
    {13,20}, {13,19}, {14,19}, {13,19},            // a₄(2040) f₄(2050)
    // Strange (149-174)
    {13,21}, {14,21}, {15,19}, {15,20},            // K⁺(us̄) K⁰(ds̄) K⁻(sū) K̄⁰(sd̄)
    {13,21}, {14,21}, {15,19}, {15,20},            // K*(892)
    {13,21}, {14,21},                               // K₀*(700)
    {13,21}, {14,21}, {13,21}, {14,21},            // K₁(1270) K₁(1400)
    {13,21}, {14,21}, {13,21}, {14,21},            // K*(1410) K₀*(1430)
    {13,21}, {14,21}, {13,21}, {14,21},            // K₂*(1430) K*(1680)
    {13,21}, {14,21}, {13,21}, {14,21},            // K₃*(1780) K₄*(2045)
    // Charmed (175-196)
    {16,20}, {16,19}, {14,22}, {13,22}, {16,21}, {15,22},  // D⁺(cd̄) D⁰(cū) D⁻(dc̄) D̄⁰(uc̄) Ds⁺(cs̄) Ds⁻(sc̄)
    {16,19}, {16,20}, {14,22}, {13,22}, {16,21}, {15,22},  // D* Ds*
    {16,19}, {16,20}, {16,21},                               // D₀* Ds₀*
    {16,19}, {16,20}, {16,21}, {16,21},                     // D₁ Ds₁
    {16,19}, {16,20}, {16,21},                               // D₂* Ds₂*
    // Bottom (197-216)
    {13,24}, {14,24}, {18,19}, {18,20}, {15,24}, {18,21}, {16,24}, {18,22}, // B⁺(ub̄) B⁰(db̄) B⁻(bū) B̄⁰(bd̄) Bs⁰(sb̄) B̄s⁰(bs̄) Bc⁺(cb̄) Bc⁻(bc̄)
    {13,24}, {14,24}, {18,19}, {18,20}, {15,24}, {18,21}, // B* Bs*
    {14,24}, {13,24}, {14,24}, {13,24}, {15,24}, {15,24}, // B₁ B₂* Bs₁ Bs₂*
    // Charmonium (217-234)
    {16,22}, {16,22},                               // ηc J/ψ
    {16,22}, {16,22}, {16,22}, {16,22},            // χc0 χc1 hc χc2
    {16,22}, {16,22}, {16,22},                     // ηc(2S) ψ(2S) ψ(3770)
    {16,22}, {16,22}, {16,22},                     // X(3872) χc0(2P) χc2(3930)
    {16,22}, {16,22}, {16,22}, {16,22}, {16,22}, {16,22}, // ψ states
    // Bottomonium (235-252)
    {18,24}, {18,24},                               // ηb Υ(1S)
    {18,24}, {18,24}, {18,24}, {18,24},            // χb0/1 hb χb2 (1P)
    {18,24}, {18,24},                               // ηb(2S) Υ(2S)
    {18,24}, {18,24}, {18,24}, {18,24},            // χb (2P)
    {18,24},                                         // Υ(3S)
    {18,24}, {18,24},                               // χb (3P)
    {18,24}, {18,24}, {18,24},                     // Υ(4S,10860,11020)
    // Exotic (253-261) — tetraquark/molecular candidates
    {16,22}, {16,22}, {16,22}, {16,22},            // Zc (cc̄ + light quarks)
    {16,22}, {16,22}, {16,22},                     // Z(4430) X(4140)
    {18,24}, {18,24},                               // Zb (bb̄ + light quarks)
};

// Render colors (family-coded)
static const glm::vec4 MESON_RENDER_COLORS[MESON_TYPE_COUNT] = {
    // Light pseudoscalar (74-86): warm yellow/orange
    {1.0f,0.85f,0.2f,1}, {1.0f,0.9f,0.3f,1}, {1.0f,0.85f,0.2f,1}, {0.95f,0.8f,0.15f,1}, {0.9f,0.75f,0.1f,1},
    {0.85f,0.7f,0.15f,1}, {0.85f,0.7f,0.15f,1}, {0.85f,0.7f,0.15f,1}, {0.9f,0.75f,0.1f,1}, {0.9f,0.75f,0.1f,1},
    {0.8f,0.65f,0.1f,1}, {0.8f,0.65f,0.1f,1}, {0.8f,0.65f,0.1f,1},
    // Light vector (87-100): red/crimson
    {0.95f,0.2f,0.15f,1}, {0.95f,0.25f,0.2f,1}, {0.95f,0.2f,0.15f,1}, {0.85f,0.15f,0.1f,1}, {0.9f,0.1f,0.3f,1},
    {0.8f,0.15f,0.12f,1}, {0.8f,0.15f,0.12f,1}, {0.8f,0.15f,0.12f,1}, {0.75f,0.12f,0.08f,1}, {0.85f,0.08f,0.25f,1},
    {0.7f,0.12f,0.1f,1}, {0.7f,0.12f,0.1f,1}, {0.7f,0.12f,0.1f,1}, {0.65f,0.1f,0.07f,1},
    // Light scalar (101-111): pink
    {1.0f,0.5f,0.6f,1}, {1.0f,0.45f,0.55f,1}, {1.0f,0.5f,0.6f,1}, {1.0f,0.45f,0.55f,1}, {0.95f,0.5f,0.6f,1},
    {0.9f,0.4f,0.5f,1}, {0.9f,0.4f,0.5f,1}, {0.9f,0.45f,0.55f,1}, {0.9f,0.4f,0.5f,1}, {0.85f,0.38f,0.48f,1}, {0.85f,0.35f,0.45f,1},
    // Light axial-vector (112-124): purple
    {0.7f,0.3f,0.9f,1}, {0.7f,0.35f,0.9f,1}, {0.7f,0.3f,0.9f,1}, {0.65f,0.25f,0.85f,1},
    {0.75f,0.3f,0.95f,1}, {0.75f,0.35f,0.95f,1}, {0.75f,0.3f,0.95f,1}, {0.6f,0.25f,0.85f,1}, {0.6f,0.2f,0.8f,1}, {0.55f,0.2f,0.8f,1},
    {0.65f,0.25f,0.85f,1}, {0.65f,0.3f,0.85f,1}, {0.65f,0.25f,0.85f,1},
    // Light tensor (125-134): deeper purple
    {0.55f,0.2f,0.8f,1}, {0.55f,0.25f,0.8f,1}, {0.55f,0.2f,0.8f,1}, {0.5f,0.18f,0.75f,1}, {0.5f,0.15f,0.7f,1},
    {0.48f,0.16f,0.72f,1}, {0.48f,0.18f,0.72f,1}, {0.48f,0.16f,0.72f,1}, {0.45f,0.14f,0.68f,1}, {0.45f,0.14f,0.68f,1},
    // Light pseudotensor (135-139)
    {0.6f,0.15f,0.7f,1}, {0.6f,0.18f,0.7f,1}, {0.6f,0.15f,0.7f,1}, {0.55f,0.12f,0.65f,1}, {0.55f,0.12f,0.65f,1},
    // Light higher spin (140-148)
    {0.5f,0.1f,0.65f,1}, {0.5f,0.12f,0.65f,1}, {0.5f,0.1f,0.65f,1}, {0.45f,0.08f,0.6f,1}, {0.45f,0.08f,0.6f,1},
    {0.4f,0.06f,0.55f,1}, {0.4f,0.08f,0.55f,1}, {0.4f,0.06f,0.55f,1}, {0.38f,0.06f,0.52f,1},
    // Strange (149-174): green shades
    {0.2f,0.9f,0.3f,1}, {0.25f,0.85f,0.3f,1}, {0.2f,0.9f,0.3f,1}, {0.25f,0.85f,0.3f,1},
    {0.15f,0.8f,0.25f,1}, {0.18f,0.78f,0.25f,1}, {0.15f,0.8f,0.25f,1}, {0.18f,0.78f,0.25f,1},
    {0.25f,0.75f,0.2f,1}, {0.25f,0.75f,0.2f,1},
    {0.2f,0.7f,0.18f,1}, {0.2f,0.7f,0.18f,1}, {0.18f,0.65f,0.16f,1}, {0.18f,0.65f,0.16f,1},
    {0.15f,0.7f,0.15f,1}, {0.15f,0.7f,0.15f,1}, {0.2f,0.68f,0.18f,1}, {0.2f,0.68f,0.18f,1},
    {0.12f,0.65f,0.14f,1}, {0.12f,0.65f,0.14f,1}, {0.15f,0.62f,0.12f,1}, {0.15f,0.62f,0.12f,1},
    {0.1f,0.6f,0.1f,1}, {0.1f,0.6f,0.1f,1}, {0.08f,0.55f,0.08f,1}, {0.08f,0.55f,0.08f,1},
    // Charmed (175-196): cyan/teal
    {0.1f,0.85f,0.85f,1}, {0.15f,0.8f,0.8f,1}, {0.1f,0.85f,0.85f,1}, {0.15f,0.8f,0.8f,1}, {0.1f,0.9f,0.8f,1}, {0.1f,0.9f,0.8f,1},
    {0.12f,0.75f,0.75f,1}, {0.08f,0.78f,0.78f,1}, {0.08f,0.78f,0.78f,1}, {0.12f,0.75f,0.75f,1}, {0.08f,0.82f,0.72f,1}, {0.08f,0.82f,0.72f,1},
    {0.15f,0.7f,0.7f,1}, {0.15f,0.7f,0.7f,1}, {0.12f,0.72f,0.65f,1},
    {0.1f,0.68f,0.68f,1}, {0.1f,0.68f,0.68f,1}, {0.08f,0.7f,0.62f,1}, {0.08f,0.7f,0.62f,1},
    {0.12f,0.65f,0.65f,1}, {0.12f,0.65f,0.65f,1}, {0.1f,0.67f,0.6f,1},
    // Bottom (197-216): blue shades
    {0.2f,0.3f,0.95f,1}, {0.25f,0.35f,0.9f,1}, {0.2f,0.3f,0.95f,1}, {0.25f,0.35f,0.9f,1}, {0.15f,0.25f,0.88f,1}, {0.15f,0.25f,0.88f,1},
    {0.18f,0.28f,0.92f,1}, {0.18f,0.28f,0.92f,1},
    {0.15f,0.25f,0.85f,1}, {0.18f,0.28f,0.82f,1}, {0.15f,0.25f,0.85f,1}, {0.18f,0.28f,0.82f,1}, {0.12f,0.2f,0.8f,1}, {0.12f,0.2f,0.8f,1},
    {0.1f,0.2f,0.78f,1}, {0.1f,0.2f,0.78f,1}, {0.08f,0.18f,0.75f,1}, {0.08f,0.18f,0.75f,1},
    {0.08f,0.15f,0.72f,1}, {0.08f,0.15f,0.72f,1},
    // Charmonium (217-234): gold
    {0.95f,0.8f,0.2f,1}, {1.0f,0.85f,0.0f,1},
    {0.9f,0.75f,0.15f,1}, {0.92f,0.78f,0.1f,1}, {0.88f,0.72f,0.12f,1}, {0.9f,0.75f,0.1f,1},
    {0.85f,0.7f,0.18f,1}, {0.95f,0.8f,0.0f,1}, {0.88f,0.72f,0.0f,1},
    {1.0f,0.9f,0.3f,1}, {0.82f,0.68f,0.15f,1}, {0.85f,0.7f,0.1f,1},
    {0.8f,0.65f,0.0f,1}, {0.78f,0.63f,0.0f,1}, {0.75f,0.6f,0.0f,1}, {0.72f,0.58f,0.0f,1}, {0.7f,0.55f,0.0f,1}, {0.68f,0.52f,0.0f,1},
    // Bottomonium (235-252): silver/steel
    {0.75f,0.75f,0.82f,1}, {0.8f,0.8f,0.9f,1},
    {0.7f,0.7f,0.78f,1}, {0.72f,0.72f,0.8f,1}, {0.68f,0.68f,0.76f,1}, {0.7f,0.7f,0.78f,1},
    {0.72f,0.72f,0.8f,1}, {0.78f,0.78f,0.88f,1},
    {0.65f,0.65f,0.74f,1}, {0.67f,0.67f,0.76f,1}, {0.63f,0.63f,0.72f,1}, {0.65f,0.65f,0.74f,1},
    {0.75f,0.75f,0.85f,1}, {0.62f,0.62f,0.72f,1}, {0.6f,0.6f,0.7f,1},
    {0.72f,0.72f,0.82f,1}, {0.68f,0.68f,0.78f,1}, {0.65f,0.65f,0.75f,1},
    // Exotic (253-261): magenta/hot pink
    {0.95f,0.15f,0.7f,1}, {0.95f,0.15f,0.7f,1}, {0.9f,0.1f,0.65f,1}, {0.9f,0.1f,0.65f,1},
    {0.85f,0.08f,0.6f,1}, {0.85f,0.08f,0.6f,1}, {0.92f,0.12f,0.68f,1},
    {0.8f,0.05f,0.55f,1}, {0.8f,0.05f,0.55f,1},
};
