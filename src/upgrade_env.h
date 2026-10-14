#ifndef UPGRADE_ENV_H
#define UPGRADE_ENV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * What `ipctool upgrade` changes in the U-Boot environment when it moves the
 * partitions, done as an edit of what the camera already has rather than a
 * fresh line: a vendor bootloader's bootargs carry things only it knows about
 * (an mmz carve-out, a console, a panic timeout), and regenerating the line
 * dropped them. Pure functions, so they are tested off a camera.
 */

/* The mtd id in front of the partitions, "hi_sfc" in
 * "... mtdparts=hi_sfc:256k(boot),...". False when bootargs has no literal
 * mtdparts= (one that names a variable, "${mtdparts}", is not literal). */
bool upgrade_mtd_id(const char *bootargs, char *id, size_t idsz);

/* The same from the value of OpenIPC's own `mtdparts` variable,
 * "hi_sfc:256k(boot),...", which its bootloader's default bootargs refer to
 * as mtdparts=${mtdparts}. */
bool upgrade_mtd_id_value(const char *mtdparts, char *id, size_t idsz);

/* Whether bootargs takes its partitions from the `mtdparts` variable
 * (mtdparts=${mtdparts} or mtdparts=$mtdparts), in which case that variable
 * is what has to change. A reference to any other variable is not this: it
 * is neither literal nor the variable upgrade sets, and is refused. */
bool upgrade_bootargs_mtdparts_is_var(const char *bootargs);

/* bootargs with root= pointing at /dev/mtdblock<root_idx> and a literal
 * mtdparts= replaced by `mtdparts` (id included), every other word kept in
 * its place. A mtdparts= that names a variable is left as it is: the
 * variable is set instead. False, leaving `out` unspecified, when either
 * word is missing: an edit that cannot find what it edits must not produce
 * a line that boots the old layout. */
bool upgrade_bootargs(const char *old, int root_idx, const char *mtdparts,
                      char *out, size_t outsz);

/* bootcmd with the kernel read, "sf read <addr> <off> <size>", reading
 * `size` bytes from `off` instead. The address may be a variable, as
 * OpenIPC's bootloader writes it ("sf read ${baseaddr} 0x50000 0x200000");
 * the offset and size are numbers. False when there is no such read, or
 * more than one. */
bool upgrade_bootcmd(const char *old, uint32_t off, uint32_t size, char *out,
                     size_t outsz);

#endif /* UPGRADE_ENV_H */
