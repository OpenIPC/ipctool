#ifndef TEGRA_H
#define TEGRA_H

#include <stdbool.h>

bool tegra_detect_cpu(char *chip_name);
void tegra_setup_hal();

#endif /* TEGRA_H */
