#include "griefprot.h"

void gp_config_defaults(gp_config *cfg)
{
	if (!cfg)
		return;

	cfg->copper.name = "copper_nugget";
	cfg->copper.breaks = 32;
	cfg->iron.name = "iron_nugget";
	cfg->iron.breaks = 256;
	cfg->max_faces_per_block = 4;

	cfg->shield_pool = 4096;
	cfg->shield_regen = 20;
	cfg->shield_window = 1;
	cfg->shield_range = 4;
	cfg->shield_max_targets = 64;
	cfg->shield_fuel_max = 64;
	cfg->shield_fuel_per_sec = 1;

	cfg->decay_interval = 300;
	cfg->decay_amount = 1;

	cfg->chunk_height = 384;
}
