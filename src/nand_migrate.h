#ifndef NAND_MIGRATE_H
#define NAND_MIGRATE_H

int nand_migrate_cmd(int argc, char **argv);
// The flash phase, run as PID 1 from the RAM root nand_migrate_cmd staged.
int nand_migrate_pid1(void);

#endif /* NAND_MIGRATE_H */
