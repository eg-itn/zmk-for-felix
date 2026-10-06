# Custom behaviors for this config (loaded as a Zephyr module via zephyr/module.yml).

if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_TURBO_CLICK app PRIVATE src/behaviors/behavior_turbo_click.c)
endif()
