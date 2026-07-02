// ── seed_default_population: additional environment cases ───────────────────
// Included directly inside the switch(cfg.environment) block in
// seed_default_population() (biochem_app.cpp).
//
// Each case mirrors the entity composition described in the preset and in
// regenerate_environment() for the same environment.

    case BIO_ENV_GUT:
        // Gut microbiome: bacteria-dominated, rich nutrients, some immune surveillance.
        // Lamina propria has resident macrophages (phagocytes) and IgA antibodies.
        spawn_group(BIO_CELL,        10, 18.0f, core,         1.05f, BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_BACTERIUM,   50, 36.0f, wide,         1.15f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_VIRUS,        4, 48.0f, mid,          0.9f,  BIO_ENV_FEATURE_TOXIN);
        spawn_group(BIO_NUTRIENT,    80,  6.0f, wide,         1.30f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_TOXIN,        8,  8.0f, wide * 0.7f,  1.0f,  BIO_ENV_FEATURE_TOXIN);
        spawn_group(BIO_WHITE_BLOOD,  6, 20.0f, mid,          1.0f,  BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_ANTIBODY,     6, 24.0f, mid,          1.0f,  BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_JANITOR,      4, 22.0f, mid * 0.9f,   1.0f,  BIO_ENV_FEATURE_MEMBRANE);
        break;

    case BIO_ENV_BLOOD:
        // Blood stream: RBC-dominated, WBC patrol, antibodies, dissolved nutrients.
        // Small pathogen load represents acute infection scenario.
        spawn_group(BIO_RED_BLOOD,   40, 22.0f, wide,         1.05f, BIO_ENV_FEATURE_CURRENT);
        spawn_group(BIO_WHITE_BLOOD,  8, 18.0f, mid,          1.05f, BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_ANTIBODY,     6, 26.0f, mid,          1.0f,  BIO_ENV_FEATURE_CURRENT);
        spawn_group(BIO_JANITOR,      4, 20.0f, mid,          1.0f,  BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_BACTERIUM,    6, 34.0f, core * 1.2f,  0.9f,  BIO_ENV_FEATURE_CURRENT);
        spawn_group(BIO_VIRUS,        4, 50.0f, core,         0.9f,  BIO_ENV_FEATURE_CURRENT);
        spawn_group(BIO_NUTRIENT,    24,  8.0f, wide * 0.8f,  1.15f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_TOXIN,        4, 10.0f, core,         0.9f,  BIO_ENV_FEATURE_TOXIN);
        break;

    case BIO_ENV_SOIL:
        // Soil rhizosphere: diverse bacteria community, some amoeboid predators,
        // rich nutrient/toxin variation reflecting mineral heterogeneity.
        spawn_group(BIO_CELL,         4, 20.0f, mid,          0.9f,  BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_BACTERIUM,   54, 38.0f, wide,         1.10f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_VIRUS,        8, 46.0f, mid,          0.95f, BIO_ENV_FEATURE_TOXIN);
        spawn_group(BIO_NUTRIENT,    60,  6.0f, wide,         1.20f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_TOXIN,       14,  8.0f, wide * 0.85f, 1.0f,  BIO_ENV_FEATURE_TOXIN);
        break;

    case BIO_ENV_WOUND:
        // Wound site: WBC surge (neutrophils, macrophages), moderate bacteria
        // (commensals + opportunistic pathogens), serum nutrients, RBC debris.
        spawn_group(BIO_CELL,         8, 20.0f, core,         0.95f, BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_BACTERIUM,   18, 36.0f, mid,          1.05f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_VIRUS,        4, 48.0f, core * 1.2f,  0.9f,  BIO_ENV_FEATURE_TOXIN);
        spawn_group(BIO_NUTRIENT,    30,  8.0f, wide * 0.8f,  1.1f,  BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_TOXIN,       14, 10.0f, mid,          1.0f,  BIO_ENV_FEATURE_TOXIN);
        spawn_group(BIO_RED_BLOOD,   10, 16.0f, wide * 0.85f, 0.85f, BIO_ENV_FEATURE_CURRENT);
        spawn_group(BIO_WHITE_BLOOD, 12, 20.0f, mid,          1.1f,  BIO_ENV_FEATURE_MEMBRANE);
        spawn_group(BIO_ANTIBODY,     6, 24.0f, mid * 1.1f,   1.05f, BIO_ENV_FEATURE_NUTRIENT);
        spawn_group(BIO_JANITOR,      8, 22.0f, mid,          1.05f, BIO_ENV_FEATURE_MEMBRANE);
        break;
