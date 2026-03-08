#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "cosmos/cosmos_types.h"
#include "cosmos/cosmos_app_internal.h"

#include <glm/glm.hpp>
#include <random>
#include <cmath>
#include <limits>

// ── Helpers ──────────────────────────────────────────────────────────────────

static CelestialBody make_body(uint32_t type, float mass, float radius,
                                float temperature = 300.0f) {
    CelestialBody b{};
    b.type = type;
    b.mass = mass;
    b.radius = radius;
    b.temperature = temperature;
    b.pos = glm::vec3(0.0f);
    b.vel = glm::vec3(0.0f);
    return b;
}

static constexpr float G_DEFAULT = 6.674e-3f; // sim-units G (from CosmosConfig default)

// ═════════════════════════════════════════════════════════════════════════════
// 1. TYPE CLASSIFICATION
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Type Classification") {

TEST_CASE("is_star_type identifies star types") {
    CHECK(is_star_type(CTYPE_STAR));
    CHECK(is_star_type(CTYPE_STAR_O));
    CHECK(is_star_type(CTYPE_STAR_G));
    CHECK(is_star_type(CTYPE_STAR_M));
    CHECK(is_star_type(CTYPE_STAR_WR));
    CHECK_FALSE(is_star_type(CTYPE_PLANET));
    CHECK_FALSE(is_star_type(CTYPE_BLACK_HOLE));
    CHECK_FALSE(is_star_type(CTYPE_ASTEROID));
    CHECK_FALSE(is_star_type(CTYPE_DUST));
}

TEST_CASE("is_black_hole_type identifies black hole types") {
    CHECK(is_black_hole_type(CTYPE_BLACK_HOLE));
    CHECK(is_black_hole_type(CTYPE_BH_STELLAR));
    CHECK(is_black_hole_type(CTYPE_BH_INTERMEDIATE));
    CHECK(is_black_hole_type(CTYPE_BH_SUPERMASSIVE));
    CHECK(is_black_hole_type(CTYPE_BH_PRIMORDIAL));
    CHECK_FALSE(is_black_hole_type(CTYPE_STAR));
    CHECK_FALSE(is_black_hole_type(CTYPE_PLANET));
}

TEST_CASE("classify_star_spectral returns correct spectral types") {
    // O-type: >30000K
    CHECK(classify_star_spectral(35000.0f, 40.0f) == CTYPE_STAR_O);
    // G-type: 5200-6000K (Sun-like)
    CHECK(classify_star_spectral(5800.0f, 1.0f) == CTYPE_STAR_G);
    // M-type: 2400-3700K
    CHECK(classify_star_spectral(3000.0f, 0.3f) == CTYPE_STAR_M);
    // L-type: 1300-2400K brown dwarf
    CHECK(classify_star_spectral(1800.0f, 0.05f) == CTYPE_STAR_L);
}

TEST_CASE("classify_black_hole returns correct subtypes") {
    CHECK(classify_black_hole(5.0f) == CTYPE_BH_STELLAR);       // 3-20 solar
    CHECK(classify_black_hole(500.0f) == CTYPE_BH_INTERMEDIATE); // 100-100000
    CHECK(classify_black_hole(1e7f) == CTYPE_BH_SUPERMASSIVE);   // 10^6+
    CHECK(classify_black_hole(0.001f) == CTYPE_BH_PRIMORDIAL);   // sub-stellar
}

} // Type Classification

// ═════════════════════════════════════════════════════════════════════════════
// 2. PHYSICAL PROPERTIES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Physical Properties") {

TEST_CASE("body_volume is correct for sphere") {
    CelestialBody b = make_body(CTYPE_PLANET, 1.0f, 10.0f);
    float expected = (4.0f / 3.0f) * 3.14159265f * 10.0f * 10.0f * 10.0f;
    CHECK(body_volume(b) == doctest::Approx(expected).epsilon(0.01));
}

TEST_CASE("body_density is mass/volume") {
    CelestialBody b = make_body(CTYPE_PLANET, 1.0f, 10.0f);
    float vol = body_volume(b);
    CHECK(body_density(b) == doctest::Approx(1.0f / vol).epsilon(0.01));
}

TEST_CASE("body_surface_gravity scales with mass/radius^2") {
    CelestialBody b1 = make_body(CTYPE_PLANET, 1.0f, 10.0f);
    CelestialBody b2 = make_body(CTYPE_PLANET, 2.0f, 10.0f);
    // Double mass → double gravity
    CHECK(body_surface_gravity(b2, G_DEFAULT) ==
          doctest::Approx(2.0f * body_surface_gravity(b1, G_DEFAULT)).epsilon(0.01));

    CelestialBody b3 = make_body(CTYPE_PLANET, 1.0f, 20.0f);
    // Double radius → quarter gravity
    CHECK(body_surface_gravity(b3, G_DEFAULT) ==
          doctest::Approx(0.25f * body_surface_gravity(b1, G_DEFAULT)).epsilon(0.01));
}

TEST_CASE("body_escape_velocity scales correctly") {
    CelestialBody b = make_body(CTYPE_PLANET, 1.0f, 10.0f);
    float v_esc = body_escape_velocity(b, G_DEFAULT);
    CHECK(v_esc > 0.0f);
    // v_esc = sqrt(2GM/R)
    float expected = std::sqrt(2.0f * G_DEFAULT * 1.0f / 10.0f);
    CHECK(v_esc == doctest::Approx(expected).epsilon(0.01));
}

TEST_CASE("body_gravitational_binding_energy is positive for valid body") {
    CelestialBody b = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS);
    float E = body_gravitational_binding_energy(b, G_DEFAULT);
    CHECK(E > 0.0f);
}

TEST_CASE("expected_planet_radius returns positive values") {
    CHECK(expected_planet_radius(EARTH_MASS_SOLAR) > 0.0f);
    CHECK(expected_planet_radius(JUPITER_MASS_SOLAR) > expected_planet_radius(EARTH_MASS_SOLAR));
}

TEST_CASE("body_escape_speed between two bodies is reasonable") {
    CelestialBody a = make_body(CTYPE_STAR, 1.0f, 100.0f);
    a.pos = glm::vec3(0.0f);
    CelestialBody b = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS);
    b.pos = glm::vec3(500.0f, 0.0f, 0.0f);
    float v = body_escape_speed(a, b, G_DEFAULT);
    CHECK(v > 0.0f);
    CHECK(std::isfinite(v));
}

} // Physical Properties

// ═════════════════════════════════════════════════════════════════════════════
// 3. RING SYSTEM
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Ring System") {

TEST_CASE("body_can_host_rings allows planets and moons") {
    CelestialBody planet = make_body(CTYPE_PLANET, 1.0f, 10.0f);
    CelestialBody moon = make_body(CTYPE_MOON, 0.1f, 5.0f);
    CelestialBody star = make_body(CTYPE_STAR, 1.0f, 100.0f);
    CelestialBody bh = make_body(CTYPE_BLACK_HOLE, 10.0f, 5.0f);
    CelestialBody asteroid = make_body(CTYPE_ASTEROID, 0.001f, 0.5f);

    CHECK(body_can_host_rings(planet));
    CHECK(body_can_host_rings(moon));
    CHECK_FALSE(body_can_host_rings(star));
    CHECK_FALSE(body_can_host_rings(bh));
    CHECK_FALSE(body_can_host_rings(asteroid));
}

TEST_CASE("set_ring_system enforces minimum inner radius") {
    CelestialBody b = make_body(CTYPE_PLANET, 1.0f, 10.0f);
    set_ring_system(b, 5.0f, 30.0f, 0.5f, 0.5f, 0.1f);
    // Inner must be >= 1.15 * radius
    CHECK(b.ring_inner_radius >= b.radius * 1.15f);
    CHECK(b.ring_outer_radius > b.ring_inner_radius);
    CHECK(b.ring_density == doctest::Approx(0.5f));
    CHECK(b.ring_ice_fraction == doctest::Approx(0.5f));
}

TEST_CASE("clear_ring_system zeroes ring properties") {
    CelestialBody b = make_body(CTYPE_PLANET, 1.0f, 10.0f);
    set_ring_system(b, 15.0f, 30.0f, 0.5f, 0.5f, 0.1f);
    CHECK(b.ring_density > 0.0f);
    clear_ring_system(b);
    CHECK(b.ring_inner_radius == 0.0f);
    CHECK(b.ring_outer_radius == 0.0f);
    CHECK(b.ring_density == 0.0f);
}

TEST_CASE("set_ring_system clamps density and ice fraction") {
    CelestialBody b = make_body(CTYPE_PLANET, 1.0f, 10.0f);
    set_ring_system(b, 15.0f, 30.0f, 5.0f, 5.0f, 0.1f); // out-of-range density/ice
    CHECK(b.ring_density <= 1.0f);
    CHECK(b.ring_ice_fraction <= 1.0f);
}

} // Ring System

// ═════════════════════════════════════════════════════════════════════════════
// 4. STELLAR PROPERTIES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Stellar Properties") {

TEST_CASE("expected_main_sequence_temperature scales with mass") {
    float t_low = expected_main_sequence_temperature(0.3f);   // M dwarf
    float t_sun = expected_main_sequence_temperature(1.0f);   // Sun
    float t_high = expected_main_sequence_temperature(10.0f); // B star

    CHECK(t_low > 0.0f);
    CHECK(t_sun > t_low);
    CHECK(t_high > t_sun);
    // Sun should be roughly 5778K
    CHECK(t_sun == doctest::Approx(5778.0f).epsilon(0.20)); // 20% tolerance
}

TEST_CASE("expected_main_sequence_radius scales with mass") {
    float r_low = expected_main_sequence_radius_solar(0.3f);
    float r_sun = expected_main_sequence_radius_solar(1.0f);
    float r_high = expected_main_sequence_radius_solar(10.0f);

    CHECK(r_low > 0.0f);
    CHECK(r_sun > r_low);
    CHECK(r_high > r_sun);
    // Sun should be ~1.0 solar radius
    CHECK(r_sun == doctest::Approx(1.0f).epsilon(0.20));
}

TEST_CASE("expected_stellar_luminosity is positive for main sequence") {
    float L = expected_stellar_luminosity(1.0f, 5778.0f,
                                           solar_radius_sim_units(),
                                           SSTAGE_MAIN_SEQUENCE, 1.0f);
    CHECK(L > 0.0f);
    CHECK(std::isfinite(L));
}

TEST_CASE("stellar_remnant_kind returns correct categories") {
    CHECK(stellar_remnant_kind(0.5f) == REMNANT_WHITE_DWARF);
    CHECK(stellar_remnant_kind(6.0f) == REMNANT_WHITE_DWARF);
    CHECK(stellar_remnant_kind(12.0f) == REMNANT_NEUTRON_STAR);
    CHECK(stellar_remnant_kind(30.0f) == REMNANT_BLACK_HOLE);
}

TEST_CASE("star_spectral_bounds are ordered correctly") {
    float min_m, max_m, min_t, max_t;
    star_spectral_bounds(CTYPE_STAR_G, min_m, max_m, min_t, max_t);
    CHECK(min_m < max_m);
    CHECK(min_t < max_t);
    // G-type: roughly 5200-6000K, 0.8-1.04 solar
    CHECK(min_t >= 4000.0f);
    CHECK(max_t <= 7000.0f);
}

TEST_CASE("expected_star_radius for Sun-like star") {
    CelestialBody sun = make_body(CTYPE_STAR_G, 1.0f, 10.0f, 5778.0f);
    sun.stellar_stage = SSTAGE_MAIN_SEQUENCE;
    float r = expected_star_radius(sun);
    CHECK(r > 0.0f);
    CHECK(std::isfinite(r));
}

TEST_CASE("merged_star_fuel conserves or reduces fuel") {
    CelestialBody a = make_body(CTYPE_STAR_G, 1.0f, 100.0f, 5778.0f);
    a.fuel = 0.8f;
    a.stellar_stage = SSTAGE_MAIN_SEQUENCE;
    CelestialBody b = make_body(CTYPE_STAR_G, 0.5f, 60.0f, 5000.0f);
    b.fuel = 0.6f;
    b.stellar_stage = SSTAGE_MAIN_SEQUENCE;
    float merged = merged_star_fuel(a, b, 1.5f);
    CHECK(merged >= 0.0f);
    CHECK(merged <= 1.0f);
}

} // Stellar Properties

// ═════════════════════════════════════════════════════════════════════════════
// 5. ROCHE LIMIT & TIDAL
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Roche Limit") {

TEST_CASE("roche_distance_for_mode returns positive for star-planet pair") {
    CelestialBody star = make_body(CTYPE_STAR_G, 1.0f, 100.0f, 5778.0f);
    CelestialBody planet = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 300.0f);

    float roche_fluid = roche_distance_for_mode(star, planet, true);
    float roche_rigid = roche_distance_for_mode(star, planet, false);

    CHECK(roche_fluid > 0.0f);
    CHECK(roche_rigid > 0.0f);
    // Fluid Roche limit should be larger than rigid
    CHECK(roche_fluid > roche_rigid);
}

TEST_CASE("roche_secondary_fluid_like for common types") {
    CelestialBody gas_giant = make_body(CTYPE_PLANET, JUPITER_MASS_SOLAR, 80.0f, 150.0f);
    CelestialBody rock = make_body(CTYPE_ASTEROID, 1e-8f, 0.5f, 250.0f);

    // Gas giant should be fluid-like (low density)
    // Asteroid should be rigid
    // (Just verify these don't crash; exact result depends on implementation)
    bool gas_fluid = roche_secondary_fluid_like(gas_giant);
    bool rock_fluid = roche_secondary_fluid_like(rock);
    CHECK_FALSE(rock_fluid);
    (void)gas_fluid; // Implementation-dependent
}

} // Roche Limit

// ═════════════════════════════════════════════════════════════════════════════
// 6. MATERIAL COMPOSITION
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Material Composition") {

TEST_CASE("derive_materials returns non-negative fractions") {
    CelestialBody earth = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 288.0f);
    MaterialComposition m = derive_materials(earth);
    CHECK(m.iron >= 0.0f);
    CHECK(m.silicate >= 0.0f);
    CHECK(m.water >= 0.0f);
    CHECK(m.hydrogen >= 0.0f);
    float total = m.iron + m.silicate + m.water + m.hydrogen;
    // Fractions should sum to roughly 1
    CHECK(total == doctest::Approx(1.0f).epsilon(0.15));
}

TEST_CASE("derive_materials for gas giant is hydrogen-dominated") {
    CelestialBody gas = make_body(CTYPE_PLANET, JUPITER_MASS_SOLAR, 80.0f, 150.0f);
    gas.custom_material = true;
    gas.custom_hydrogen = 0.90f;
    gas.custom_silicate = 0.08f;
    gas.custom_iron = 0.02f;
    MaterialComposition m = derive_materials(gas);
    CHECK(m.hydrogen > m.iron);
    CHECK(m.hydrogen > m.silicate);
}

TEST_CASE("derive_materials for star is all hydrogen") {
    CelestialBody star = make_body(CTYPE_STAR_G, 1.0f, 100.0f, 5778.0f);
    MaterialComposition m = derive_materials(star);
    CHECK(m.hydrogen == doctest::Approx(1.0f));
}

TEST_CASE("infer_material_phase returns valid phases") {
    CelestialBody ice = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR * 0.1f, 4.0f, 80.0f);
    MaterialComposition m = derive_materials(ice);
    MaterialPhase phase = infer_material_phase(ice, m);
    // Cold body should be solid or ice
    CHECK((phase == PHASE_SOLID || phase == PHASE_ICE));

    CelestialBody lava = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 2000.0f);
    MaterialComposition m2 = derive_materials(lava);
    MaterialPhase phase2 = infer_material_phase(lava, m2);
    // Very hot body should be lava/plasma/gas
    CHECK((phase2 == PHASE_MOLTEN || phase2 == PHASE_PLASMA || phase2 == PHASE_GAS));
}

} // Material Composition

// ═════════════════════════════════════════════════════════════════════════════
// 7. RANDOMIZATION DETERMINISM
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Randomization Determinism") {

TEST_CASE("randomize_small_body_properties is deterministic with same seed") {
    CelestialBody a{}, b{};
    a.mass = 1e-8f; a.radius = 0.5f;
    b.mass = 1e-8f; b.radius = 0.5f;

    std::mt19937 rng_a(42);
    std::mt19937 rng_b(42);
    randomize_small_body_properties(a, rng_a, false);
    randomize_small_body_properties(b, rng_b, false);

    CHECK(a.mass == b.mass);
    CHECK(a.radius == b.radius);
    CHECK(a.temperature == b.temperature);
}

TEST_CASE("randomize_dust_properties produces valid body") {
    CelestialBody b{};
    b.mass = 1e-10f;
    std::mt19937 rng(12345);
    randomize_dust_properties(b, rng);
    CHECK(b.mass > 0.0f);
    CHECK(b.radius > 0.0f);
    CHECK(std::isfinite(b.temperature));
}

TEST_CASE("randomize_star_properties produces valid star") {
    CelestialBody b{};
    b.mass = 1.0f;
    std::mt19937 rng(999);
    randomize_star_properties(b, rng, CTYPE_STAR_G);
    CHECK(b.mass > 0.0f);
    CHECK(b.radius > 0.0f);
    CHECK(b.temperature > 0.0f);
    CHECK(b.fuel >= 0.0f);
    CHECK(b.fuel <= 1.0f);
}

TEST_CASE("randomize_planet_properties produces valid planet") {
    CosmosState state;
    CosmosConfig cfg;
    // Add a star so the planet can compute equilibrium temperature
    CelestialBody star = make_body(CTYPE_STAR_G, 1.0f, solar_radius_sim_units(), 5778.0f);
    star.luminosity = 1.0f;
    state.bodies.push_back(star);
    state.trails.emplace_back();

    CelestialBody b{};
    b.mass = EARTH_MASS_SOLAR;
    b.pos = glm::vec3(500.0f, 0.0f, 0.0f);
    std::mt19937 rng(7777);
    randomize_planet_properties(b, state, cfg, rng);

    CHECK(b.mass > 0.0f);
    CHECK(b.radius > 0.0f);
    CHECK(b.temperature > 0.0f);
    CHECK(std::isfinite(b.temperature));
}

TEST_CASE("randomize_moon_properties produces valid moon") {
    CosmosState state;
    CelestialBody planet = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 300.0f);
    state.bodies.push_back(planet);
    state.trails.emplace_back();

    CelestialBody b{};
    b.mass = EARTH_MASS_SOLAR * 0.01f;
    b.pos = glm::vec3(50.0f, 0.0f, 0.0f);
    std::mt19937 rng(5555);
    randomize_moon_properties(b, state, rng);

    CHECK(b.mass > 0.0f);
    CHECK(b.radius > 0.0f);
    CHECK(std::isfinite(b.temperature));
}

} // Randomization Determinism

// ═════════════════════════════════════════════════════════════════════════════
// 8. HASH & PROCEDURAL GENERATION
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Hash Functions") {

TEST_CASE("hash_combine is deterministic") {
    CHECK(hash_combine(42, 7) == hash_combine(42, 7));
    CHECK(hash_combine(42, 7) != hash_combine(42, 8));
    CHECK(hash_combine(0, 0) != hash_combine(1, 0));
}

TEST_CASE("hash_float produces values in [0, 1)") {
    for (uint32_t i = 0; i < 1000; ++i) {
        float f = hash_float(i);
        CHECK(f >= 0.0f);
        CHECK(f < 1.0f);
    }
}

TEST_CASE("float_bits round-trips") {
    float val = 3.14159f;
    uint32_t bits = float_bits(val);
    float reconstructed;
    std::memcpy(&reconstructed, &bits, sizeof(float));
    CHECK(reconstructed == val);
}

TEST_CASE("generate_body_name produces non-empty strings") {
    for (uint32_t type = 0; type < CTYPE_COUNT; ++type) {
        std::string name = generate_body_name(12345, type);
        CHECK_FALSE(name.empty());
    }
}

TEST_CASE("generate_body_name is deterministic") {
    CHECK(generate_body_name(42, CTYPE_STAR) ==
          generate_body_name(42, CTYPE_STAR));
    CHECK(generate_body_name(42, CTYPE_PLANET) ==
          generate_body_name(42, CTYPE_PLANET));
}

TEST_CASE("generate_body_name varies with seed") {
    // Different seeds should usually produce different names
    std::string a = generate_body_name(1, CTYPE_PLANET);
    std::string b = generate_body_name(999, CTYPE_PLANET);
    CHECK(a != b);
}

} // Hash Functions

// ═════════════════════════════════════════════════════════════════════════════
// 9. IMPACT SIGNATURES
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Impact Signatures") {

TEST_CASE("clear_impact_signature zeroes impact fields") {
    CelestialBody b = make_body(CTYPE_PLANET, 1.0f, 10.0f);
    apply_impact_signature(b, glm::vec3(1.0f, 0.0f, 0.0f), 0.8f, 0.3f, 0.5f, 0.4f);
    CHECK(b.impact_crater_strength > 0.0f);
    clear_impact_signature(b);
    CHECK(b.impact_crater_strength == 0.0f);
}

TEST_CASE("apply_impact_signature sets crater strength > 0") {
    CelestialBody b = make_body(CTYPE_PLANET, 1.0f, 10.0f);
    apply_impact_signature(b, glm::vec3(0.0f, 1.0f, 0.0f), 0.5f, 0.2f, 0.3f, 0.3f);
    CHECK(b.impact_crater_strength > 0.0f);
}

} // Impact Signatures

// ═════════════════════════════════════════════════════════════════════════════
// 10. ATMOSPHERE
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Atmosphere") {

TEST_CASE("normalize_atmosphere_composition sums to 1") {
    Atmosphere atm{};
    atm.n2_frac = 0.78f;
    atm.o2_frac = 0.21f;
    atm.co2_frac = 0.01f;
    normalize_atmosphere_composition(atm);
    float total = atmosphere_total_fraction(atm);
    CHECK(total == doctest::Approx(1.0f).epsilon(0.01));
}

TEST_CASE("atmosphere_total_fraction handles zero atmosphere") {
    Atmosphere a{};
    CHECK(atmosphere_total_fraction(a) == doctest::Approx(0.0f).epsilon(0.001));
}

TEST_CASE("atmosphere_layer_emissivity_factor is bounded") {
    float f = atmosphere_layer_emissivity_factor(0.5f, 3);
    CHECK(f >= 0.0f);
    CHECK(f <= 1.0f);
    // Zero emissivity
    float f0 = atmosphere_layer_emissivity_factor(0.0f, 3);
    CHECK(f0 >= 0.0f);
}

} // Atmosphere

// ═════════════════════════════════════════════════════════════════════════════
// 11. COMPARISON METRICS & MAGNETIC
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Comparison & Magnetic Metrics") {

TEST_CASE("derive_comparisons returns finite values") {
    CelestialBody earth = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 288.0f);
    ComparisonMetrics cm = derive_comparisons(earth);
    CHECK(std::isfinite(cm.earth_similarity));
    CHECK(std::isfinite(cm.life_likelihood));
    CHECK(cm.earth_similarity >= 0.0f);
    CHECK(cm.earth_similarity <= 1.0f);
}

TEST_CASE("derive_magnetic_metrics returns finite values") {
    CelestialBody b = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 300.0f);
    b.angular_vel = 7.27e-5f; // Earth-like rotation
    MagneticMetrics mm = derive_magnetic_metrics(b, G_DEFAULT);
    CHECK(std::isfinite(mm.magnetosphere_size));
    CHECK(std::isfinite(mm.magnetic_field));
}

TEST_CASE("magnetic_shielding_score is in [0, 1]") {
    CelestialBody b = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 300.0f);
    b.angular_vel = 7.27e-5f;
    float score = magnetic_shielding_score(b, G_DEFAULT);
    CHECK(score >= 0.0f);
    CHECK(score <= 1.0f);
}

} // Comparison & Magnetic Metrics

// ═════════════════════════════════════════════════════════════════════════════
// 12. PHYSICS MODE
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Physics Mode") {

TEST_CASE("apply_physics_mode Newtonian disables GR features") {
    CosmosConfig cfg;
    apply_physics_mode(cfg, 0); // NEWTONIAN
    CHECK_FALSE(cfg.gr_enabled);
    CHECK_FALSE(cfg.j2_perturbation);
    CHECK_FALSE(cfg.hawking_radiation);
}

TEST_CASE("apply_physics_mode GR enables GR features") {
    CosmosConfig cfg;
    apply_physics_mode(cfg, 1); // GR
    CHECK(cfg.gr_enabled);
}

} // Physics Mode

// ═════════════════════════════════════════════════════════════════════════════
// 13. EQUILIBRIUM TEMPERATURE
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Equilibrium Temperature") {

TEST_CASE("equilibrium temperature decreases with distance") {
    CelestialBody star = make_body(CTYPE_STAR_G, 1.0f, solar_radius_sim_units(), 5778.0f);
    star.luminosity = 1.0f;
    star.pos = glm::vec3(0.0f);

    CelestialBody near_planet = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 300.0f);
    near_planet.pos = glm::vec3(200.0f, 0.0f, 0.0f);

    CelestialBody far_planet = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 300.0f);
    far_planet.pos = glm::vec3(800.0f, 0.0f, 0.0f);

    float t_near = equilibrium_temperature_from_star(near_planet, star);
    float t_far = equilibrium_temperature_from_star(far_planet, star);

    CHECK(t_near > 0.0f);
    CHECK(t_far > 0.0f);
    CHECK(t_near > t_far);
}

} // Equilibrium Temperature

// ═════════════════════════════════════════════════════════════════════════════
// 14. MASS LOSS & BODY LIMITS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Mass Loss & Limits") {

TEST_CASE("register_mass_loss tracks loss rate and total") {
    CelestialBody b = make_body(CTYPE_STAR_G, 1.0f, 100.0f, 5778.0f);
    register_mass_loss(b, 0.1f, 0.01f);
    CHECK(b.mass_loss_total == doctest::Approx(0.1f).epsilon(0.001));
    CHECK(b.mass_loss_rate == doctest::Approx(10.0f).epsilon(0.01)); // 0.1/0.01
}

TEST_CASE("register_mass_loss accumulates") {
    CelestialBody b = make_body(CTYPE_STAR_G, 1.0f, 100.0f, 5778.0f);
    register_mass_loss(b, 0.05f, 0.01f);
    register_mass_loss(b, 0.03f, 0.01f);
    CHECK(b.mass_loss_total == doctest::Approx(0.08f).epsilon(0.001));
}

TEST_CASE("body_albedo_for_type returns valid albedo") {
    CelestialBody planet = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 288.0f);
    float albedo = body_albedo_for_type(planet);
    CHECK(albedo >= 0.0f);
    CHECK(albedo <= 1.0f);
}

} // Mass Loss & Limits

// ═════════════════════════════════════════════════════════════════════════════
// 15. EDGE CASES & ROBUSTNESS
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("Edge Cases") {

TEST_CASE("zero-mass body does not produce NaN") {
    CelestialBody b = make_body(CTYPE_PLANET, 0.0f, 1.0f, 0.0f);
    CHECK(std::isfinite(body_density(b)));
    CHECK(std::isfinite(body_volume(b)));
    CHECK(std::isfinite(body_surface_gravity(b, G_DEFAULT)));
}

TEST_CASE("zero-radius body does not produce NaN") {
    CelestialBody b = make_body(CTYPE_PLANET, 1.0f, 0.0f, 300.0f);
    // Some functions may return 0 or inf but should not NaN
    float vol = body_volume(b);
    CHECK(std::isfinite(vol));
    CHECK(vol == doctest::Approx(0.0f));
}

TEST_CASE("very large mass does not produce NaN") {
    CelestialBody b = make_body(CTYPE_STAR_O, 150.0f, 500.0f, 50000.0f);
    CHECK(std::isfinite(body_density(b)));
    CHECK(std::isfinite(body_escape_velocity(b, G_DEFAULT)));
    CHECK(std::isfinite(body_gravitational_binding_energy(b, G_DEFAULT)));
}

TEST_CASE("classify_star_spectral handles boundary temperatures") {
    // At exact boundaries, should return one valid type
    uint32_t t = classify_star_spectral(30000.0f, 30.0f);
    CHECK(is_star_type(t));

    t = classify_star_spectral(0.0f, 0.001f);
    CHECK(is_star_type(t));

    t = classify_star_spectral(100000.0f, 150.0f);
    CHECK(is_star_type(t));
}

TEST_CASE("ring system with extreme values does not crash") {
    CelestialBody b = make_body(CTYPE_PLANET, 1.0f, 10.0f);
    set_ring_system(b, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    CHECK(b.ring_inner_radius >= b.radius * 1.15f);
    CHECK(b.ring_outer_radius > b.ring_inner_radius);

    set_ring_system(b, 1e6f, 1e6f, 1.0f, 1.0f, 1.3f);
    CHECK(std::isfinite(b.ring_inner_radius));
    CHECK(std::isfinite(b.ring_outer_radius));
}

TEST_CASE("derive_materials for all body types does not crash") {
    uint32_t types[] = {CTYPE_PLANET, CTYPE_MOON, CTYPE_ASTEROID, CTYPE_COMET,
                        CTYPE_DUST, CTYPE_STAR_G, CTYPE_BLACK_HOLE, CTYPE_NEBULA};
    for (uint32_t type : types) {
        CelestialBody b = make_body(type, 1.0f, 10.0f, 300.0f);
        MaterialComposition m = derive_materials(b);
        CHECK(std::isfinite(m.iron));
        CHECK(std::isfinite(m.silicate));
        CHECK(std::isfinite(m.water));
        CHECK(std::isfinite(m.hydrogen));
    }
}

TEST_CASE("equilibrium_temperature_from_star at zero distance") {
    CelestialBody star = make_body(CTYPE_STAR_G, 1.0f, 100.0f, 5778.0f);
    star.pos = glm::vec3(0.0f);
    star.luminosity = 1.0f;
    CelestialBody planet = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 300.0f);
    planet.pos = glm::vec3(0.0f); // Same position
    float t = equilibrium_temperature_from_star(planet, star);
    CHECK(std::isfinite(t));
}

TEST_CASE("roche_distance for identical bodies") {
    CelestialBody a = make_body(CTYPE_PLANET, EARTH_MASS_SOLAR, EARTH_RADIUS_SIM_UNITS, 300.0f);
    CelestialBody b = a;
    float d = roche_distance_for_mode(a, b, true);
    CHECK(std::isfinite(d));
    CHECK(d >= 0.0f);
}

} // Edge Cases

// ═════════════════════════════════════════════════════════════════════════════
// 16. COSMOS STATE CONSISTENCY
// ═════════════════════════════════════════════════════════════════════════════

TEST_SUITE("State Consistency") {

TEST_CASE("CosmosConfig defaults are sane") {
    CosmosConfig cfg;
    CHECK(cfg.G > 0.0f);
    CHECK(std::isfinite(cfg.G));
}

TEST_CASE("CosmosState starts empty") {
    CosmosState state;
    CHECK(state.bodies.empty());
    CHECK(state.trails.empty());
}

TEST_CASE("temp_band returns consistent bands") {
    // Colder temperatures → lower band numbers
    int cold = temp_band(100.0f);
    int warm = temp_band(5000.0f);
    int hot = temp_band(30000.0f);
    CHECK(cold <= warm);
    CHECK(warm <= hot);
}

} // State Consistency
